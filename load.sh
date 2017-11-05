#!/bin/sh 

function unload()
{
	sudo umount ramfs
	sudo rmmod gzafs
	
}
function load()
{
	sudo insmod gzafs.ko
	sudo mount -t zramfs /dev/sbulla  ramfs
}

case $1 in
	load)
		load;;
	unload)
		unload;;
	*)
		echo "unknow option"
esac
