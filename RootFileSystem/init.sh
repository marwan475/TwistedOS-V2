#!/bin/sh

mkdir -p /proc /sys
mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true

mkdir -p /tmp /tmp/.X11-unix /var/lib/xkb
chmod 1777 /tmp
chmod 1777 /tmp/.X11-unix

if [ -x /usr/bin/xinit ] && [ -x /root/.xinitrc ]; then
	XORG_BIN=""
	if [ -x /usr/libexec/Xorg ]; then
		XORG_BIN="/usr/libexec/Xorg"
	elif [ -x /usr/bin/Xorg ]; then
		XORG_BIN="/usr/bin/Xorg"
	fi

	if [ -n "${XORG_BIN}" ]; then
		echo "init: starting xinit (/root/.xinitrc -- ${XORG_BIN})"
		exec /usr/bin/xinit /root/.xinitrc -- "${XORG_BIN}" :0 -wr -nolisten tcp
	fi
fi

echo "init: required binaries not found (xinit/.xinitrc/Xorg); refusing to exec shell"
while true; do
	sleep 60
done
