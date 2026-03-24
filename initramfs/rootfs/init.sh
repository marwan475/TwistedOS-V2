#!/bin/sh

test -e /Test1 || exec /bin/sh

mount -t ext2 /dev/sda2 /mnt

exec chroot /mnt /bin/sh -c 'mkdir -p /proc /sys; mount -t proc proc /proc; mount -t sysfs sysfs /sys; exec /bin/sh'