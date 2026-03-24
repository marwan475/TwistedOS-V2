#!/bin/sh

if command -v dwm >/dev/null 2>&1; then
	exec dwm
fi

while :; do
	sleep 1
done
