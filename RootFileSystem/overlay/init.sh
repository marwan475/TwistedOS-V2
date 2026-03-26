#!/bin/sh

mkdir -p /proc /sys
mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true

mkdir -p /tmp /tmp/.X11-unix /var/lib/xkb
chmod 1777 /tmp
chmod 1777 /tmp/.X11-unix

if [ -x /usr/bin/xinit ] && [ -x /usr/bin/xsetroot ] && [ -x /usr/libexec/Xorg ]; then
	echo "init: starting bare xinit (xsetroot -- /usr/libexec/Xorg)"
	exec /usr/bin/xinit /usr/bin/xsetroot -solid '#224488' -- /usr/libexec/Xorg :0 -wr -nolisten tcp
fi

if [ -x /usr/bin/xinit ] && [ -x /usr/bin/xsetroot ] && [ -x /usr/bin/Xorg ]; then
	echo "init: starting bare xinit (xsetroot -- /usr/bin/Xorg)"
	exec /usr/bin/xinit /usr/bin/xsetroot -solid '#224488' -- /usr/bin/Xorg :0 -wr -nolisten tcp
fi

echo "init: Xorg binary not found; refusing to exec shell"
while true; do
	sleep 60
done
