#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
	echo "Usage: $0 <ext2-image-path> <overlay-dir>" >&2
	exit 1
fi

EXT2_IMAGE_PATH="$1"
OVERLAY_DIR="$2"

if [ ! -f "${EXT2_IMAGE_PATH}" ]; then
	echo "Error: ext2 image not found: ${EXT2_IMAGE_PATH}" >&2
	exit 1
fi

if [ ! -d "${OVERLAY_DIR}" ]; then
	echo "Error: overlay directory not found: ${OVERLAY_DIR}" >&2
	exit 1
fi

if ! command -v debugfs >/dev/null 2>&1; then
	echo "Error: debugfs not found. Install e2fsprogs." >&2
	exit 1
fi

OVERLAY_DIR="$(cd "${OVERLAY_DIR}" && pwd)"
XORG_CONF_PATH="${OVERLAY_DIR}/20-twisted-fbdev.conf"
XINITRC_PATH="${OVERLAY_DIR}/.xinitrc"
INIT_SH_PATH="${OVERLAY_DIR}/init.sh"

if [ ! -f "${XORG_CONF_PATH}" ]; then
	echo "Error: missing overlay file: ${XORG_CONF_PATH}" >&2
	exit 1
fi

if [ ! -f "${XINITRC_PATH}" ]; then
	echo "Error: missing overlay file: ${XINITRC_PATH}" >&2
	exit 1
fi

if [ ! -f "${INIT_SH_PATH}" ]; then
	echo "Error: missing overlay file: ${INIT_SH_PATH}" >&2
	exit 1
fi

debugfs -w -R "mkdir /etc" "${EXT2_IMAGE_PATH}" >/dev/null 2>&1 || true
debugfs -w -R "mkdir /etc/X11" "${EXT2_IMAGE_PATH}" >/dev/null 2>&1 || true
debugfs -w -R "mkdir /etc/X11/xorg.conf.d" "${EXT2_IMAGE_PATH}" >/dev/null 2>&1 || true
debugfs -w -R "mkdir /root" "${EXT2_IMAGE_PATH}" >/dev/null 2>&1 || true

debugfs -w -R "rm /etc/X11/xorg.conf.d/20-twisted-fbdev.conf" "${EXT2_IMAGE_PATH}" >/dev/null 2>&1 || true
debugfs -w -R "rm /root/.xinitrc" "${EXT2_IMAGE_PATH}" >/dev/null 2>&1 || true
debugfs -w -R "rm /init.sh" "${EXT2_IMAGE_PATH}" >/dev/null 2>&1 || true

debugfs -w -R "write ${XORG_CONF_PATH} /etc/X11/xorg.conf.d/20-twisted-fbdev.conf" "${EXT2_IMAGE_PATH}" >/dev/null
debugfs -w -R "write ${XINITRC_PATH} /root/.xinitrc" "${EXT2_IMAGE_PATH}" >/dev/null
debugfs -w -R "write ${INIT_SH_PATH} /init.sh" "${EXT2_IMAGE_PATH}" >/dev/null

debugfs -w -R "set_inode_field /etc/X11/xorg.conf.d/20-twisted-fbdev.conf mode 0100644" "${EXT2_IMAGE_PATH}" >/dev/null 2>&1 || true
debugfs -w -R "set_inode_field /root/.xinitrc mode 0100755" "${EXT2_IMAGE_PATH}" >/dev/null 2>&1 || true
debugfs -w -R "set_inode_field /init.sh mode 0100755" "${EXT2_IMAGE_PATH}" >/dev/null 2>&1 || true

echo "Applied rootfs overlay to EXT2 image from ${OVERLAY_DIR}"