/* file-mmu.c: ramfs MMU-based file operations
 *
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

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/ramfs.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/highmem.h>
#include <linux/mpage.h>

#include "internal.h"

//no make addressspace dirt wrong
inline int  __set_page_dirty_no_writeback(struct page * page)
{
	if (!PageDirty(page))
		SetPageDirty(page);
	return 0;
	
}

int gfs_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh, int create) 
{
	struct gza_inode *info = (struct gza_inode*)inode->i_private;
	gzafs_sb_info *sbinfo = &((struct ramfs_fs_info *)inode->i_sb->s_fs_info)->sbinfo;
	//int fs_blocksize_bits = inode->i_sb->s_blocksize_bits;
	struct block_device *bdev = inode->i_sb->s_bdev;
	int begin, end, new_num;
	printk(KERN_NOTICE "gfs_get_block, inode->num:%ld,file block:%lld, create:%d\n",inode->i_ino, iblock, create);
	if (iblock > MAX_FILE_BLOCK_NUM)
		return -ENOSPC;
	
	if (info->data[iblock] != 0)
	{
		//bh->b_blocknr = info->data[iblock];
		//bh->b_bdev = bdev;
		//set_buffer_mapped(bh);
		//clear_buffer_new(bh);
		
		clear_buffer_new(bh);
		map_bh(bh, inode->i_sb, info->data[iblock]);
		return 0;
	}
	//not creat
	if (!create) 
	{
		clear_buffer_mapped(bh);
		return 0;
	}
	//alloc a new data bloc
	begin = sbinfo->data_bitmap_begin << inode->i_blkbits;
        end = (sbinfo->data_bitmap_begin + sbinfo->data_bitmap_block_num) << inode->i_blkbits;
	//begin = sbinfo->data_bitmap_begin << fs_blocksize_bits;
        //end = (sbinfo->data_bitmap_begin + sbinfo->data_bitmap_block_num) << fs_blocksize_bits;
	new_num = find_valid_bit_num(bdev, begin, end);
	printk(KERN_NOTICE "**gfs_get_block, find_valid_bit_num, begin:%d, end:%d, result:%d\n", begin, end,  new_num);
	if (! new_num)
		return -ENOSPC;

	//update inode
	info->data[iblock] = new_num;
	mark_inode_dirty(inode);
	

	printk(KERN_NOTICE "gfs_get_block, file block:%lld, fs block:%d\n", iblock, new_num);
	//bh->b_blocknr = new_num;
	//bh->b_bdev = bdev;
	//set_buffer_mapped(bh);
	set_buffer_new(bh);
	map_bh(bh, inode->i_sb, new_num);	
	return 0;
}


/*
//block_prepare_write for help
static int __block_prepare_write(struct inode* inode, struct page* page, unsigned from, unsigned to, get_block_t *get_block) 
{
	int block_start, block_end;
	int blocksize = 1 << inode->i_blkbits;
	struct buffer_head *head, *bh, *wait[4], **wait_bh = wait;	
	sector_t block;
	int bbits = inode->i_blkbits;
	int err = 0;

	block = (sector_t)page->index << (PAGE_CACHE_SHIFT - bbits);
	
	if (!page_has_buffers(page))
		create_empty_buffers(page, blocksize,0);
	head = page_buffers(page);
	for (bh = head, block_start=0; bh!=head||!block_start; 
		block++,bh=bh->b_this_page,block_start=block_end) 
	{
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (PageUptodate(page)) {
				if (buffer_uptodate(bh))
					set_buffer_uptodate(bh);
			}
			continue;
		}
		if (buffer_new(bh))
			clear_buffer_new(bh);
		if (!buffer_mapped(bh)){
			WARN_ON(bh->b_size != blocksize);
			err = gfs_get_block(inode, block, bh, 1);
			if (err)
				break;
			if (buffer_new(bh))
			{
				if (PageUptodate(page)) {
					clear_buffer_new(bh);
					set_buffer_uptodate(bh);
					mark_buffer_dirty(bh);
					continue;
				}
				if (block_end > to || block_start < from) {
					zero_user_segments(page,
							to, block_end,
							block_start, from);
				}
				continue;
			}
		}
		if (PageUptodate(page)) {
			if (!buffer_uptodate(bh))
				set_buffer_uptodate(bh);
			continue;
		}
		if (!buffer_uptodate(bh) && !buffer_delay(bh) &&
				!buffer_unwritten(bh) &&
				(block_start < from || block_end > to)) {
			ll_rw_block(READ, 1, &bh);
			*wait_bh++ = bh;
		}
	}
	while (wait_bh != wait) {
		wait_on_buffer(*--wait_bh);
		if (!buffer_uptodate(bh))
			err = -EIO;
	}
	if (unlikely(err))
		page_zero_new_buffers(page, from, to);
	return err;
}
*/

/**
fs/buffer.c
*/
static int __block_prepare_write(struct inode *inode, struct page *page,
		unsigned from, unsigned to, get_block_t *get_block)
{
	unsigned block_start, block_end;
	sector_t block;
	int err = 0;
	unsigned blocksize, bbits;
	struct buffer_head *bh, *head, *wait[4], **wait_bh=wait;

	BUG_ON(!PageLocked(page));
	BUG_ON(from > PAGE_CACHE_SIZE);
	BUG_ON(to > PAGE_CACHE_SIZE);
	BUG_ON(from > to);

	blocksize = 1 << inode->i_blkbits;
	if (!page_has_buffers(page))
		create_empty_buffers(page, blocksize, 0);
	head = page_buffers(page);

	bbits = inode->i_blkbits;
	block = (sector_t)page->index << (PAGE_CACHE_SHIFT - bbits);

	for(bh = head, block_start = 0; bh != head || !block_start;
	    block++, block_start=block_end, bh = bh->b_this_page) {
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (PageUptodate(page)) {
				if (!buffer_uptodate(bh))
					set_buffer_uptodate(bh);
			}
			continue;
		}
		if (buffer_new(bh))
			clear_buffer_new(bh);
		if (!buffer_mapped(bh)) {
			WARN_ON(bh->b_size != blocksize);
			err = get_block(inode, block, bh, 1);
			if (err)
				break;
			if (buffer_new(bh)) {
				unmap_underlying_metadata(bh->b_bdev,
							bh->b_blocknr);
				if (PageUptodate(page)) {
					clear_buffer_new(bh);
					set_buffer_uptodate(bh);
					mark_buffer_dirty(bh);
					continue;
				}
				if (block_end > to || block_start < from)
					zero_user_segments(page,
						to, block_end,
						block_start, from);
				continue;
			}
		}
		if (PageUptodate(page)) {
			if (!buffer_uptodate(bh))
				set_buffer_uptodate(bh);
			continue; 
		}
		if (!buffer_uptodate(bh) && !buffer_delay(bh) &&
		    !buffer_unwritten(bh) &&
		     (block_start < from || block_end > to)) {
			ll_rw_block(READ, 1, &bh);
			*wait_bh++=bh;
		}
	}
	/*
	 * If we issued read requests - let them complete.
	 */
	while(wait_bh > wait) {
		wait_on_buffer(*--wait_bh);
		if (!buffer_uptodate(*wait_bh))
			err = -EIO;
	}
	if (unlikely(err))
		page_zero_new_buffers(page, from, to);
	return err;
}
/*
int zramfs_write_begin(struct file* file, struct address_space *mapping, loff_t pos, unsigned len, unsigned flags, struct page **page, void ** fsdata) 
{
	int index;
	int from;
	struct page *pg;
	int err=0;
	
	if (pos > MAX_GZA_FILESIZE || pos+len > MAX_GZA_FILESIZE)
		return -ENOSPC;

	index = pos >> PAGE_CACHE_SHIFT;
	from = pos & (PAGE_CACHE_SIZE -1);
	pg = grab_cache_page_write_begin(mapping, index, flags);
	if (!pg)
		return -ENOMEM;
 	err = __block_prepare_write(mapping->host, pg, from, from+len, gfs_get_block );
	*page = pg;
	return err;
}
*/


int zramfs_write_begin(struct file* file, struct address_space *mapping, loff_t pos, unsigned len, unsigned flags, struct page **page, void ** fsdata) 
{
	*page = NULL;
	return block_write_begin(file, mapping, pos, len, flags,page ,fsdata,gfs_get_block);
}

int zramfs_generic_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	int err;
	struct inode *inode = file->f_dentry->d_inode;
	printk(KERN_NOTICE "zramfs_write_end begin, page dirty:%d, inode:%ld, dirty:%ld", PageDirty(page), inode->i_ino, inode->i_state & I_DIRTY);
	err = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
	printk(KERN_NOTICE "zramfs_write_end end, page dirty:%d, inode:%ld, drity:%ld", PageDirty(page), inode->i_ino, inode->i_state & I_DIRTY );//bug page may be invalid
	return err;
}

int zramfs_read_page(struct file* file, struct page* page)
{
	return mpage_readpage(page, gfs_get_block);
}

int zramfs_write_page(struct page *page, struct writeback_control *wbc) 
{
	printk(KERN_NOTICE "zramfs_write_page ****************");
	return block_write_full_page(page, gfs_get_block, wbc);
}

const struct address_space_operations ramfs_aops = {
	//.readpage	= simple_readpage,
	.readpage	= zramfs_read_page,
	.writepage      = zramfs_write_page,
	//.write_begin	= simple_write_begin,
	.write_begin	= zramfs_write_begin,
	//.write_end	= simple_write_end,
	.write_end	= zramfs_generic_write_end,
	//.set_page_dirty = __set_page_dirty_no_writeback,
};

const struct file_operations ramfs_file_operations = {
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.fsync		= simple_sync_file,
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
	.llseek		= generic_file_llseek,
};

const struct inode_operations ramfs_file_inode_operations = {
	.getattr	= simple_getattr,
};


struct file_system_type fst;
