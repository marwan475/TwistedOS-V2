#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
	echo "Usage: $0 <ext2-image-path> <source-rootfs-dir>" >&2
	exit 1
fi

EXT2_IMAGE_PATH="$1"
SOURCE_ROOTFS_DIR="$2"

if [ ! -f "${EXT2_IMAGE_PATH}" ]; then
	echo "Error: ext2 image not found: ${EXT2_IMAGE_PATH}" >&2
	exit 1
fi

if [ ! -d "${SOURCE_ROOTFS_DIR}" ]; then
	echo "Error: source rootfs directory not found: ${SOURCE_ROOTFS_DIR}" >&2
	exit 1
fi

if ! command -v debugfs >/dev/null 2>&1; then
	echo "Error: debugfs not found. Install e2fsprogs." >&2
	exit 1
fi

SOURCE_ROOTFS_DIR="$(cd "${SOURCE_ROOTFS_DIR}" && pwd)"

COMMAND_FILE="$(mktemp)"
trap 'rm -f "${COMMAND_FILE}"' EXIT

while IFS= read -r relative_dir; do
	echo "mkdir /${relative_dir}" >>"${COMMAND_FILE}"
done < <(cd "${SOURCE_ROOTFS_DIR}" && find . -mindepth 1 -type d | sed 's|^\./||' | LC_ALL=C sort)

while IFS= read -r relative_path; do
	source_path="${SOURCE_ROOTFS_DIR}/${relative_path}"
	target_path="/${relative_path}"
	parent_path="$(dirname "${target_path}")"

	echo "mkdir ${parent_path}" >>"${COMMAND_FILE}"
	echo "rm ${target_path}" >>"${COMMAND_FILE}"

	if [ -L "${source_path}" ]; then
		link_target="$(readlink "${source_path}")"
		echo "symlink ${target_path} ${link_target}" >>"${COMMAND_FILE}"
	else
		echo "write ${source_path} ${target_path}" >>"${COMMAND_FILE}"
	fi
done < <(cd "${SOURCE_ROOTFS_DIR}" && find . -mindepth 1 \( -type f -o -type l \) | sed 's|^\./||' | LC_ALL=C sort)

debugfs -w -f "${COMMAND_FILE}" "${EXT2_IMAGE_PATH}" >/dev/null

echo "EXT2 rootfs populated from ${SOURCE_ROOTFS_DIR}"