/* internal.h: ramfs internal definitions
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef GZA_HEADER
#define GZA_HEADER

extern const struct address_space_operations ramfs_aops;
extern const struct inode_operations ramfs_file_inode_operations;


#define GFS_BLOCK_SIZE 1024
#define GFS_BLOCK_SIZE_BIT 10

#define FS_MAGIC 0x12341234

#define MAX_FILE_BLOCK_NUM 10
#define MAX_GZA_FILESIZE (MAX_FILE_BLOCK_NUM * BLOCK_SIZE) 

#define INODE_SIZE 64
#define DIRECTORY_SIZE 256
#define INODE_DATA_COUNT 10

#define MAX_DIR_NAME 192
#define ROOT_INODE_NUM 1
struct gza_inode 
{
	u32 num;
	umode_t mode;
	int length;
	dev_t dev;
	unsigned int data[10];
};

typedef struct
{
	u32 inode_bitmap_begin;
	u32 inode_bitmap_block_num;
	u32 inode_num;
	u32 data_bitmap_begin;
	u32 data_bitmap_block_num;
	u32 data_num;
	u32 inode_begin;
	u32 inode_block_num;
	u32 data_begin;
	u32 data_block_num;
	u32 block_size;
	
	u32 magic;

} __attribute__ ((packed)) gzafs_sb_info;

struct directory {
	char d_name[MAX_DIR_NAME];
	int d_len;
	int d_status;
	int d_num;
};

struct ramfs_mount_opts {
	umode_t mode;
};

struct ramfs_fs_info {
	struct ramfs_mount_opts mount_opts;
	gzafs_sb_info sbinfo;
};

enum SET_FLAG{
	UNSET=0,
	SET=1,
};


int get_dev_content(struct block_device *bdev, loff_t offset, char * buff, int size);
void set_dev_content(struct block_device *bdev, loff_t offset, char * buff, int size);
void set_dev_bit(struct block_device *bdev, loff_t offset, int bitoffset, enum SET_FLAG);
int find_valid_inode_num(struct block_device *bdev, loff_t begin, loff_t end);
int find_valid_data_num(struct block_device *bdev, loff_t begin, loff_t end);
int find_valid_bit_num(struct block_device *bdev, loff_t begin, loff_t end);

int gfs_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh, int create); 
#endif
