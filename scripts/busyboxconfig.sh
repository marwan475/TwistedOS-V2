#!/usr/bin/env bash
set -euo pipefail

SCRIPT_PATH="${BASH_SOURCE:-$0}"
SCRIPT_DIR="$(cd "$(dirname "${SCRIPT_PATH}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUSYBOX_DIR="${REPO_ROOT}/busybox"
ROOTFS_DIR="${REPO_ROOT}/initramfs/rootfs"
INSTALL_DIR="${ROOTFS_DIR}/bin"
BUILD_DIR="${REPO_ROOT}/build"
DEBUG_SYMBOLS_OUT="${BUILD_DIR}/busybox.debug"
if [ -n "${MUSL_CC:-}" ]; then
	MUSL_CC="${MUSL_CC}"
elif command -v x86_64-linux-musl-gcc >/dev/null 2>&1; then
	MUSL_CC="x86_64-linux-musl-gcc"
else
	MUSL_CC="musl-gcc"
fi

if ! command -v "${MUSL_CC}" >/dev/null 2>&1; then
	echo "Error: '${MUSL_CC}' not found. Install musl toolchain first (e.g. package providing musl-gcc)." >&2
	exit 1
fi

BUSYBOX_DEBUG="${BUSYBOX_DEBUG:-0}"
if [[ "${BUSYBOX_DEBUG}" == "1" ]]; then
	BUSYBOX_EXTRA_CFLAGS="-O0 -g3 -fno-omit-frame-pointer -static"
	BUSYBOX_MAKE_ARGS=("SKIP_STRIP=y")
	echo "BusyBox build mode: DEBUG (symbols enabled)"
else
	BUSYBOX_EXTRA_CFLAGS="-Os -static"
	BUSYBOX_MAKE_ARGS=()
	echo "BusyBox build mode: RELEASE"
fi

cd "${BUSYBOX_DIR}"

make distclean
make allnoconfig

cat >> .config <<'EOF'
CONFIG_DESKTOP=n
CONFIG_EXTRA_COMPAT=n
CONFIG_STATIC=y
CONFIG_CROSS_COMPILER_PREFIX=""
CONFIG_PIE=n
CONFIG_NOMMU=n
CONFIG_BUSYBOX=y

CONFIG_FEATURE_INSTALLER=n
CONFIG_INSTALL_NO_USR=y
CONFIG_LOCALE_SUPPORT=n
CONFIG_UNICODE_SUPPORT=n
CONFIG_LONG_OPTS=n
CONFIG_SHOW_USAGE=n
CONFIG_FEATURE_VERBOSE_USAGE=n
CONFIG_FEATURE_COMPRESS_USAGE=n

CONFIG_LFS=y

# Bare-minimum applets we want
CONFIG_ECHO=y
CONFIG_CAT=y
CONFIG_LS=y
CONFIG_ASH=y
CONFIG_SH_IS_ASH=y
CONFIG_BASH_IS_NONE=y

# Keep shell small
CONFIG_ASH_OPTIMIZE_FOR_SIZE=y
CONFIG_ASH_INTERNAL_GLOB=y
CONFIG_ASH_BASH_COMPAT=n
CONFIG_ASH_JOB_CONTROL=n
CONFIG_ASH_ALIAS=n
CONFIG_ASH_RANDOM_SUPPORT=n
CONFIG_ASH_EXPAND_PRMT=n
CONFIG_ASH_IDLE_TIMEOUT=n
CONFIG_ASH_MAIL=n
CONFIG_ASH_ECHO=y
CONFIG_ASH_PRINTF=n
CONFIG_ASH_TEST=y
CONFIG_ASH_HELP=n
CONFIG_ASH_GETOPTS=n
CONFIG_ASH_CMDCMD=n

# Disable hush completely
CONFIG_HUSH=n
CONFIG_SH_IS_HUSH=n
CONFIG_SHELL_HUSH=n

# Common shell extras off
CONFIG_FEATURE_SH_MATH=n
CONFIG_FEATURE_SH_EXTRA_QUIET=y
CONFIG_FEATURE_SH_STANDALONE=n
CONFIG_FEATURE_SH_NOFORK=n
CONFIG_FEATURE_SH_READ_FRAC=n
CONFIG_FEATURE_SH_HISTFILESIZE=n
CONFIG_FEATURE_SH_EMBEDDED_SCRIPTS=n

# Process basics off
CONFIG_KILL=n
CONFIG_KILLALL=n
CONFIG_PIDOF=n

# Filesystem / misc mostly off
CONFIG_TEST=n
CONFIG_BASENAME=n
CONFIG_DIRNAME=n
CONFIG_EXPR=n
CONFIG_ENV=n
CONFIG_PRINTF=n
CONFIG_SLEEP=n
CONFIG_USLEEP=n
CONFIG_HEAD=n
CONFIG_TAIL=n
CONFIG_CP=n
CONFIG_MV=n
CONFIG_RM=n
CONFIG_MKDIR=n
CONFIG_TOUCH=n

# Logging / proc / networking off
CONFIG_SYSLOGD=n
CONFIG_KLOGD=n
CONFIG_LOGGER=n
CONFIG_DMESG=n
CONFIG_KBD_MODE=n
CONFIG_IP=n
CONFIG_IFCONFIG=n
CONFIG_ROUTE=n
CONFIG_PING=n
CONFIG_WGET=n
CONFIG_NC=n

# Crond / mail / print / admin stuff off
CONFIG_CROND=n
CONFIG_CRONTAB=n
CONFIG_SENDMAIL=n
CONFIG_LPD=n
CONFIG_LPR=n
CONFIG_LPQ=n

# Editors / viewers off
CONFIG_LESS=n
CONFIG_VI=n
CONFIG_HEXEDIT=n
CONFIG_MAN=n

# Device tools off
CONFIG_DEVMEM=n
CONFIG_HDPARM=n
CONFIG_MAKEDEVS=n
CONFIG_MDEV=n
CONFIG_MDEV_CONF=n

# Login / passwd off
CONFIG_LOGIN=n
CONFIG_PASSWD=n
CONFIG_SU=n
CONFIG_GETTY=n

# Init off for now
CONFIG_INIT=n

EOF

if make -n olddefconfig >/dev/null 2>&1; then
	make olddefconfig
else
	answers_file="$(mktemp)"
	for _ in $(seq 1 20000); do
		echo
	done > "${answers_file}"
	make oldconfig < "${answers_file}"
	rm -f "${answers_file}"
fi

set_config_y() {
	local sym="$1"
	if grep -q "^# ${sym} is not set$" .config; then
		sed -i "s/^# ${sym} is not set$/${sym}=y/" .config
	elif grep -q "^${sym}=" .config; then
		sed -i "s/^${sym}=.*/${sym}=y/" .config
	else
		echo "${sym}=y" >> .config
	fi
}

set_config_n() {
	local sym="$1"
	if grep -q "^${sym}=" .config; then
		sed -i "s/^${sym}=.*/# ${sym} is not set/" .config
	elif ! grep -q "^# ${sym} is not set$" .config; then
		echo "# ${sym} is not set" >> .config
	fi
}

# Force required applets/modes regardless of Kconfig defaults.
set_config_y CONFIG_STATIC
set_config_y CONFIG_LS
set_config_y CONFIG_CAT
set_config_y CONFIG_ECHO
set_config_y CONFIG_SH_IS_ASH
set_config_y CONFIG_SHELL_ASH
set_config_n CONFIG_HUSH
set_config_n CONFIG_SHELL_HUSH
set_config_n CONFIG_FEATURE_SH_STANDALONE

if make -n olddefconfig >/dev/null 2>&1; then
	make olddefconfig
fi

if ! grep -q '^CONFIG_LS=y$' .config; then
	echo "Error: BusyBox configuration did not enable CONFIG_LS=y" >&2
	exit 1
fi

if ! grep -q '^CONFIG_SHELL_ASH=y$' .config; then
	echo "Error: BusyBox configuration did not enable CONFIG_SHELL_ASH=y" >&2
	exit 1
fi

echo "BusyBox minimal musl config written."

make \
	CC="${MUSL_CC}" \
	EXTRA_CFLAGS="${BUSYBOX_EXTRA_CFLAGS}" \
	EXTRA_LDFLAGS="-static" \
	"${BUSYBOX_MAKE_ARGS[@]}" \
	-j"$(nproc)"

if command -v readelf >/dev/null 2>&1; then
	if readelf -l busybox | grep -q "Requesting program interpreter"; then
		echo "Error: BusyBox is dynamically linked (PT_INTERP present)." >&2
		exit 1
	fi
	if readelf -d busybox | grep -q "(NEEDED)"; then
		echo "Error: BusyBox is dynamically linked (DT_NEEDED entries present)." >&2
		exit 1
	fi
fi

if command -v strings >/dev/null 2>&1; then
	if strings busybox | grep -q "GNU C Library"; then
		echo "Warning: BusyBox binary appears to contain glibc strings." >&2
		echo "If you require musl specifically, try: MUSL_CC=x86_64-linux-musl-gcc bash ./scripts/busyboxconfig.sh" >&2
	fi
fi

mkdir -p "${INSTALL_DIR}"
mkdir -p "${BUILD_DIR}"

if [[ "${BUSYBOX_DEBUG}" == "1" ]]; then
	cp -f busybox "${DEBUG_SYMBOLS_OUT}"
fi

cp -f busybox "${INSTALL_DIR}/busybox"
ln -sf busybox "${INSTALL_DIR}/sh"
ln -sf busybox "${INSTALL_DIR}/ls"
ln -sf busybox "${INSTALL_DIR}/cat"
ln -sf busybox "${INSTALL_DIR}/echo"

echo "Built: ${BUSYBOX_DIR}/busybox"
echo "Installed: ${INSTALL_DIR}/busybox and ${INSTALL_DIR}/sh"
if [[ "${BUSYBOX_DEBUG}" == "1" ]]; then
	echo "Debug symbols copy: ${DEBUG_SYMBOLS_OUT}"
fi
echo "Compiler: ${MUSL_CC}"
if command -v file >/dev/null 2>&1; then
	file "${INSTALL_DIR}/busybox"
fi