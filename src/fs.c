
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

#define DIVIDE(a, b) (a % b ? a / b + 1 : a / b)
#define INODE_NUMBER(blockno, index) (INODES_PER_BLOCK * (blockno-1) + index)

int BEEN_MOUNTED = 0;
char *G_FREE_BLOCK_BITMAP;

struct fs_superblock {
    int magic;
    int nblocks;
    int ninodeblocks;
    int ninodes;
};

struct fs_inode {
    int isvalid;
    int size;
    int direct[POINTERS_PER_INODE];
    int indirect;
};

union fs_block {
    struct fs_superblock super;
    struct fs_inode inode[INODES_PER_BLOCK];
    int pointers[POINTERS_PER_BLOCK];
    char data[DISK_BLOCK_SIZE];
};

struct fs_superblock SUPER = {0x00000000, 0, 0, 0};
int next_free_block();

int fs_format()
{
    //check if disk is already mounted
    if(BEEN_MOUNTED){
        printf("Cannot format a disk that is already mounted\n");
        return 0;
    }
    //set aside 10% of blocks for inodes
    int nblocks = disk_size();
    int ninodeblocks = DIVIDE(nblocks, 10);
    int ninodes = ninodeblocks * INODES_PER_BLOCK;
    //set appropriate superblock SUPER values
    SUPER.magic = FS_MAGIC;
    SUPER.nblocks = nblocks;
    SUPER.ninodeblocks = ninodeblocks;
    SUPER.ninodes = ninodes;
    //destroy any data already present by setting all the inode isvalid bits to 0
    int i;
    for(i = 1; i < SUPER.ninodeblocks + 1; i++){
        //read an inode block
        union fs_block inode_block;
        disk_read(i, inode_block.data);
        int j;
        for(j = 0; j < INODES_PER_BLOCK; j++){
            //set all the inodes in that block to invalid
            inode_block.inode[j].isvalid = 0;
        }
        //write block back to disk
        disk_write(i, inode_block.data);
    }
    return 1;
}

void fs_debug(){

    // Process the super block
    union fs_block block;
    disk_read(0, block.data);

    // Debugging output for super block
    printf("superblock:\n");
    printf("    magic number is %svalid\n", (block.super.magic == FS_MAGIC ? "" : "in"));
    printf("    %d blocks\n",block.super.nblocks);
    printf("    %d inode blocks\n",block.super.ninodeblocks);
    printf("    %d inodes\n",block.super.ninodes);

    // For each inode block (this excludes the super block
    // at index 0)...
    for(int i = 1; i <= block.super.ninodeblocks; i++){

        // Read the block from disk to the block struct
        union fs_block direct_block;
        disk_read(i, direct_block.data);

        // For each inode in the block we just read...
        for(int j = 0; j < INODES_PER_BLOCK; j++){

            // Skip invalid inodes, we only care about the ones
            // that refer to actual files
            if(!direct_block.inode[j].isvalid) continue;

            // Regular inode debugging output
            printf("inode %d:\n", INODE_NUMBER(i, j));
            printf("    size: %d bytes\n", direct_block.inode[j].size);
            printf("    direct blocks: ");

            // Report each direct block pointer (the list is null terminated and
            // does not exceed POINTERS_PER_INODE in length)
            for(int k = 0; k < POINTERS_PER_INODE; k++)
                if(direct_block.inode[j].direct[k]) printf("%d ", direct_block.inode[j].direct[k]);
            printf("\n");

            // If the inode has an indirect pointer, process the target indoe
            if(!direct_block.inode[j].indirect) continue;

            // Report the indirect block info located within the inode
            printf("    indirect block: %d\n", direct_block.inode[j].indirect);
            printf("    indirect data blocks: ");

            // Read in the indrect block and process it
            union fs_block indirect_block;
            disk_read(direct_block.inode[j].indirect, indirect_block.data);

            // Report the direct pointers in the inode
            for(int m = 0; m < POINTERS_PER_BLOCK; m++)
                if(indirect_block.pointers[m] > 0) printf("%d ", indirect_block.pointers[m]);

            // Print the newline after the full inode report
            printf("\n");
        }
    }
}

int fs_mount(){

    BEEN_MOUNTED = 1;

    // Get info from super block
    union fs_block superblock;
    disk_read(0, superblock.data);

    // Check magic value to verify validitiy
    if(superblock.super.magic != FS_MAGIC){
        printf("ERROR: Invalid magic value 0x%x", superblock.super.magic);
        return 0;
    }

    // Initialize and build free block bitmap
    G_FREE_BLOCK_BITMAP = malloc(8 * superblock.super.nblocks);

    // For each inode block...
    for(int i = 1; i <= superblock.super.ninodeblocks; i++){

        // Read in the inode block
        union fs_block block;
        disk_read(i, block.data);

        // Mark all inode blocks as used
        G_FREE_BLOCK_BITMAP[i] = 1;

        // For each inode in the block we just read...
        for(int j = 0; j < INODES_PER_BLOCK; j++){

            // Skip empty inodes
            if(!block.inode[j].isvalid) continue;

            // Mark each used block as such in the bitmap
            for(int k = 0; k < POINTERS_PER_INODE && block.inode[j].direct[k]; k++)
                G_FREE_BLOCK_BITMAP[block.inode[j].direct[k]] = 1;

            // Record any blocks in use via indirection (else continue)
            if(!block.inode[j].indirect) continue;

            // Mark the block of indirect pointers as used
            G_FREE_BLOCK_BITMAP[block.inode[j].indirect] = 1;

            union fs_block indirect_block;
            disk_read(block.inode[j].indirect, indirect_block.data);

            // Update bitmap with indirectly referenced blocks
            for(int k = 0; k < POINTERS_PER_BLOCK; k++)
                if(indirect_block.pointers[k])
                    G_FREE_BLOCK_BITMAP[indirect_block.pointers[k]] = 1;

        }
    }

    // Return 1 (success code; failure is 0)
    return 1;
}

int fs_create(){

    // The objective here is to find an open (invalid) inode, initialize
    // it for use, and then return its inumber

    union fs_block superblock;
    disk_read(0, superblock.data);

    // For each inode block...
    int i;
    for(i = 1; i <= superblock.super.ninodeblocks; i++){

        union fs_block block;
        disk_read(i, block.data);

        // For each inode in the block...
        int j;
        for(j = 0; j < INODES_PER_BLOCK; j++){

            // Skip VALID inodes
            if(block.inode[j].isvalid || !INODE_NUMBER(i, j)) continue;
            printf("INFO: Found ivalid invode (%d, %d)=%d\n", i, j, INODE_NUMBER(i, j));

            // Initialize the found inode
            block.inode[j].isvalid  = 1;
            block.inode[j].size     = 0;
            block.inode[j].indirect = 0;
            int k;
            for(k = 0; k < POINTERS_PER_INODE; k++)
                block.inode[j].direct[k] = 0;

            // Write the changes to disk
            disk_write(i, block.data);

            // Calculate and return the inumber
            return INODE_NUMBER(i, j);
        }
    }

    return 0;
}

int fs_delete( int inumber ){

    // Grab that super block
    union fs_block superblock;
    disk_read(0, superblock.data);

    // Check the validity of the inumber
    if(inumber > superblock.super.ninodes){
        printf("ERROR: Inumber %d is out of range.\n", inumber);
        return 0;
    } else if(inumber == 0){
        printf("ERROR: Cannot delete inode 0\n.");
        return 0;
    }

    // Calculate target block
    int blockno = inumber / INODES_PER_BLOCK + 1;

    // Read in the target block
    union fs_block block;
    disk_read(blockno, block.data);

    // Index into the block
    int inode_index = inumber % INODES_PER_BLOCK;

    // Verify inode validity (can't delete an invalid inode)
    if(!block.inode[inode_index].isvalid){
        printf("ERROR: Cannot delete invalid inode \"%d\"\n", inumber);
        return 0;
    }

    // Update values in free block map, check direct pointers
    for(int i = 0; i < POINTERS_PER_INODE; i++){
        int b = block.inode[inode_index].direct[i];
        if(b > 0){
            G_FREE_BLOCK_BITMAP[b] = 0;
            block.inode[inode_index].direct[i] = 0;
        }
    }

    // Check indirect pointer
    union fs_block indirect_block;
    int indirect_block_num = block.inode[inode_index].indirect;

    // If inode has indirect pointer, update those blocks in bitmap too
    if(indirect_block_num){

        // Read indirect block
        disk_read(indirect_block_num, indirect_block.data);

        for(int i = 0; i < POINTERS_PER_BLOCK; i++){
            int b = indirect_block.pointers[i];
            if(b){
                G_FREE_BLOCK_BITMAP[b] = 0;
                indirect_block.pointers[i] = 0;
            }
        }
        disk_write(indirect_block_num, indirect_block.data);
    }

    // Nuke the metadata
    block.inode[inode_index].isvalid = 0;
    block.inode[inode_index].size    = 0;

    // Save changes to disk
    disk_write(blockno, block.data);
    return 1;
}

int fs_getsize( int inumber )
{
    int block_num = inumber / INODES_PER_BLOCK + 1;
    union fs_block block;
    disk_read(block_num, block.data);
    int inode_index = inumber % INODES_PER_BLOCK;
    if(!block.inode[inode_index].isvalid){
        printf("inode %d is invalid\n", inumber);
        return -1;
    } else
        return block.inode[inode_index].size;
}

int fs_read( int inumber, char *data, int length, int offset ){
    // get inode info
    int block_num = inumber / INODES_PER_BLOCK + 1;
    union fs_block block;
    disk_read(block_num, block.data);
    int inode_index = inumber % INODES_PER_BLOCK;
    // check if inode is valid
    if(!block.inode[inode_index].isvalid){
        printf("inode %d is invalid\n", inumber);
        return 0;
    }
    //printf("inode %d is valid\n", inumber);
    int bytesread = 0;
    int startblock = offset / 4096;
    //printf("offset is %d so start reading at %d pointer\n", offset, startblock);
    //while(bytesread < length){
        // start reading from direct pointers
        for(int i = startblock; i < POINTERS_PER_INODE; i++){
            int b = block.inode[inode_index].direct[i];
            //printf("direct pointer %d points to %d\n", i, b);
            if(b){
                // read data block
                union fs_block data_block;
                disk_read(b, data_block.data);
                // copy that data to end of our char *data
                int numbytes = sizeof(data_block.data);
                memcpy(data + bytesread, data_block.data, numbytes);
                //printf("copied %d bytes\n", numbytes);
                // increment bytes read
                bytesread += numbytes;
                //printf("directly read %d out of %d bytes\n", bytesread, length);
                if(bytesread >= length)
                    return bytesread;
            }
        }
        //printf("finished reading direct blocks\n");
        // once finished reading direct blocks, go to indirect blocks
        int indirect_block_num = block.inode[inode_index].indirect;
        //printf("indirect pointer points to %d\n", indirect_block_num);
        // if indirect pointer is not null
        if(indirect_block_num > 0){
            union fs_block indirect_block;
            // read indirect block
            disk_read(indirect_block_num, indirect_block.data);
            for(int i = startblock - 5; i < POINTERS_PER_BLOCK; i++){
                int b = indirect_block.pointers[i];
                //printf("indirect pointer %d points to %d\n", i, b);
                if(b > 0){
                    // if pointer is not null
                    union fs_block data_block;
                    // read data block
                    disk_read(b, data_block.data);
                    // copy that data to end of our char *data
                    int numbytes = sizeof(data_block.data);
                    memcpy(data + bytesread, data_block.data, numbytes);
                    // increment bytes read
                    bytesread += numbytes;
                    //printf("indirectly read %d out of %d bytes\n", bytesread, length);
                    if(bytesread >= length)
                        return bytesread;
                }
            }
        }
    //}
    return bytesread;
}

int fs_write( int inumber, const char *data, int length, int offset ){

    printf("INFO: fs_write got length %d and offset %d\n", length, offset);

    // Get the super block for some sanity checks
    union fs_block superblock;;
    disk_read(0, superblock.data);

    // Perform checks on passed inumber
    if(inumber <= 0 || inumber > superblock.super.ninodes){
        printf("ERROR: inumber '%d' out of range.\n", inumber);
        return 0;
    }

    // Get the block and index to get the inode itself
    int blockno = inumber / INODES_PER_BLOCK + 1
      , i_index = inumber % INODES_PER_BLOCK;

    union fs_block block;
    disk_read(blockno, block.data);

    // More sanity checks
    if(!block.inode[i_index].isvalid){
        printf("ERROR: Inode #%d is invalid!\n", inumber);
        return 0;
    }

    // Initialize helper data
    int bytes_written = 0
      , start_block   = offset / DISK_BLOCK_SIZE;

    // Write them bytes
    for(int i = start_block; i < POINTERS_PER_INODE; i++){

        // Find out if inode has direct block ready
        if(block.inode[i_index].direct[i]){

            // Write 4kb at a time
            printf("INFO: Writing to direct block %d\n", block.inode[i_index].direct[i]);
            disk_write(block.inode[i_index].direct[i], data + bytes_written);
            G_FREE_BLOCK_BITMAP[block.inode[i_index].direct[i]] = 1;
            bytes_written += DISK_BLOCK_SIZE;

            block.inode[i_index].size += DISK_BLOCK_SIZE;
            disk_write(blockno, block.data);

            if(bytes_written >= length) return bytes_written;
        }

        // Otherwise allocate a new block
        // If there's room for another direct pointer...
        else if(i < POINTERS_PER_INODE){

            // Locate the next free block
            int target_block = next_free_block();
            if(!target_block) return 0;

            // Update the indode and the bitmap
            block.inode[i_index].direct[i] = target_block;
            block.inode[i_index].size += DISK_BLOCK_SIZE;
            disk_write(blockno, block.data);
            G_FREE_BLOCK_BITMAP[target_block] = 1;

            // Write the data
            printf("INFO: Writing to allocated direct block %d\n", target_block);
            disk_write(target_block, data + bytes_written);
            bytes_written += DISK_BLOCK_SIZE;

            if(bytes_written >= length) return bytes_written;
        }
    }

    // If there's still data to be written, use indirection, else done
    if(bytes_written >= length) return bytes_written;

    // If the inode's indirect pointer isn't set, find a block for it
    if(!block.inode[i_index].indirect){

        // Locate the next open block
        int target_block = next_free_block();
        if(!target_block) return 0;

        // If a free block was found, update the inode and save changes
        block.inode[i_index].indirect = target_block;
        disk_write(blockno, block.data);
    }

    // Read in the indirect block
    union fs_block indirect_block;
    disk_read(block.inode[i_index].indirect, indirect_block.data);

    for(int i = start_block - 5; i < POINTERS_PER_BLOCK; i++){

        // If the indrect data block has already been allocated...
        if(indirect_block.pointers[i]){

            // Update the bitmap
            G_FREE_BLOCK_BITMAP[indirect_block.pointers[i]] = 1;
            block.inode[i_index].size += DISK_BLOCK_SIZE;
            disk_write(blockno, block.data);

            // Write the data, update bytes written
            disk_write(indirect_block.pointers[i], data + bytes_written);
            bytes_written += DISK_BLOCK_SIZE;
            printf("INFO: Writing to indirect block %d\n", indirect_block.pointers[i]);
            if(bytes_written >= length) return bytes_written;
        }

        // Otherwise allocte a new block
        else{

            // Find the block
            int target_block = next_free_block();
            if(!target_block) return 0;

            // Update the indrect pointer list, save changes, update bitmap
            indirect_block.pointers[i] = target_block;
            disk_write(block.inode[i_index].indirect, indirect_block.data);
            G_FREE_BLOCK_BITMAP[target_block] = 1;

            block.inode[i_index].size += DISK_BLOCK_SIZE;
            disk_write(blockno, block.data);

            // Write the raw data and update bytes written
            disk_write(target_block, data + bytes_written);
            printf("INFO: Writing to allocated indirect block %d\n", target_block);
            if(bytes_written >= length) return bytes_written;
        }
    }

    // Now we should be done
    return bytes_written;
}

int next_free_block(){

    // Get the super block for nblocks
    union fs_block superblock;
    disk_read(0, superblock.data);

    // Scan the bitmap for an opening
    for(int i = 1; i <= superblock.super.nblocks; i++)
        if(!G_FREE_BLOCK_BITMAP[i]) return i;
    printf("ERROR: No free blocks.\n");
    return 0;
}

