#!/bin/sh

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-root}"
mkdir -p "${XDG_RUNTIME_DIR}"
chmod 700 "${XDG_RUNTIME_DIR}" 2>/dev/null || true

# --- background & cursor ---
xsetroot -solid "#1a1b26" 2>/dev/null || true
xsetroot -cursor_name left_ptr 2>/dev/null || true

# --- dbus session bus ---
if command -v dbus-daemon >/dev/null 2>&1; then
	eval "$(dbus-launch --sh-syntax 2>/dev/null)" || true
fi

# --- status bar ---
while true; do
	xsetroot -name " TwistedOS " 2>/dev/null
	sleep 30
done &

# --- launch a terminal on startup ---
if command -v st >/dev/null 2>&1; then
	st &
elif command -v xterm >/dev/null 2>&1; then
	xterm -fa "DejaVu Sans Mono" -fs 11 &
fi

# --- run dwm (loop restarts it on clean exit / reload) ---
if command -v dwm >/dev/null 2>&1; then
	while true; do
		dwm 2>/tmp/dwm.log
	done
fi

echo "xinitrc: dwm not found"
while :; do
	sleep 60
done
