#!/usr/bin/env bash
set -euo pipefail

SCRIPT_PATH="${BASH_SOURCE:-$0}"
SCRIPT_DIR="$(cd "$(dirname "${SCRIPT_PATH}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

ROOTFS_PARENT_DIR="${REPO_ROOT}/RootFileSystem"
ROOTFS_OUTPUT_DIR="${ROOTFS_PARENT_DIR}/alpine-rootfs"
DOWNLOAD_DIR="${ROOTFS_PARENT_DIR}/downloads"

ALPINE_MIRROR="${ALPINE_MIRROR:-https://dl-cdn.alpinelinux.org/alpine}"
ALPINE_ARCH="${ALPINE_ARCH:-x86_64}"
ALPINE_VERSION="${ALPINE_VERSION:-latest-stable}"

require_command()
{
	local command_name="$1"
	if ! command -v "${command_name}" >/dev/null 2>&1; then
		echo "Error: required command '${command_name}' not found." >&2
		exit 1
	fi
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

main()
{
	require_command curl
	require_command tar
	require_command sha256sum

	mkdir -p "${ROOTFS_PARENT_DIR}" "${DOWNLOAD_DIR}"

	local tar_filename
	local tar_sha256
	local tar_url

	if [ "${ALPINE_VERSION}" = "latest-stable" ]; then
		local latest_yaml_url="${ALPINE_MIRROR}/latest-stable/releases/${ALPINE_ARCH}/latest-releases.yaml"
		local latest_yaml_path="${DOWNLOAD_DIR}/latest-releases-${ALPINE_ARCH}.yaml"
		local resolved
		resolved="$(resolve_latest_minirootfs "${latest_yaml_url}" "${latest_yaml_path}")"
		tar_filename="${resolved%%|*}"
		tar_sha256="${resolved#*|}"
		tar_url="${ALPINE_MIRROR}/latest-stable/releases/${ALPINE_ARCH}/${tar_filename}"
	else
		tar_filename="alpine-minirootfs-${ALPINE_VERSION}-${ALPINE_ARCH}.tar.gz"
		tar_url="${ALPINE_MIRROR}/v${ALPINE_VERSION}/releases/${ALPINE_ARCH}/${tar_filename}"
		tar_sha256=""
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

	echo "Alpine root filesystem ready at: ${ROOTFS_OUTPUT_DIR}"
}

main "$@"