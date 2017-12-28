# zramfs

zramfs is disk file system just for fun. then it have bugs. but to learn the kernel and vfs is good. Change from ramfs

I have a blockdev project https://github.com/gggao/sbull.git, for easy the block dev logic block size is 1k. and the zramfs make fs block size is 1k. But they can't must be the same, but if they don't equal may be occur some problem, some places in the code don't deal this and almont can deal this. I don't try this.

fs structure is very easy, see format.c gzafs_sb_info;

-----------------------------------
the first fs block is super block.|
-----------------------------------
inode bitmap			  |
-----------------------------------
data bitmap 			  |
-----------------------------------
inode data			  |
-----------------------------------
data				  |
-----------------------------------


steps:
1.load the blockdev sbull, ./sbull_init.sh load
2.compile the format program [format.c], then format the bdev: ./a.out /dev/sbull0
3.compile zramfs by command make. then load zramfs by ./load.sh load, It do insmod and mount to the dir ramfs;

