/*
 * Resizable simple ram filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 *
 * Usage limits added by David Gibson, Linuxcare Australia.
 * This file is released under the GPL.
 */

/*
 * NOTE! This filesystem is probably most useful
 * not as a real filesystem, but as an example of
 * how virtual filesystems can be written.
 *
 * It doesn't get much simpler than this. Consider
 * that this file implements the full semantics of
 * a POSIX-compliant read-write filesystem.
 *
 * Note in particular how the filesystem does not
 * need to implement any data structures of its own
 * to keep track of the virtual data: using the VFS
 * caches is sufficient.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <asm/uaccess.h>
#include "internal.h"

#define RAMFS_DEFAULT_MODE	0755

static const struct super_operations ramfs_ops;
static const struct inode_operations ramfs_dir_inode_operations;
static const struct file_operations zramfs_dir_operations;

enum {
	Opt_mode,
	Opt_err
};


static const match_table_t tokens = {
	{Opt_mode, "mode=%o"},
	{Opt_err, NULL}
};



static struct backing_dev_info ramfs_backing_dev_info = {
	.name		= "zramfs",
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= //BDI_CAP_NO_ACCT_AND_WRITEBACK |
			  BDI_CAP_MAP_DIRECT | BDI_CAP_MAP_COPY |
			  BDI_CAP_READ_MAP | BDI_CAP_WRITE_MAP | BDI_CAP_EXEC_MAP,
};

struct inode *ramfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current_fsuid();
		inode->i_gid = current_fsgid();
		inode->i_mapping->a_ops = &ramfs_aops;
		//inode->i_mapping->backing_dev_info = &ramfs_backing_dev_info;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &ramfs_file_inode_operations;
			inode->i_fop = &ramfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &ramfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
	}
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
ramfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode * inode = ramfs_get_inode(dir->i_sb, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		if (dir->i_mode & S_ISGID) {
			inode->i_gid = dir->i_gid;
			if (S_ISDIR(mode))
				inode->i_mode |= S_ISGID;
		}
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	}
	return error;
}

static int ramfs_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	int retval = 0;
	retval = ramfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int ramfs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd)
{
	return ramfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int ramfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = ramfs_get_inode(dir->i_sb, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			if (dir->i_mode & S_ISGID)
				inode->i_gid = dir->i_gid;
			d_instantiate(dentry, inode);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
		} else
			iput(inode);
	}
	return error;
}
/*
int find_valid_inode_num(struct block_device *bdev, loff_t begin, loff_t end)
{
	int block_size = bdev->bd_block_size;
	int block_bits = bdev->bd_inode->i_blkbits;
	int begin_block = begin >> block_bits;
	int end_block = end >> block_bits;
	int off = begin & ((1<<block_bits) - 1);
	int left = end & ((1<<block_bits) - 1);
	struct buffer_head *bh; 
	char *cur = NULL;
	int count;
	int i = begin_block - 1;
	int byte = 0;
	int bit = 0;
	while (++i <= end_block) {
		bh = __bread(bdev, i, block_size);
		count = block_size;
		if (i == end_block)
			count = left;
		if (i == begin_block)
			count -= off;
		if (count == 0)
			break;
		if (PageHighMem(bh->b_page)) {
			cur = kmap_atomic(bh->b_page, KM_USER0);
			cur +=  (int)bh->b_data;
			if (i == begin_block)
				cur += off;	
		} else {
			cur = bh->b_data;
			if (i == begin_block)
				cur = bh->b_data + off;
		}
		
		for(byte=0;byte<count;byte++) {
			if (cur[byte] !=0xff) {
				bit = -1;
				while (++bit<8&& cur[byte]&(1<<bit));
				cur[byte] |= (1<<bit);
				goto end;
			}
		}
		if (PageHighMem(bh->b_page))
			kunmap_atomic(bh->b_page, KM_USER0);
		put_bh(bh);
		continue;
end:
		mark_buffer_dirty(bh);
		put_bh(bh);
		return ((i-begin_block) * block_size + byte) * 8 + bit;
	}
	return 0;
}
*/

int zramfs_find_valid_inode_num(struct super_block *sb) 
{
	
	gzafs_sb_info *gzsb = &((struct ramfs_fs_info*)sb->s_fs_info)->sbinfo; 
	int block_bits = sb->s_blocksize_bits;
	int begin =  gzsb->inode_bitmap_begin << block_bits;
	int end = (gzsb->inode_bitmap_begin + gzsb->inode_bitmap_block_num) << block_bits;
	int num = find_valid_bit_num(sb->s_bdev, begin, end);
 	printk("*** find valid inode:%d, begin:%d, end:%d\n", num, begin, end);
	return num;	

}

struct inode* zramfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
	//struct block_device *bdev = sb->s_bdev;
	//gzafs_sb_info *gzsb = &((struct ramfs_fs_info*)sb->s_fs_info)->sbinfo; 
	struct inode *inode;
	//int block_size = bdev->bd_block_size;
	//int block_bits = bdev->bd_inode->i_blkbits;
	//int begin =  gzsb->inode_bitmap_begin << block_bits;
	//int end = (gzsb->inode_bitmap_begin + gzsb->inode_bitmap_block_num) << block_bits;
	int num = zramfs_find_valid_inode_num(sb);
	struct gza_inode * ginode;
        int res = 0;	
	if (!num)
		return NULL;
	inode = new_inode(sb);
	ginode = kzalloc(sizeof(struct gza_inode), GFP_KERNEL);
	ginode->num = num;
	ginode->mode = mode;
        ginode->length = 0;	
	inode->i_mode = mode;
	inode->i_private = ginode;
	inode->i_ino = num;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_mapping->a_ops = &ramfs_aops;
	//inode->i_mapping->backing_dev_info = &ramfs_backing_dev_info;
	mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
	//mapping_set_unevictable(inode->i_mapping);
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	switch (mode & S_IFMT) {
	default:
		init_special_inode(inode, mode, dev);
		break;
	case S_IFREG:
		inode->i_op = &ramfs_file_inode_operations;
		inode->i_fop = &ramfs_file_operations;
		break;
	case S_IFDIR:
		inode->i_op = &ramfs_dir_inode_operations;
		//inode->i_fop = &simple_dir_operations;
		inode->i_fop = &zramfs_dir_operations;

		/* directory inodes start off with i_nlink == 2 (for "." entry) */
		inc_nlink(inode);
		break;
	case S_IFLNK:
		inode->i_op = &page_symlink_inode_operations;
		break;
	}
	printk(KERN_NOTICE "***zramfs_get_inode, inode:%ld, drity:%ld\n",  inode->i_ino, inode->i_state & I_DIRTY );
	res  = insert_inode_locked(inode);
	if (res) {
		printk(KERN_ERR "zramfs_get_inode, insert hash error, occer fatal error");
	}	
	unlock_new_inode(inode);
        return inode;	
}	

struct inode* zramfs_get_inode_byid(struct super_block *sb, int num)
{
	struct block_device *bdev = sb->s_bdev;
	gzafs_sb_info *gzsb = &((struct ramfs_fs_info*)sb->s_fs_info)->sbinfo; 
	struct inode *inode;
	int block_size = bdev->bd_block_size;
	int block_bits = bdev->bd_inode->i_blkbits;
	
	//int fs_block_size = sb->s_blocksize;
	int fs_block_bits = sb->s_blocksize_bits;
	int begin =  (gzsb->inode_begin << (fs_block_bits - block_bits)) + ((num * INODE_SIZE) >> block_bits);
	int off = (num * INODE_SIZE) & (block_size - 1);
	struct gza_inode *ginode = kzalloc(sizeof(struct gza_inode), GFP_KERNEL);
	umode_t mode = 0;	
	struct buffer_head *bh = NULL;
	void *cur = NULL;
	
	if (!num)
		return NULL;

	bh = __bread(bdev, begin, block_size);
	if (!bh)
		return NULL;

	if (PageHighMem(bh->b_page)) {
		cur = kmap_atomic(bh->b_page, KM_USER0);
		cur +=  (int)bh->b_data;
		cur += off;	
	} else {
		cur = bh->b_data + off;
	}
	
	memcpy(ginode, cur, sizeof(struct gza_inode));
		
	if (PageHighMem(bh->b_page))
		kunmap_atomic(bh->b_page, KM_USER0);
	put_bh(bh);
	
 	printk(KERN_NOTICE "***zramfs_get_inode_byid inode num:%d, ginode-num:%d, mode:%o, first block:%d\n",num, ginode->num,ginode->mode,ginode->data[0]);	
	mode = ginode->mode; 
	//inode = new_inode(sb);
	// param require unsign long
	inode = iget_locked(sb, num);
	if ((inode->i_state & I_NEW) != I_NEW) {
		return inode;
	}
	inode->i_size = (loff_t)ginode->length;
	inode->i_mode = mode;
	inode->i_private = ginode;
	inode->i_ino = num;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	//inode->i_blksize = sb->s_blocksize;
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_mapping->a_ops = &ramfs_aops;
	//inode->i_mapping->backing_dev_info = &ramfs_backing_dev_info;
	mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
	//mapping_set_unevictable(inode->i_mapping);
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	switch (mode & S_IFMT) {
	default:
		init_special_inode(inode, mode, ginode->dev);
		break;
	case S_IFREG:
		inode->i_op = &ramfs_file_inode_operations;
		inode->i_fop = &ramfs_file_operations;
		break;
	case S_IFDIR:
		inode->i_op = &ramfs_dir_inode_operations;
		//inode->i_fop = &simple_dir_operations;
		inode->i_fop = &zramfs_dir_operations;

		/* directory inodes start off with i_nlink == 2 (for "." entry) */
		inc_nlink(inode);
		break;
	case S_IFLNK:
		inode->i_op = &page_symlink_inode_operations;
		break;
	}
	//add inode cache -- new_inode
	//insert_inode_locked(inode);
	unlock_new_inode(inode);
        return inode;	
}	


int zramfs_mknod(struct inode* dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode* inode = zramfs_get_inode(dir->i_sb, mode, dev);
	int error = -ENOSPC;
	if (inode) {
		if (dir->i_mode & S_ISGID) {
			inode->i_gid |= dir->i_gid;
			if (S_ISDIR(mode)) {
				inode->i_mode |= S_ISGID;
			}
		}	
		d_instantiate(dentry, inode);
		error = 0;
		dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	}
	return error;
}

/*
int zramfs_get_valid_diretory(struct inode * inode, const char* name, int len, struct page** page, int *dic_n)
{
	int last_block = MAX_FILE_BLOCK_NUM;
	int cur_block = 0;
	int block_bits = inode->i_blkbits;
	int cur_page;
	struct page *pg;
	void * kaddr;
	struct directory * dty;
	int dic_num;
	int err = -ENOSPC;
	while (cur_block <= last_block) {
		cur_page = MAX_FILE_BLOCK_NUM >> (PAGE_CACHE_SHIFT - block_bits);
		pg = find_lock_page(inode->i_mapping, cur_page);
		if (!pg) {
			err = -ENOSPC;
			goto no_pg_fail;
		}
		if (!PageUptodate(pg)) {
			err = inode->i_mapping->a_ops->readpage(NULL, pg);
			if (err)
				goto fail;
		}
		if (!PageUptodate(pg)) {
			err = lock_page_killable(pg);
			if (err)
				goto fail;
			if (!PageUptodate(pg)){
				err = -EIO;
				goto fail;
			}
		}
		
		kaddr = kmap_atomic(pg, KM_USER0);
		dty = (struct directory*) kaddr;	
		dic_num = 0;
		while (((char *) dty) - (char *)kaddr < PAGE_CACHE_SIZE && dty->d_status) {
			dty++;
			dic_num++;
		}
		if (((char *)dty) - (char *)kaddr < PAGE_CACHE_SIZE) {
			dty->d_status = 1;
			len = (len - 1) > MAX_DIR_NAME ? MAX_DIR_NAME : (len - 1);
			memcpy(dty->d_name, name, len); 
		}

		kunmap_atomic(kaddr, KM_USER0);
		if(((char *)dty) - (char *)kaddr <PAGE_CACHE_SIZE) {
			pagecache_write_end(NULL, inode->i_mapping, (char *)dty - (char*)kaddr, DIRECTORY_SIZE, DIRECTORY_SIZE, pg, NULL);
			*page = pg;
			*dic_n = dic_num;
			return 0;
		}
		cur_block++;
	}
fail:
	unlock_page(pg);
	page_cache_release(pg);
no_pg_fail:
	return err;
}*/

void create_empty_buffers1(struct page *page,
			unsigned long blocksize, unsigned long b_state)
{
	struct buffer_head *bh, *head, *tail;

	struct buffer_head *tbh ;
	tbh = alloc_buffer_head(GFP_NOFS);
	printk("test alloc bh:%p\n", tbh);
	head = alloc_page_buffers(page, blocksize, 1);
	printk("create_empty_buffers1, bh:%p, blocksize:%ld\n", head, blocksize);
	bh = head;
	do {
		bh->b_state |= b_state;
		tail = bh;
		bh = bh->b_this_page;
	} while (bh);
	tail->b_this_page = head;

	spin_lock(&page->mapping->private_lock);
	if (PageUptodate(page) || PageDirty(page)) {
		bh = head;
		do {
			if (PageDirty(page))
				set_buffer_dirty(bh);
			if (PageUptodate(page))
				set_buffer_uptodate(bh);
			bh = bh->b_this_page;
		} while (bh != head);
	}
	attach_page_buffers(page, head);
	spin_unlock(&page->mapping->private_lock);
}

/*
int zramfs_get_valid_diretory(struct inode * inode, struct page** page, int *dic_n)
{
	int last_block = MAX_FILE_BLOCK_NUM;
	int cur_block = 0;
	int block_bits = inode->i_blkbits;
	int cur_page;
	struct page *pg;
	void * kaddr;
	void * dty;
	int dic_num;
	int err = -ENOSPC;
	while (cur_block <= last_block) {
		cur_page = cur_block >> (PAGE_CACHE_SHIFT - block_bits);
		//pg = find_lock_page(inode->i_mapping, cur_page);
		pg = find_or_create_page(inode->i_mapping, cur_page, GFP_KERNEL);
		if (!pg) {
			printk("zramfs_get_valid_directory, can't find lock page\n");
			err = -ENOSPC;
			goto no_pg_fail;
		}
		printk("zramfs_get_valid_directory,lock page:%p\n", pg);
		if (!PageUptodate(pg)) {
			//err = inode->i_mapping->a_ops->readpage(NULL, pg);
			//err = block_read_full_page(pg, gfs_get_block);
			create_empty_buffers1(pg,1<<block_bits, 1);
			
			printk("zramfs_get_valid_directory,page_head::%p\n", page_buffers(pg));
			err = -EIO;
			if (err)
				goto fail;
		}
		return -EIO;
		if (!PageUptodate(pg)) {
			err = lock_page_killable(pg);
			if (err)
				goto fail;
			if (!PageUptodate(pg)){
				err = -EIO;
				goto fail;
			}
		}
		
		kaddr = kmap_atomic(pg, KM_USER0);
		dty = kaddr;	
		dic_num = 0;
		while (dty - kaddr < PAGE_CACHE_SIZE && ((struct directory *)dty)->d_status) {
			dty += DIRECTORY_SIZE;
			dic_num++;
		}

		kunmap_atomic(kaddr, KM_USER0);
		if(dty - kaddr < PAGE_CACHE_SIZE) {
			*page = pg;
			*dic_n = dic_num;
			return 0;
		}
		while (++cur_block >> (PAGE_CACHE_SHIFT - block_bits) > cur_page);
	}
fail:
	unlock_page(pg);
	page_cache_release(pg);
no_pg_fail:
	return err;
}
*/

void copy_dentry(struct directory *dty, struct dentry* dentry) 
{
	int len;
	dty->d_status = 1;
	dty->d_num = dentry->d_inode->i_ino;
	len = dentry->d_name.len > MAX_DIR_NAME ? MAX_DIR_NAME : dentry->d_name.len;
        memcpy(dty->d_name, dentry->d_name.name, len);
	dty->d_len = len;
}

int zramfs_get_data_block(struct super_block * sb) {
	int begin, end;
	int block_num = -ENOSPC;
        struct block_device *bdev = sb->s_bdev;
	gzafs_sb_info * sbinfo = &((struct ramfs_fs_info*)sb->s_fs_info)->sbinfo;
	begin = sbinfo->data_bitmap_begin * sbinfo->block_size;
	end = begin + sbinfo->data_bitmap_block_num * sbinfo->block_size;
	block_num = find_valid_bit_num(bdev, begin, end);
	if (block_num) {		
		return block_num + sbinfo->data_begin;
	}
	return -ENOSPC;
}

int zramfs_get_valid_diretory(struct inode * inode, struct dentry *dentry)
{

	struct gza_inode *ginode = inode->i_private;
	struct block_device *bdev = inode->i_sb->s_bdev;
	int blk_blocksize = bdev->bd_block_size;
	int blk_blockbits = blksize_bits(blk_blocksize);
	int dev_block = 0;

	int cur_block = 0;
	int last_block = MAX_FILE_BLOCK_NUM;
	int file_block = 0;
	int block_bits = inode->i_blkbits;
	int num = 1 << (block_bits - blk_blockbits);
	struct buffer_head *bh;


	void * dty;
	void * cur;
	int dic_num = -1;
	int err = -ENOSPC;
	while (cur_block < last_block) {
		printk(KERN_NOTICE "zramfs_get_valid_directory, inode:%ld, file block index:%d, file block:%d\n", inode->i_ino, cur_block, ginode->data[cur_block]);
		if (ginode->data[cur_block]) {
			file_block = ginode->data[cur_block];
			dev_block = file_block << (block_bits - blk_blockbits);
			num = 1 << (block_bits - blk_blockbits);
			while (--num >= 0) {
				bh = __bread(bdev, dev_block, blk_blocksize);
				if (PageHighMem(bh->b_page)) {
					cur = kmap_atomic(bh->b_page, KM_USER0);
					cur +=  (int)bh->b_data;
				} else {
					cur = bh->b_data;
				}
				dty = cur;
				while(dty < cur + blk_blocksize &&
						((struct directory *)dty)->d_status) {
					dty += DIRECTORY_SIZE;
					dic_num++;
				}
				if (dty < cur + blk_blocksize ) {
					copy_dentry((struct directory*) dty, dentry);
					mark_buffer_dirty(bh);
					// dirty inode
					mark_inode_dirty(inode);
					put_bh(bh);
					return 0;	
				}
				put_bh(bh);
				dev_block++;
	
			}		

			cur_block++;
		} else {
			// alloc new block  
			file_block = zramfs_get_data_block(inode->i_sb);
			printk(KERN_NOTICE "zramfs_get_valid_directory, inode:%ld,  file block index:%d,  alloc file block:%d\n",inode->i_ino, cur_block, file_block);
			if (file_block) {
				//clear content
				dev_block = file_block << (block_bits - blk_blockbits);	
				num = 1 << (block_bits - blk_blockbits);
				while(--num >= 0) {
					clear_bdev_block_content(bdev, dev_block, blk_blocksize);
					dev_block++;
				}
				ginode->data[cur_block] = file_block;
				mark_inode_dirty(inode);
				continue;
			}
			return err;			
		}	
	}

	return err;
}

/**
 * find dir is empty or not
 * dentry must be a dentry, must be active dentry
 * 1:dir is empty
 * 0:dir is not empty
 */
static int zramfs_dir_empty(struct dentry * dentry) 
{
	struct inode *inode = dentry->d_inode;
	struct gza_inode *ginode = dentry->d_inode->i_private;
	struct block_device *bdev = inode->i_sb->s_bdev;
	int blk_blocksize = bdev->bd_block_size;
	int blk_blockbits = blksize_bits(blk_blocksize);
	int dev_block = 0;

	int cur_block = 0;
	int last_block = MAX_FILE_BLOCK_NUM;
	int file_block = 0;
	int block_bits = inode->i_blkbits;
	int num = 1 << (block_bits - blk_blockbits);
	struct buffer_head *bh;
	int ret = 1; //empty

	void * dty;
	void * cur;
	void * kmap_addr = 0;
	while (cur_block < last_block) {
		if (ginode->data[cur_block]) {
			file_block = ginode->data[cur_block];
			dev_block = file_block << (block_bits - blk_blockbits);
			num = 1 << (block_bits - blk_blockbits);
			while (--num >= 0) {
				bh = __bread(bdev, dev_block, blk_blocksize);
				kmap_addr = 0;
				if (PageHighMem(bh->b_page)) {
					kmap_addr = kmap_atomic(bh->b_page, KM_USER0);
					cur = kmap_addr + (int)bh->b_data;
				} else {
					cur = bh->b_data;
				}
				dty = cur;
				while(dty < cur + blk_blocksize &&
						!((struct directory *)dty)->d_status) {
					dty += DIRECTORY_SIZE;
				}
				put_bh(bh);
				dev_block++;
				if (kmap_addr) {
					kunmap_atomic(kmap_addr, KM_USER0);
				}
				if (dty < cur + blk_blocksize) {
					ret = 0;
					goto out;
				}

			}

			cur_block++;
		} else {
			break;
		}

	}
out:
	return ret;

}

/**
 * lookup a dentry
 */
void zramfs_find_diretory(struct inode * inode, struct dentry *dentry, struct buffer_head **bhp, struct directory **fentry)
{

	struct gza_inode *ginode = inode->i_private;
	struct block_device *bdev = inode->i_sb->s_bdev;
	int blk_blocksize = bdev->bd_block_size;
	int blk_blockbits = blksize_bits(blk_blocksize);
	int dev_block = 0;

	int cur_block = 0;
	int last_block = MAX_FILE_BLOCK_NUM;
	int file_block = 0;
	int block_bits = inode->i_blkbits;
	int num;
	struct buffer_head *bh;


	void * dty;
	void * cur;
	//TODO: last_block must is file size
	while (cur_block < last_block) {
		if (ginode->data[cur_block]) {
			file_block = ginode->data[cur_block];
			dev_block = file_block << (block_bits - blk_blockbits);
			num = 1 << (block_bits - blk_blockbits);
			while (--num >= 0) {
				bh = __bread(bdev, dev_block, blk_blocksize);
				if (PageHighMem(bh->b_page)) {
					cur = kmap_atomic(bh->b_page, KM_USER0);
					cur +=  (int)bh->b_data;
				} else {
					cur = bh->b_data;
				}
				dty = cur;
				while(dty < cur + blk_blocksize) {
					if (!((struct directory *)dty)->d_status) {
						dty += DIRECTORY_SIZE;
						continue;	
					}
					if (!memcmp(dentry->d_name.name, ((struct directory *)dty)->d_name, dentry->d_name.len)) {
						//find entry 
						printk(KERN_NOTICE "zramfs_find_directory, find entry name:%s", dentry->d_name.name);
						*bhp = bh;
						*fentry = dty;
						return;
					}
					dty += DIRECTORY_SIZE;
				}
				put_bh(bh);
				dev_block++;
	
			}		

			cur_block++;
		} else {
			break;
		}	
	}

}

static struct dentry *zramfs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct buffer_head* bh = NULL;
        struct directory *fentry = NULL;
	int inum;
	struct inode *inode;
	//int err;
       	zramfs_find_diretory(dir, dentry, &bh, &fentry);
	printk(KERN_NOTICE "zramfs_lookup, find dentry name:%s", dentry->d_name.name);
	if (!fentry)
		goto not_find;
	printk(KERN_NOTICE "zramfs_lookup, find bh:%p, fentry:%p", bh, fentry);
	inum = fentry->d_num;
	put_bh(bh);
	
	printk(KERN_NOTICE "zramfs_lookup, find parent inode:%ld", dir->i_ino);
	printk(KERN_NOTICE "zramfs_lookup, find entry name:%s", dentry->d_name.name);
	printk(KERN_NOTICE "zramfs_lookup, find entry:%p", fentry);
	printk(KERN_NOTICE "zramfs_lookup, find entry inode num:%d", fentry->d_num);
	//lookup from inode cache
	inode = ilookup(dir->i_sb, inum);
	if (inode)
		goto find;
	//lookup from disk 	
	inode = zramfs_get_inode_byid(dir->i_sb, fentry->d_num);
	printk(KERN_NOTICE "zramfs_lookup,  find inode byid inode:%p, inode->num:%ld", inode, inode->i_ino);
	if (inode)
		goto find;
not_find:
	// may add del operations, look simple_look TODO
	d_add(dentry, NULL);
	return NULL;
find:
	d_add(dentry, inode);
	return NULL;
}


/*
int zramfs_lookup_inode(struct inode * inode, struct dentry* dentry,  struct page** page, int *dic_n, int * inum)
{
	const char * name = dentry->d_name.name;
	int len = dentry->d_name.len;
	int last_block = MAX_FILE_BLOCK_NUM;
	int cur_block = 0;
	int block_bits = inode->i_blkbits;
	int cur_page;
	struct page *pg;
	void * kaddr;
	void * dty;
	int dic_num;
	int err = -ENOENT;
	while (cur_block < last_block) {
		cur_page = cur_block >> (PAGE_CACHE_SHIFT - block_bits);
		pg = find_lock_page(inode->i_mapping, cur_page);
		if (!pg) {
			err = -ENOSPC;
			goto no_pg_fail;
		}
		if (!PageUptodate(pg)) {
			err = inode->i_mapping->a_ops->readpage(NULL, pg);
			if (err)
				goto fail;
		}
		if (!PageUptodate(pg)) {
			err = lock_page_killable(pg);
			if (err)
				goto fail;
			if (!PageUptodate(pg)){
				err = -EIO;
				goto fail;
			}
		}
		
		kaddr = kmap_atomic(pg, KM_USER0);
		dty = kaddr;	
		dic_num = 0;
		while (dty - kaddr < PAGE_CACHE_SIZE) {
			if (((struct directory*)dty)->d_status && ((struct directory*)dty)->d_len == len && strncmp(((struct directory*)dty)->d_name, name, len)){
				*page = pg;
				*dic_n = dic_num;
				*inum = ((struct directory*)dty)->d_num;
				break;
			}

			dty += DIRECTORY_SIZE;
			dic_num++;
		}

		kunmap_atomic(kaddr, KM_USER0);
		if(dty - kaddr < PAGE_CACHE_SIZE) {
			return 0;
		}
		while (++cur_block >> (PAGE_CACHE_SHIFT - block_bits) > cur_page);
	}
fail:
	unlock_page(pg);
	page_cache_release(pg);
no_pg_fail:
	return err;
}

static struct dentry *zramfs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct page* page;
	int dir_num;
	int inum;
	struct inode *inode;
	int err;
       	err = zramfs_lookup_inode(dir, dentry, &page, &dir_num, &inum);
	if (err)
		return NULL;
	
	inode = zramfs_get_inode_byid(dir->i_sb, inum);
	if (!inode)
		return NULL;
	d_add(dentry, inode);
	return dentry;
}
*/

int zramfs_mkdir(struct inode* dir, struct dentry * dentry, int mode)
{
	int err = 0;

	err = zramfs_mknod(dir, dentry, mode|S_IFDIR, 0);
	if (err)
		return err;

	err =  zramfs_get_valid_diretory(dir, dentry);
	if (err)
		return err;
	
	if (!err) {
		inc_nlink(dir);
	}	
	return err;
}

int zramfs_create(struct inode *dir, struct dentry * dentry, int mode, struct nameidata *nd)
{
	int err = 0;
	
	err = zramfs_mknod(dir, dentry, mode|S_IFREG, 0);
	if (err)
		return err;
	printk("zramfs_create, zramfs_mknod:%d, dentry->d_inode->i_sb->s_op:%p\n", err, dentry->d_inode->i_sb->s_op);
	
	err =  zramfs_get_valid_diretory(dir, dentry);
	printk("zramfs_create, zramfs_get_valid_directory, return status: %d\n", err);
	if (err)
		return err;

	
	return err;

}

static int zramfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = zramfs_get_inode(dir->i_sb, S_IFLNK|S_IRWXUGO, 0);
	if (!inode) {
		printk(KERN_NOTICE"zramfs_symlink, zramfs_get_inode can't get inode, err code:%d", error);
		return error;
	}

	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			if (dir->i_mode & S_ISGID)
				inode->i_gid = dir->i_gid;
			d_instantiate(dentry, inode);
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
			error = zramfs_get_valid_diretory(dir, dentry);
			if (error) {
				printk(KERN_NOTICE"zramfs_symlink, zramfs_get_valid_diretory error,error code:%d", error);
				//dput(dentry);
				return error;
			}
		} else {
			printk(KERN_NOTICE"zramfs_symlink, zramfs_get_inode error,error code:%d", error);
			iput(inode);
		}
	}
	return error;
}

void zramfs_delete_inode(struct inode * inode)
{	
	//del from filesystem, trancate the file mapping
	struct gza_inode * ginode = (struct gza_inode*)inode->i_private;
	gzafs_sb_info *sbinfo = &((struct ramfs_fs_info *)inode->i_sb->s_fs_info)->sbinfo;
	int num= 0;
	int index=0;
	int bitoffset = 0;
	u32 offset = 0;
	int i = 0;
	//truncate page cache
	truncate_inode_pages(&inode->i_data,0);
	clear_inode(inode);
	if (!ginode)
		return;
	//trucate data
	for (;i<INODE_DATA_COUNT;i++)
	{
		if (ginode->data[i] == 0)
			continue;
		num = ginode->data[i];
		index = num >> 3;
		bitoffset = num & 0x07;
		offset = (int)(sbinfo->data_bitmap_begin * sbinfo->block_size + index);
		set_dev_bit(inode->i_sb->s_bdev, offset, bitoffset, UNSET);
	
 		printk(KERN_NOTICE "*** zramfs_delete_inode clear data block num:%d\n", num);	
	}
	//clean inode bitmap
	index = ginode->num >> 3;
	offset = sbinfo->inode_bitmap_begin * sbinfo->block_size + index;
	bitoffset = ginode->num & 0x07;
	set_dev_bit(inode->i_sb->s_bdev, offset, bitoffset, UNSET);
		
 	printk(KERN_NOTICE "*** zramfs_delete_inode:%d, offset:%d, bitoffset:%d\n", ginode->num, offset, bitoffset);	
	
	kfree(ginode);

}

int  write_inode(struct super_block *sb, struct inode * inode, struct buffer_head** tbh)
{
	gzafs_sb_info * sbinfo = &((struct ramfs_fs_info*)sb->s_fs_info)->sbinfo;
	struct buffer_head *bh;
	int ino = inode->i_ino;
	loff_t index = sbinfo->inode_begin * sbinfo->block_size + ino * INODE_SIZE;
	loff_t begin_block = index >> sb->s_bdev->bd_inode->i_blkbits;
	int offset = index & ((1<<sb->s_bdev->bd_inode->i_blkbits) - 1);
	char* cur= NULL;
	struct gza_inode *ginode;
	struct gza_inode *buf_ginode = (struct gza_inode *)inode->i_private;
	bh = __bread(sb->s_bdev, begin_block, sb->s_bdev->bd_block_size);
	if (!bh)
		return -EIO;

	if (PageHighMem(bh->b_page)) {
		cur = kmap_atomic(bh->b_page, KM_USER0);
		cur +=  (int)bh->b_data;
		cur += offset;	
	} else {
		cur = bh->b_data;
		cur = bh->b_data + offset;
	}
	ginode = (struct gza_inode*) cur;
	ginode->num = inode->i_ino;
	ginode->mode = inode->i_mode;	
	ginode->length = inode->i_size;
	ginode->dev = inode->i_rdev;
	memcpy(ginode->data, buf_ginode->data, sizeof(ginode->data));
 	printk(KERN_NOTICE "*** write inode num:%d, mode:%o\n", ginode->num,ginode->mode);	
	if (PageHighMem(bh->b_page))
		kunmap_atomic(bh->b_page, KM_USER0);
	*tbh = bh;
	return 0;
}

int zramfs_write_inode(struct inode * inode, int do_sync)
{
	struct buffer_head *bh;
	struct super_block *sb = inode->i_sb;
	int err =  write_inode(sb, inode, &bh);
	printk(KERN_NOTICE "inode->i_sb->s_bdev->bd_disk->queue:%p", inode->i_sb->s_bdev->bd_disk->queue );
	if (err)
		return err;
	mark_buffer_dirty(bh);
	if (do_sync) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk("IO error syncing ginode [%s:%08lx\n]", sb->s_id,(unsigned long) inode->i_ino);
			err = -EIO;
		}
	}
	brelse(bh);
	return err;
}

struct dentry *zramfs_simple_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	return NULL;	
}

static int permission(struct inode* inode, int flag){
	printk(KERN_NOTICE "this is permission.is mmutable:%d, mask:%o", IS_IMMUTABLE(inode), flag);
	return 0;
}

inline unsigned char dt_type(struct inode *inode)
{
		return (inode->i_mode >> 12) & 15;
}

static int zramfs_readdir(struct file * filp, void * dirent, filldir_t filldir) {
	struct dentry *dentry = filp->f_path.dentry;
	ino_t ino;
	int i = filp->f_pos;
	int index = 0;
	int offset = 0;
	int cur_block = 0;
	int max_block = MAX_FILE_BLOCK_NUM;
	struct inode * inode = filp->f_dentry->d_inode;
	int file_block;
	int dev_block;
	//int dev_blk_num;

	if (!inode) {
		return 0;
	}
	printk(KERN_NOTICE "zramfs_readdir, filp->f_pos:%lld", filp->f_pos);
	switch(i) {
		case 0:
			ino = dentry->d_inode->i_ino;
			if (filldir(dirent, ".", 1, i, ino, DT_DIR) < 0)
				break;
			filp->f_pos++;
			i++;
		case 1:
			ino = parent_ino(dentry);
			if (filldir(dirent, "..", 2, i, ino, DT_DIR) < 0)
				break;
			filp->f_pos++;
			i++;
		default:
			{
			int fs_blocksize = inode->i_sb->s_blocksize;
			struct gza_inode *ginode = inode->i_private;
			int block_bits = inode->i_blkbits;
			struct buffer_head *bh;
			struct block_device *bdev = inode->i_sb->s_bdev;
			int dev_block_size = bdev->bd_block_size;
			int dev_block_bits = blksize_bits(dev_block_size);
			int dev_blk_num = 1 << (block_bits - dev_block_bits);
			int num = 0;
			int cur_dev_block_offset = 0;
			void * cur;
			void * dty;
			struct directory *tmp_dicp;
			index = i-2;
			cur_block = sizeof(struct directory) * index / fs_blocksize;
			offset  = sizeof(struct directory) * index % fs_blocksize;
			cur_dev_block_offset = offset / dev_block_size;
			offset = offset % dev_block_size;
			while (cur_block < max_block) {
				file_block = ginode->data[cur_block];
				if (file_block) {
					dev_block = file_block << (block_bits - dev_block_bits);
					dev_block += cur_dev_block_offset;
					num = dev_blk_num - cur_dev_block_offset;
					while (--num >= 0) {
						bh = __bread(bdev, dev_block, dev_block_size);
						if (PageHighMem(bh->b_page)) {
							cur = kmap_atomic(bh->b_page, KM_USER0);
							cur +=  (int)bh->b_data;
						} else {
							cur = bh->b_data;
						}
						
						dty = cur +  offset;
						while (dty < cur + dev_block_size) {
							
							if (!((struct directory *)dty)->d_status) {
								dty += DIRECTORY_SIZE;
								filp->f_pos++;
								continue;	
							}
							tmp_dicp = ((struct directory *) dty);
							if (filldir(dirent, tmp_dicp->d_name, 
								tmp_dicp->d_len, 
								filp->f_pos, 
								inode->i_ino, 
								dt_type(inode)) < 0)
								return 0;
							printk(KERN_NOTICE "zramfs_readdir, filp->f_pos:%lld, name:%s, inode:%ld", filp->f_pos, tmp_dicp->d_name, inode->i_ino);
							filp->f_pos++;
							dty += DIRECTORY_SIZE;
						}
						put_bh(bh);
					}	
					cur_dev_block_offset = 0;
					offset = 0;
					//dic has no hole
					cur_block++;
				} else {
					break;
				}
			}
			} // for static code block
	}
	return 0;
}

static int zramfs_unlink (struct inode *dir,struct dentry * dentry) {

	struct inode *inode = dentry->d_inode;
	struct buffer_head* bh = NULL;
        struct directory *fentry = NULL;

	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	drop_nlink(inode);

	//clear parent dentry
       	zramfs_find_diretory(dir, dentry, &bh, &fentry); //debug
	fentry->d_status = 0;
	put_bh(bh);	
	return 0;
}

static int zramfs_rmdir(struct inode* dir, struct dentry *dentry){
	
	if (!dentry->d_inode) {
		return 0;
	}
	//dentry is empty?
	if (!zramfs_dir_empty(dentry)) {
		return -ENOTEMPTY;
	}
 
	drop_nlink(dentry->d_inode);
	zramfs_unlink(dir, dentry);
	drop_nlink(dir);	
   	return 0;		
}

int zramfs_rename(struct inode *old_dir, struct dentry *old_dentry,
				struct inode *new_dir, struct dentry *new_dentry)
{
	struct buffer_head* bh = NULL;
        struct directory *fentry = NULL;
	int err = 0;
	if (new_dentry->d_inode) {
		drop_nlink(new_dentry->d_inode);
		// change the inode
       		zramfs_find_diretory(new_dir, new_dentry, &bh, &fentry);
	       	if (!fentry) {
			printk(KERN_ERR"zramfs_rename, not find the new_dentry");
			return -EIO;
		}	
		fentry->d_num = old_dentry->d_inode->i_ino;
		put_bh(bh);	
	} else {
		// create the dentry
		new_dentry->d_inode = old_dentry->d_inode;
		err =  zramfs_get_valid_diretory(new_dir, new_dentry);
		new_dentry->d_inode = NULL;
		if (err)
			return err;
	}
	//del the old dentry	
       	zramfs_find_diretory(old_dir, old_dentry, &bh, &fentry); 
	if (!fentry) {
		printk(KERN_ERR"zramfs_rename, not find the old_dentry");
		return -EIO;
	}	
	fentry->d_status = 0;
	put_bh(bh);	
	return err;
}

static const struct inode_operations ramfs_dir_inode_operations = {
	//.create		= ramfs_create,
	.create		= zramfs_create,
	//.lookup		= simple_lookup,
	.lookup		= zramfs_lookup,
	.link		= simple_link,
	//.unlink		= simple_unlink,
	.unlink		= zramfs_unlink,
	//.symlink	= ramfs_symlink
	.symlink	= zramfs_symlink,
	//.mkdir	= ramfs_mkdir,
	.mkdir		= zramfs_mkdir,
	//.rmdir		= simple_rmdir,
	.rmdir		= zramfs_rmdir,
	//.mknod		= ramfs_mknod,
	.mknod		= zramfs_mknod,
	//.rename		= simple_rename,
	.rename		= zramfs_rename,
	.permission     = permission,
};

static const struct file_operations zramfs_dir_operations = {
	//.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.llseek		= dcache_dir_lseek,
	.read		= generic_read_dir,
	//.readdir	= dcache_readdir,
	.readdir	= zramfs_readdir,
	.fsync		= simple_sync_file,
};

static const struct super_operations ramfs_ops = {
	.statfs		= simple_statfs,
	//.drop_inode	= generic_delete_inode,
	//.alloc_inode   = zramfs_alloc_inode,
	.write_inode     = zramfs_write_inode,
	.delete_inode   = zramfs_delete_inode,
	.show_options	= generic_show_options,
};

static int ramfs_parse_options(char *data, struct ramfs_mount_opts *opts)
{
	substring_t args[MAX_OPT_ARGS];
	int option;
	int token;
	char *p;

	opts->mode = RAMFS_DEFAULT_MODE;

	while ((p = strsep(&data, ",")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_mode:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->mode = option & S_IALLUGO;
			break;
		/*
		 * We might like to report bad mount options here;
		 * but traditionally ramfs has ignored all mount options,
		 * and as it is used as a !CONFIG_SHMEM simple substitute
		 * for tmpfs, better continue to ignore other mount options.
		 */
		}
	}

	return 0;
}

static int ramfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct ramfs_fs_info *fsi;
	struct inode *inode = NULL;
	struct dentry *root;
	int err;

	printk(KERN_ERR "fill super block*****\n");
	save_mount_options(sb, data);

	fsi = kzalloc(sizeof(struct ramfs_fs_info), GFP_KERNEL);
	sb->s_fs_info = fsi;
	if (!fsi) {
		err = -ENOMEM;
		goto fail;
	}

	err = ramfs_parse_options(data, &fsi->mount_opts);
	if (err)
		goto fail;

	printk("**read super block\n");
	get_dev_content(sb->s_bdev, (loff_t)0, (char*)&fsi->sbinfo, sizeof(fsi->sbinfo));
	printk("**read super block end\n");
	if (fsi->sbinfo.magic != FS_MAGIC)
		goto fail;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= fsi->sbinfo.block_size;
	sb->s_blocksize_bits	= blksize_bits(sb->s_blocksize);
	sb->s_magic		= FS_MAGIC;
	sb->s_op		= &ramfs_ops;
	sb->s_time_gran		= 1;

	printk("**read root inode\n");
	//inode = ramfs_get_inode(sb, S_IFDIR | fsi->mount_opts.mode, 0);
	inode = zramfs_get_inode_byid(sb, ROOT_INODE_NUM);

	if (!inode) {
		err = -ENOMEM;
		goto fail;
	}

	printk("**init root dentry\n");
	root = d_alloc_root(inode);
	printk("**init root dentry end root isdir:%d\n", S_ISDIR(inode->i_mode));
	sb->s_root = root;


	if (!root) {
		err = -ENOMEM;
		goto fail;
	}

	printk("** block device:%p, queue:%p.\n", sb->s_bdev, bdev_get_queue(sb->s_bdev));
	printk("** super_block->s_list.next:%p, super_block->s_list->pre:%p.\n", sb->s_list.next, sb->s_list.prev);
	printk("** inode->i_mapping->backing_dev_info:%p, sb->s_bdev->bd_inode->i_mapggin->backing_dev_info:%p.\n", inode->i_mapping->backing_dev_info, sb->s_bdev->bd_inode->i_mapping->backing_dev_info);
	printk("** fill super block sucess.\n");
	return 0;
fail:
	kfree(fsi);
	sb->s_fs_info = NULL;
	iput(inode);
	return err;
}

int ramfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	printk("****begin get super block.\n");
	return get_sb_bdev(fs_type, flags, dev_name, data, ramfs_fill_super, mnt);
}

static void ramfs_kill_sb(struct super_block *sb)
{
	kfree(sb->s_fs_info);
	kill_litter_super(sb);
}

static void zramfs_kill_sb(struct super_block *sb)
{
	//dump_stack();	
	struct inode *inode = NULL;
	struct bdi_writeback *wb; 
	printk(KERN_ERR "** begin zramfs_kill_sb, super_block->s_list.next:%p, super_block->s_list->pre:%p. sb->s_op:%p\n", 
			sb->s_list.next, sb->s_list.prev, sb->s_op);
	// print dirty inodes
	inode = sb->s_root->d_inode;
 	wb = &inode->i_mapping->backing_dev_info->wb;
	list_for_each_entry(inode, &wb->b_dirty, i_list) {
		printk(KERN_ERR "zramfs_kill_sb, dirty inode num:%ld", inode->i_ino);
	}	
	kill_block_super(sb);
	kfree(sb->s_fs_info);
	printk(KERN_ERR "** end zramfs_kill_sb, super_block->s_list.next:%p, super_block->s_list->pre:%p. sb->s_op:%p\n", 
			sb->s_list.next, sb->s_list.prev, sb->s_op);
}

static struct file_system_type ramfs_fs_type = {
	.name		= "zramfs",
	.get_sb		= ramfs_get_sb,
	.kill_sb	= zramfs_kill_sb, //TODO:chang
	//.kill_sb	= ramfs_kill_sb,
};

static int __init init_ramfs_fs(void)
{
	int err = 0;
	//err = bdi_init(&ramfs_backing_dev_info);
	if (err) {
		printk(KERN_ERR "init ramfs_backing_dev_info err res:%d", err);
		return err;
	}
	return register_filesystem(&ramfs_fs_type);
}

static void __exit exit_ramfs_fs(void)
{
	unregister_filesystem(&ramfs_fs_type);
}

module_init(init_ramfs_fs)
module_exit(exit_ramfs_fs)


MODULE_LICENSE("GPL");
