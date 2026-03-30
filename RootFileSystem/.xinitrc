#!/bin/sh

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-root}"
mkdir -p "${XDG_RUNTIME_DIR}"
chmod 700 "${XDG_RUNTIME_DIR}" 2>/dev/null || true

if command -v xsetroot >/dev/null 2>&1; then
	xsetroot -solid "#1f2430"
fi

if command -v startxfce4 >/dev/null 2>&1; then
	if command -v dbus-run-session >/dev/null 2>&1; then
		exec dbus-run-session startxfce4
	fi
	exec startxfce4
fi

if command -v dwm >/dev/null 2>&1; then
	if command -v xfce4-terminal >/dev/null 2>&1; then
		xfce4-terminal &
	elif command -v xterm >/dev/null 2>&1; then
		xterm -fa "monospace" -fs 11 &
	fi
	exec dwm
fi

echo "xinitrc: no supported desktop session found"
while :; do
	sleep 60
done
