#!/usr/bin/env bash
set -euo pipefail

SCRIPT_PATH="${BASH_SOURCE:-$0}"
SCRIPT_DIR="$(cd "$(dirname "${SCRIPT_PATH}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

ROOTFS_PARENT_DIR="${REPO_ROOT}/RootFileSystem"
ROOTFS_OUTPUT_DIR="${ROOTFS_PARENT_DIR}/alpine-rootfs"
DOWNLOAD_DIR="${ROOTFS_PARENT_DIR}/downloads"
ROOTFS_OVERLAY_DIR="${ROOTFS_PARENT_DIR}/overlay"
ROOTFS_OVERLAY_XORG_CONF="${ROOTFS_OVERLAY_DIR}/20-twisted-fbdev.conf"
ROOTFS_OVERLAY_XINITRC="${ROOTFS_OVERLAY_DIR}/.xinitrc"

ALPINE_MIRROR="${ALPINE_MIRROR:-https://dl-cdn.alpinelinux.org/alpine}"
ALPINE_ARCH="${ALPINE_ARCH:-x86_64}"
ALPINE_VERSION="${ALPINE_VERSION:-latest-stable}"
ALPINE_EXTRA_PACKAGES="${ALPINE_EXTRA_PACKAGES:-xorg-server xinit xkeyboard-config dwm xsetroot xf86-video-fbdev xf86-input-evdev}"
ALPINE_REQUIRED_PACKAGES="libx11"

require_command()
{
	local command_name="$1"
	if ! command -v "${command_name}" >/dev/null 2>&1; then
		echo "Error: required command '${command_name}' not found." >&2
		exit 1
	fi
}

run_as_root()
{
	if [ "${EUID}" -eq 0 ]; then
		"$@"
	elif command -v sudo >/dev/null 2>&1; then
		sudo "$@"
	else
		echo "Error: this step requires root privileges. Re-run as root or install sudo." >&2
		exit 1
	fi
}

resolve_alpine_branch()
{
	local version="$1"
	if [[ "${version}" =~ ^([0-9]+\.[0-9]+)(\..*)?$ ]]; then
		echo "${BASH_REMATCH[1]}"
	else
		echo "Error: unable to resolve Alpine branch from version '${version}'" >&2
		exit 1
	fi
}

download_apk_static()
{
	local mirror="$1"
	local branch="$2"
	local arch="$3"
	local download_dir="$4"

	local apkindex_url="${mirror}/v${branch}/main/${arch}/APKINDEX.tar.gz"
	local apkindex_path="${download_dir}/APKINDEX-v${branch}-${arch}.tar.gz"
	local apkindex_txt="${download_dir}/APKINDEX-v${branch}-${arch}.txt"

	echo "Fetching package index: ${apkindex_url}" >&2
	curl -fsSL "${apkindex_url}" -o "${apkindex_path}"
	tar -xzf "${apkindex_path}" -C "${download_dir}" APKINDEX
	mv "${download_dir}/APKINDEX" "${apkindex_txt}"

	local apk_tools_static_version
	apk_tools_static_version="$(awk '
		$0 == "P:apk-tools-static" { in_pkg = 1; next }
		in_pkg && /^V:/ { print substr($0, 3); exit }
		/^$/ { in_pkg = 0 }
	' "${apkindex_txt}")"

	if [ -z "${apk_tools_static_version}" ]; then
		echo "Error: unable to find apk-tools-static in ${apkindex_url}." >&2
		exit 1
	fi

	local apk_tools_static_filename="apk-tools-static-${apk_tools_static_version}.apk"
	local apk_tools_static_url="${mirror}/v${branch}/main/${arch}/${apk_tools_static_filename}"
	local apk_tools_static_path="${download_dir}/${apk_tools_static_filename}"
	local apk_tools_static_extract_dir="${download_dir}/apk-tools-static-v${branch}-${arch}"

	echo "Downloading ${apk_tools_static_url}" >&2
	curl -fsSL "${apk_tools_static_url}" -o "${apk_tools_static_path}"

	rm -rf "${apk_tools_static_extract_dir}"
	mkdir -p "${apk_tools_static_extract_dir}"
	tar -xzf "${apk_tools_static_path}" -C "${apk_tools_static_extract_dir}"

	local apk_static_path="${apk_tools_static_extract_dir}/sbin/apk.static"
	if [ ! -x "${apk_static_path}" ]; then
		echo "Error: extracted apk.static not found at ${apk_static_path}." >&2
		exit 1
	fi

	echo "${apk_static_path}"
}

resolve_latest_minirootfs()
{
	local latest_yaml_url="$1"
	local latest_yaml_path="$2"

	echo "Fetching release metadata: ${latest_yaml_url}" >&2
	curl -fsSL "${latest_yaml_url}" -o "${latest_yaml_path}"

	local minirootfs_filename
	minirootfs_filename="$(awk '/file: alpine-minirootfs-.*\.tar\.gz/{print $2; exit}' "${latest_yaml_path}")"
	if [ -z "${minirootfs_filename}" ]; then
		echo "Error: unable to find Alpine minirootfs in ${latest_yaml_url}." >&2
		exit 1
	fi

	local minirootfs_sha256
	minirootfs_sha256="$(awk '
		/file: alpine-minirootfs-.*\.tar\.gz/ { capture_sha = 1; next }
		capture_sha && /sha256:/ { print $2; exit }
	' "${latest_yaml_path}")"

	echo "${minirootfs_filename}|${minirootfs_sha256}"
}

write_rootfs_overlay_configs()
{
	if [ ! -f "${ROOTFS_OVERLAY_XORG_CONF}" ]; then
		echo "Error: missing overlay config ${ROOTFS_OVERLAY_XORG_CONF}" >&2
		exit 1
	fi

	if [ ! -f "${ROOTFS_OVERLAY_XINITRC}" ]; then
		echo "Error: missing overlay config ${ROOTFS_OVERLAY_XINITRC}" >&2
		exit 1
	fi

	run_as_root mkdir -p "${ROOTFS_OUTPUT_DIR}/etc/X11/xorg.conf.d"
	run_as_root mkdir -p "${ROOTFS_OUTPUT_DIR}/root"
	run_as_root cp "${ROOTFS_OVERLAY_XORG_CONF}" "${ROOTFS_OUTPUT_DIR}/etc/X11/xorg.conf.d/20-twisted-fbdev.conf"
	run_as_root cp "${ROOTFS_OVERLAY_XINITRC}" "${ROOTFS_OUTPUT_DIR}/root/.xinitrc"

	run_as_root chmod +x "${ROOTFS_OUTPUT_DIR}/root/.xinitrc"
}

main()
{
	require_command curl
	require_command tar
	require_command sha256sum
	require_command awk

	mkdir -p "${ROOTFS_PARENT_DIR}" "${DOWNLOAD_DIR}"

	local tar_filename
	local tar_sha256
	local tar_url
	local alpine_branch

	if [ "${ALPINE_VERSION}" = "latest-stable" ]; then
		local latest_yaml_url="${ALPINE_MIRROR}/latest-stable/releases/${ALPINE_ARCH}/latest-releases.yaml"
		local latest_yaml_path="${DOWNLOAD_DIR}/latest-releases-${ALPINE_ARCH}.yaml"
		local resolved
		resolved="$(resolve_latest_minirootfs "${latest_yaml_url}" "${latest_yaml_path}")"
		tar_filename="${resolved%%|*}"
		tar_sha256="${resolved#*|}"
		tar_url="${ALPINE_MIRROR}/latest-stable/releases/${ALPINE_ARCH}/${tar_filename}"
		local resolved_full_version
		resolved_full_version="$(echo "${tar_filename}" | sed -E 's/^alpine-minirootfs-([0-9]+\.[0-9]+\.[0-9]+)-.*$/\1/')"
		alpine_branch="$(resolve_alpine_branch "${resolved_full_version}")"
	else
		tar_filename="alpine-minirootfs-${ALPINE_VERSION}-${ALPINE_ARCH}.tar.gz"
		tar_url="${ALPINE_MIRROR}/v${ALPINE_VERSION}/releases/${ALPINE_ARCH}/${tar_filename}"
		tar_sha256=""
		alpine_branch="$(resolve_alpine_branch "${ALPINE_VERSION}")"
	fi

	local tar_path="${DOWNLOAD_DIR}/${tar_filename}"
	echo "Downloading ${tar_url}"
	curl -fL "${tar_url}" -o "${tar_path}"

	if [ -n "${tar_sha256}" ]; then
		echo "Verifying SHA256 checksum"
		echo "${tar_sha256}  ${tar_path}" | sha256sum -c -
	else
		echo "Warning: no checksum found for ALPINE_VERSION=${ALPINE_VERSION}; checksum verification skipped."
	fi

	echo "Rebuilding rootfs at ${ROOTFS_OUTPUT_DIR}"
	rm -rf "${ROOTFS_OUTPUT_DIR}"
	mkdir -p "${ROOTFS_OUTPUT_DIR}"
	tar -xzf "${tar_path}" -C "${ROOTFS_OUTPUT_DIR}"

	local apk_static_path
	apk_static_path="$(download_apk_static "${ALPINE_MIRROR}" "${alpine_branch}" "${ALPINE_ARCH}" "${DOWNLOAD_DIR}")"

	local packages_to_install="${ALPINE_EXTRA_PACKAGES} ${ALPINE_REQUIRED_PACKAGES}"
	echo "Installing packages into rootfs: ${packages_to_install}"
	# shellcheck disable=SC2086
	run_as_root "${apk_static_path}" \
		--root "${ROOTFS_OUTPUT_DIR}" \
		--arch "${ALPINE_ARCH}" \
		--keys-dir "${ROOTFS_OUTPUT_DIR}/etc/apk/keys" \
		--repositories-file "${ROOTFS_OUTPUT_DIR}/etc/apk/repositories" \
		--update-cache \
		add --no-cache ${packages_to_install}

	echo "Applying mapped overlay configs from ${ROOTFS_OVERLAY_DIR}"
	write_rootfs_overlay_configs

	echo "Alpine root filesystem ready at: ${ROOTFS_OUTPUT_DIR}"
}

main "$@"