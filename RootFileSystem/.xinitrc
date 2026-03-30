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

# --- status bar (date, memory, uptime in dwm title) ---
statusbar() {
	while true; do
		MEM=""
		if [ -f /proc/meminfo ]; then
			MEM_USED=$(awk '/MemTotal/{t=$2} /MemAvailable/{a=$2} END{printf "%.0f",(t-a)/1024}' /proc/meminfo 2>/dev/null)
			MEM_TOTAL=$(awk '/MemTotal/{printf "%.0f",$2/1024}' /proc/meminfo 2>/dev/null)
			if [ -n "${MEM_USED}" ] && [ -n "${MEM_TOTAL}" ]; then
				MEM="mem ${MEM_USED}/${MEM_TOTAL}M"
			fi
		fi

		UP=""
		if [ -f /proc/uptime ]; then
			SECS=$(awk '{printf "%.0f",$1}' /proc/uptime 2>/dev/null)
			if [ -n "${SECS}" ]; then
				HRS=$((SECS / 3600))
				MINS=$(( (SECS % 3600) / 60 ))
				UP="up ${HRS}h${MINS}m"
			fi
		fi

		DATE=$(date '+%a %b %d  %H:%M' 2>/dev/null)

		SEP=" | "
		BAR=""
		[ -n "${MEM}" ] && BAR="${MEM}"
		[ -n "${UP}" ]  && BAR="${BAR:+${BAR}${SEP}}${UP}"
		[ -n "${DATE}" ] && BAR="${BAR:+${BAR}${SEP}}${DATE}"

		xsetroot -name " ${BAR} " 2>/dev/null
		sleep 5
	done
}
statusbar &

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
