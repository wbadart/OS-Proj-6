
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
    if(SUPER.magic == FS_MAGIC){
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

    // Get info from super block
    union fs_block superblock;
    disk_read(0, superblock.data);

    // Check magic value to verify validitiy
    if(superblock.super.magic != FS_MAGIC){
        printf("ERROR: Invalid magic value 0x%x", superblock.super.magic);
        return 0;
    }

    // Initialize and build free block bitmap
    G_FREE_BLOCK_BITMAP = malloc(superblock.super.ninodes / 8);

    // For each inode block...
    for(int i = 1; i <= superblock.super.ninodeblocks; i++){

        // Read in the inode block
        union fs_block block;
        disk_read(i, block.data);

        // For each inode in the block we just read...
        for(int j = 0; j < INODES_PER_BLOCK; j++){

            // Skip empty inodes
            if(!block.inode[j].isvalid) continue;

            // Calculate the target byte of the bitmap
            // Should be floor(inode_number / sizeof(char))
            int inode_number = INODE_NUMBER(i, j);

            // Calculate the index of the target bit [0-3]
            int target_byte  = inode_number / 8
              , target_bit   = inode_number % 8;

            // Perform the masking
            G_FREE_BLOCK_BITMAP[target_byte] =
                G_FREE_BLOCK_BITMAP[target_byte] | 1 << target_bit;

            /* printf("INFO: Inode num [%d]\n", INODE_NUMBER(i, j)); */
            /* printf("INFO: target_byte[%d] has %02x\n" */
            /*         , target_byte, G_FREE_BLOCK_BITMAP[target_byte]); */
        }
    }

    // Prepare filesystem for use

    // Return 1 (success code; failure is 0)
    return 1;
}

int fs_create()
{
    return 0;
}

int fs_delete( int inumber )
{
    return 0;
}

int fs_getsize( int inumber )
{
    return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
    return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
    return 0;
}
