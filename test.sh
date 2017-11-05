#!/bin/sh

./a.out /dev/sbull0
./load.sh load
#sudo mount -t zramfs /dev/sbull0 ramfs
sudo chown vita:vita ramfs
touch ramfs/aa
