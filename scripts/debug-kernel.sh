#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
port="${QEMU_GDB_PORT:-1234}"
bootloader_efi="${repo_root}/bin/BOOTX64.EFI"
kernel_elf="${repo_root}/build/kernel.elf"
gdb_extension="${repo_root}/scripts/twistedos_gdb.py"
qemu_log="${repo_root}/build/qemu-debug-launch.log"
serial_log="${repo_root}/build/qemu-debug-serial.log"
gdb_cmds="${repo_root}/build/gdb-debug-init.gdb"
qemu_pid=""

pick_objdump() {
    if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
        echo "x86_64-w64-mingw32-objdump"
        return 0
    fi

    if command -v objdump >/dev/null 2>&1; then
        echo "objdump"
        return 0
    fi

    return 1
}

detect_bootloader_text_vma() {
    local objdump_cmd

    if ! objdump_cmd="$(pick_objdump)"; then
        return 1
    fi

    "${objdump_cmd}" -h "${bootloader_efi}" 2>/dev/null | awk '$2 == ".text" { print "0x" $4; exit }'
}

cleanup() {
    if [[ -n "${qemu_pid}" ]] && kill -0 "${qemu_pid}" 2>/dev/null; then
        kill "${qemu_pid}" 2>/dev/null || true
        wait "${qemu_pid}" 2>/dev/null || true
    fi

    rm -f "${gdb_cmds}"
}

is_gdb_stub_listening() {
    if command -v ss >/dev/null 2>&1; then
        ss -ltnH "sport = :${port}" | grep -q ":${port}[[:space:]]"
        return $?
    fi

    if command -v netstat >/dev/null 2>&1; then
        netstat -ltn 2>/dev/null | awk '{print $4}' | grep -Eq "[.:]${port}$"
        return $?
    fi

    echo "Need either ss or netstat to detect the QEMU GDB stub." >&2
    return 1
}

ensure_port_is_free() {
    if is_gdb_stub_listening; then
        echo "Port localhost:${port} is already in use. Stop the existing QEMU/GDB session first." >&2
        return 1
    fi

    return 0
}

wait_for_gdb_stub() {
    local attempt

    for attempt in $(seq 1 100); do
        if is_gdb_stub_listening; then
            return 0
        fi

        if [[ -n "${qemu_pid}" ]] && ! kill -0 "${qemu_pid}" 2>/dev/null; then
            echo "QEMU exited before the GDB stub became available. See ${qemu_log}." >&2
            return 1
        fi

        sleep 0.1
    done

    echo "Timed out waiting for QEMU GDB stub on localhost:${port}. See ${qemu_log}." >&2
    return 1
}

trap cleanup EXIT INT TERM

mkdir -p "${repo_root}/build"
rm -f "${qemu_log}" "${serial_log}" "${gdb_cmds}"

if [[ ! -f "${bootloader_efi}" || ! -f "${kernel_elf}" ]]; then
    echo "Building OS image to ensure bootloader and kernel symbols are present"
    make -C "${repo_root}" all
fi

if [[ ! -f "${bootloader_efi}" ]]; then
    echo "Bootloader image not found at ${bootloader_efi}" >&2
    exit 1
fi

if [[ ! -f "${kernel_elf}" ]]; then
    echo "Kernel ELF not found at ${kernel_elf}" >&2
    exit 1
fi

if [[ ! -f "${gdb_extension}" ]]; then
    echo "GDB extension not found at ${gdb_extension}" >&2
    exit 1
fi

bootloader_text_vma="${BOOTLOADER_TEXT_VMA:-}"
if [[ -z "${bootloader_text_vma}" ]]; then
    bootloader_text_vma="$(detect_bootloader_text_vma || true)"
fi

if [[ -z "${bootloader_text_vma}" ]]; then
    bootloader_text_vma="0x140001000"
fi

ensure_port_is_free

echo "Starting QEMU paused with GDB stub on localhost:${port}"
make -C "${repo_root}" qemu-basic-debug >"${qemu_log}" 2>&1 &
qemu_pid=$!

wait_for_gdb_stub

cat >"${gdb_cmds}" <<EOF
set pagination off
set breakpoint pending on
set architecture i386:x86-64
target remote localhost:${port}

symbol-file ${kernel_elf}
add-symbol-file ${bootloader_efi} ${bootloader_text_vma}
source ${gdb_extension}

hbreak efi_main
hbreak FileSystem::SetupForKernel
hbreak KernelEntry

echo Connected. Use 'continue' to reach efi_main, then continue into kernel.\\n
echo Run 'twistedos-help' to inspect kernel scheduler state.\\n
EOF

echo "Launching GDB (TUI mode) with symbols from ${bootloader_efi} and ${kernel_elf}"
echo "Bootloader .text assumed at ${bootloader_text_vma}"
"${GDB:-gdb}" -tui -x "${gdb_cmds}"