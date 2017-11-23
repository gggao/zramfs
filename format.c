#include<stdio.h>
#include<unistd.h>
#include<fcntl.h>
#include<errno.h>
#include<memory.h>
#include<linux/if_packet.h>

typedef struct
{
	__u32 inode_bitmap_begin;
	__u32 inode_bitmap_block_num;
	__u32 inode_num;
	__u32 data_bitmap_begin;
	__u32 data_bitmap_block_num;
	__u32 data_num;
	__u32 inode_begin;
	__u32 inode_block_num;
	__u32 data_begin;
	__u32 data_block_num;
	__u32 block_size;
	
	__u32 magic;

} __attribute__ ((packed)) gzafs_sb_info;

#define BLOCK_SIZE 1024
struct gza_inode 
{
	__u32 num;
	umode_t mode;
	int length;
	dev_t dev;
	unsigned int data[10];
};
#define INODE_SIZE 64
#define ROOT_INODE_NUM 1
#define RESERVE_INODE_NUM 0

int main(int argc, char* argv[])
{
	int res;
	int cur = 0;
	int count = 0;
	int data = 0;
	int i;
	gzafs_sb_info sb;
	struct gza_inode ginode;
	
	ginode.num = 1;
	ginode.mode = 00777 | 0040000;
	ginode.dev = 0;
	ginode.length = 0;
	memset(ginode.data,0,sizeof(ginode.data));

	if (argc < 2) 
	{
		printf("param is error!");
		printf("error: %s\n", strerror(errno));
		return 1;
	}

	int fp = open(argv[1], O_RDWR);
	if (fp <0) 
	{
		printf("open failed.");
		return;
	}
	
	sb.inode_bitmap_begin = 1;
	sb.inode_bitmap_block_num = 1;
	sb.data_bitmap_begin = 2;
	sb.data_bitmap_block_num = 1;
	sb.inode_begin = 3;
	sb.inode_block_num = 3;
	sb.data_begin = sb.inode_begin + sb.inode_block_num;
       	sb.data_block_num = 100;
	sb.inode_num = (BLOCK_SIZE/32)*sb.inode_block_num;
	sb.data_num = sb.data_block_num;
	sb.block_size = BLOCK_SIZE;
	
	sb.magic = 0x12341234;
	
	res = lseek(fp, 0, SEEK_CUR);
	if (res == -1)
	{
		printf("seek error");
		return;
	}
	
	res = write(fp, &sb, sizeof(sb));
	if (res == -1)
	{
		printf("write error");
		return;
	}
	cur =  lseek(fp, sb.inode_bitmap_begin * BLOCK_SIZE, SEEK_SET);

	if (res == -1)	
	{
		printf("seek error");
		return;
	}
	count = (sb.inode_bitmap_block_num + sb.data_bitmap_block_num)*BLOCK_SIZE/sizeof(data);
	printf("format, begin:%d, num:%d",cur, count* sizeof(data));
	for (i = 0;i< count;i++) 
	{
		write(fp, &data, sizeof(data));
	}
	//init 0 data bit, reserve the first bit
	cur =  lseek(fp, sb.data_bitmap_begin * BLOCK_SIZE, SEEK_SET);
	if (res == -1)	
	{
		printf("seek error");
		return;
	}
	
	data = 1;
	write(fp, &data, sizeof(data));
	
	
	//init root directory
	cur =  lseek(fp, sb.inode_bitmap_begin * BLOCK_SIZE, SEEK_SET);
	if (res == -1)	
	{
		printf("seek error");
		return;
	}
	
	data = 1 << ROOT_INODE_NUM;
	data |= 1 << RESERVE_INODE_NUM; 
	write(fp, &data, sizeof(data));
	
	cur =  lseek(fp, (sb.inode_begin * BLOCK_SIZE) + INODE_SIZE * ROOT_INODE_NUM, SEEK_SET);
	if (res == -1)	
	{
		printf("seek error");
		return;
	}
	
	write(fp, &ginode, sizeof(struct gza_inode));
}	
