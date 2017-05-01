
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

void fs_debug()
{
    union fs_block block;

    disk_read(0,block.data);

    printf("superblock:\n");
    printf("    %d blocks\n",block.super.nblocks);
    printf("    %d inode blocks\n",block.super.ninodeblocks);
    printf("    %d inodes\n",block.super.ninodes);

    for(int i = 1; i <= block.super.ninodeblocks; i++){
        disk_read(i, block.data);
        for(int j = 0; j < INODES_PER_BLOCK; j++){
            if(block.inode[j].isvalid){
                printf("inode %d:\n", INODES_PER_BLOCK * (i-1) + j);
                printf("    size: %d bytes\n", block.inode[j].size);
                printf("    direct blocks:\n");
            }
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
