
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

int *G_FREE_BLOCK_BITMAP;

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

int fs_format()
{
    return 0;
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
        disk_read(i, block.data);

        // For each inode in the block we just read...
        for(int j = 0; j < INODES_PER_BLOCK; j++){

            // Skip invalid inodes, we only care about the ones
            // that refer to actual files
            if(!block.inode[j].isvalid) continue;

            // Regular inode debugging output
            printf("inode %d:\n", INODES_PER_BLOCK * (i-1) + j);
            printf("    size: %d bytes\n", block.inode[j].size);
            printf("    direct blocks: ");

            // Report each direct block pointer (the list is null terminated and
            // does not exceed POINTERS_PER_INODE in length)
            for(int k = 0; k < POINTERS_PER_INODE && block.inode[j].direct[k]; k++)
                printf("%d ", block.inode[j].direct[k]);
            printf("\n");

            // If the inode has an indirect pointer, process the target indoe
            if(!block.inode[j].indirect) continue;

            // Report the indirect block info located within the inode
            printf("    indirect block: %d\n", block.inode[j].indirect);
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

int fs_mount()
{
    printf("INFO: Mounting...\n");
    union fs_block block;
    disk_read(0, block.data);

    for(int i = 1; i <= block.super.ninodeblocks; i++){
        disk_read(i, block.data);
        for(int j = 0; j < INODES_PER_BLOCK; j++){
            if(block.inode[j].isvalid){
                *G_FREE_BLOCK_BITMAP = *G_FREE_BLOCK_BITMAP | (1 << INODES_PER_BLOCK * (i-1) * j);
            }
        }
    }

    return 0;
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
