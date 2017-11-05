
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include "internal.h"

 int get_dev_content(struct block_device *bdev, loff_t offset, char * buff, int size)
{
	int block_size = bdev->bd_block_size;
	int block_bits = bdev->bd_inode->i_blkbits;
	int begin_block = offset >> block_bits;
	int end_block = (offset + size) >> block_bits;
	int off = offset & ((1<<block_bits) - 1);
	struct buffer_head *bh; 
	char *cur = NULL;
	int count;
	int left = size;
	int i = begin_block - 1;
	while (++i <= end_block) {
		bh = __bread(bdev, i, block_size);
		count = block_size;
		if (i == end_block)
			count = left;
		if (i == begin_block) 
			count -= off;
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
		memcpy(buff, cur, count);
		buff += count;
		left -= count;
		if (PageHighMem(bh->b_page))
			kunmap_atomic(bh->b_page, KM_USER0);
		put_bh(bh);
	}	
	return 0;
}

 void set_dev_content(struct block_device *bdev, loff_t offset, char * buff, int size)
{
	int block_size = bdev->bd_block_size;
	int block_bits = bdev->bd_inode->i_blkbits;
	int begin_block = offset >> block_bits;
	int end_block = (offset + size) >> block_bits;
	int off = offset & ((1<<block_bits) - 1);
	struct buffer_head *bh; 
	char *cur = NULL;
	int count;
	int left = size;
	int i = begin_block - 1;
	while (++i <= end_block) {
		bh = __bread(bdev, i, block_size);
		count = block_size;
		if (i == end_block)
			count = left;
		if (i == begin_block)
			count -= off;
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
		memcpy(cur, buff, count);
		buff += count;
		left -= count;
		if (PageHighMem(bh->b_page))
			kunmap_atomic(bh->b_page, KM_USER0);
		mark_buffer_dirty(bh);
		put_bh(bh);
	}
}

 void set_dev_bit(struct block_device *bdev, loff_t offset, int bitoffset, enum SET_FLAG	flag )
{
	int block_size = bdev->bd_block_size;
	int block_bits = bdev->bd_inode->i_blkbits;
	int begin_block = offset >> block_bits;
	int off = offset & ((1<<block_bits) - 1);
	struct buffer_head *bh; 
	char *cur = NULL;
	bh = __bread(bdev, begin_block, block_size);
	if (PageHighMem(bh->b_page)) {
		cur = kmap_atomic(bh->b_page, KM_USER0);
		cur +=  (int)bh->b_data;
		cur += off;	
	} else {
		cur = bh->b_data;
		cur = bh->b_data + off;
	}
	if (flag == SET) {
		*cur |= (1<<bitoffset);
	} else {
		*cur &= ~(1<<bitoffset);
	}
	if (PageHighMem(bh->b_page))
			kunmap_atomic(bh->b_page, KM_USER0);
	mark_buffer_dirty(bh);
	put_bh(bh);
}


int find_valid_bit_num(struct block_device *bdev, loff_t begin, loff_t end)
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
	int index = 0;
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
			index++;
		}
		if (PageHighMem(bh->b_page))
			kunmap_atomic(bh->b_page, KM_USER0);
		put_bh(bh);
		continue;
end:
		if (PageHighMem(bh->b_page))
			kunmap_atomic(bh->b_page, KM_USER0);
		mark_buffer_dirty(bh);
		put_bh(bh);
		return index * 8 +bit;
	}
	return 0;
}


