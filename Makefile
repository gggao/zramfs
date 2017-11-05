ifneq ($(KERNELRELEASE),)
	#file-mmu-y := file-nommu.o
	#file-mmu-y := file-mmu.o
	#EXTRA_CFLAGS := $(EXTRA_CFLAGS) --verbose
	obj-m := gzafs.o
	gzafs-objs = inode.o file-mmu.o blkoper.o
else
	PWD := $(shell pwd)
	KERNELDIR ?=/lib/modules/$(shell uname -r)/build
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
.PHONY: clean
clean:
	find -name ".*.*" -exec rm {} \;
	rm *.o
endif
