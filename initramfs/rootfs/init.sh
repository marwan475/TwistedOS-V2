#!/bin/sh

test -e /Test1 || exec /bin/sh

mount -t ext2 /dev/sda2 /mnt

exec chroot /mnt /bin/sh /init.sh