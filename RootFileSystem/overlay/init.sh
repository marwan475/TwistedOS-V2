#!/bin/sh

mkdir -p /proc /sys
mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true

mkdir -p /tmp
chmod 1777 /tmp

#xinit
exec /bin/sh
