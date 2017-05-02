
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
    for(int i = 1; i < SUPER.ninodeblocks + 1; i++){
        //read an inode block
        union fs_block inode_block;
        disk_read(i, inode_block.data);
        for(int j = 0; j < INODES_PER_BLOCK; j++){
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
            for(int k = 0; k < POINTERS_PER_INODE && direct_block.inode[j].direct[k]; k++)
                printf("%d ", direct_block.inode[j].direct[k]);
            printf("\n");

            // If the inode has an indirect pointer, process the target indoe
            if(!direct_block.inode[j].indirect) continue;

            // Report the indirect block info located within the inode
            printf("    indirect block: %d\n", direct_block.inode[j].indirect);
            printf("    indirect data blocks: ");

            // Read in the indrect block and process it
            union fs_block indirect_block;
            disk_read(block.inode[j].indirect, indirect_block.data);

            // For each inode in the indirect block...
            for(int l = 0; l < INODES_PER_BLOCK; l++){

                // Ignore invalid blocks
                if(!indirect_block.inode[l].isvalid) continue;

                // Report the direct pointers in the inode
                for(int m = 0; m < POINTERS_PER_INODE && indirect_block.inode[l].direct[m]; m++)
                    printf("%d ", indirect_block.inode[l].direct[m]);
            }
            // Print the newline after the full inode report
            printf("\n");
        }
    }
}

int fs_mount(){
    // set BEEN_MOUNTED to true
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
    G_FREE_BLOCK_BITMAP = malloc(superblock.super.nblocks);

    // For each inode block...
    for(int i = 1; i <= superblock.super.ninodeblocks; i++){

        // Read in the inode block
        union fs_block block;
        disk_read(i, block.data);

        // For each inode in the block we just read...
        for(int j = 0; j < INODES_PER_BLOCK; j++){

            // Skip empty inodes
            if(!block.inode[j].isvalid) continue;

            // Mark each used block as such in the bitmap
            for(int k = 0; k < POINTERS_PER_INODE && block.inode[j].direct[k]; k++)
                G_FREE_BLOCK_BITMAP[block.inode[j].direct[k]] = 1;

            // Record any blocks in use via indirection (else continue)
            if(!block.inode[j].indirect) continue;

            union fs_block indirect_block;
            disk_read(block.inode[j].indirect, indirect_block.data);

            // For each inode in the indirect block...
            for(int k = 0; k < INODES_PER_BLOCK; k++){

                // Skip empty inodes
                if(!indirect_block.inode[k].isvalid) continue;

                // Mark each used block as such in the bitmap
                for(int l = 0; l < POINTERS_PER_INODE && indirect_block.inode[k].direct[l]; l++)
                    G_FREE_BLOCK_BITMAP[indirect_block.inode[k].direct[l]] = 1;
            }
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
    for(int i = 1; i <= superblock.super.ninodeblocks; i++){

        union fs_block block;
        disk_read(i, block.data);

        // For each inode in the block...
        for(int j = 0; j < INODES_PER_BLOCK; j++){

            // Skip VALID inodes
            if(block.inode[j].isvalid) continue;

            // Initialize the found inode
            block.inode[j].isvalid = 1;
            block.inode[j].size    = 0;
            block.inode[j].indirect = 0;
            for(int k = 0; k < POINTERS_PER_INODE; k++)
                block.inode[j].direct[k] = 0;

            // Write the changes to disk
            disk_write(i, block.data);

            // Calculate and return the inumber
            return INODE_NUMBER(i, j);
        }
    }

    return 0;
}

int fs_delete( int inumber )
{
    //figure out what block the inode is on and read that block
    int block_num = (inumber - 1 - inumber % INODES_PER_BLOCK)/(INODES_PER_BLOCK) + 1;
    union fs_block block;
    disk_read(block_num, block.data);
    //figure out index of inode on that block
    int inode_index = inumber % INODES_PER_BLOCK;
    printf("inode %d is the %dth inode on the %dth block\n", inumber, inode_index, block_num);
    if(!block.inode[inode_index].isvalid){
        printf("inode %d is invalid, cannot be deleted\n", inumber);
        return 0;
    }
    //update values in free block map
    //check direct pointers
    for(int i = 0; i < POINTERS_PER_INODE; i++){
        int b = block.inode[inode_index].direct[i];
        if(b)
            G_FREE_BLOCK_BITMAP[b] = 0;
    }
    //check indirect pointer
    union fs_block indirect_block;
    int indirect_block_num = block.inode[inode_index].indirect;
    disk_read(indirect_block_num, indirect_block.data);
    for(int i = 0; i < POINTERS_PER_BLOCK; i++){
        int b = indirect_block.pointers[i];
        if(b)
            G_FREE_BLOCK_BITMAP[b] = 0;
    }

    //set all the values of the inode block to 0
    block.inode[inode_index].isvalid = 0;
    for(int i = 0; i < POINTERS_PER_INODE; i++)
        block.inode[inode_index].direct[i] = 0;
    block.inode[inode_index].indirect = 0;
    //set all the values of the indirect block to 0
    for(int i = 0; i < POINTERS_PER_BLOCK; i++)
        indirect_block.pointers[i] = 0;
    //write both blocks back
    disk_write(indirect_block_num, indirect_block.data);
    disk_write(block_num, block.data);
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

int fs_read( int inumber, char *data, int length, int offset )
{
    return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
    return 0;
}
