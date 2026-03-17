# TwistedOS

The goal is to build a semi-POSIX-compliant x86_64 operating system.

## Summary

TwistedOS currently boots through UEFI into a higher-half x86_64 kernel with physical and virtual memory management, process scheduling, sleep support, and user-process address-space isolation with syscall instruction support. It can load and run ELF binaries from the initramfs, with a kernel self-test suite and QEMU/GDB-based debug tooling for validation and inspection.

## Developed

- Bootstrapping a UEFI program on QEMU
- UEFI bootloader for x86_64 systems
    - EFI console logging
    - Sets up Graphics Output Protocol
    - Reads the kernel from the EFI filesystem
    - Gets the memory map
    - Exits boot services
    - Memory allocator using the memory map that can be transferred to the kernel
    - Sets up paging and identity maps usable RAM and the framebuffer
    - Loads the kernel and initramfs
    - Maps the kernel to the higher half and allocates a 64 KB stack
    - Transfers control to the higher-half kernel with kernel arguments
- Framebuffer logging for the kernel
- Kernel entry
    - Initialize GDT
    - Initialize IDT
    - Remap PIC
    - Initialize the syscall instruction
    - Initialize the timer
    - Physical memory manager using the memory map
    - Virtual memory manager
    - Set up a new kernel virtual heap
    - Jump to the dispatcher
- Layered kernel
    - Dispatcher
        - Initializes and manages layers
        - Callable from anywhere in the kernel
        - Called on interrupts
    - Resource layer
        - Manages all hardware resources
        - Stores physical and virtual memory managers
        - Initializes the kernel heap manager
            - Tracks free memory using a bitmap
            - Tracks allocations using a header before each allocation with size and magic
            - Supports `new` and `delete` operators for C++ object allocations in the kernel
        - Exposes task switching to the logic layer
        - Creates `RamFileSystemManager`
            - Loads files from initramfs
        - `VirtualAddressSpace`
            - Creates and manages virtual address space for user processes
    - Logic layer
        - Creates the process manager
            - Stores an array of `Process` structs using ID as the index
        - Exposes APIs to create and run processes
            - Supports kernel and user-level processes
            - User processes support isolation via virtual address space
        - Creates the scheduler
            - Schedules processes using the timer interrupt
        - Creates the synchronization manager
            - Sleeps processes for a certain amount of timer ticks
        - Creates `ELFManager`
            - Used to parse and map ELFs to user virtual address space
    - Translation layer
        - TODO: how user processes will interact with the kernel
        - Translates user requests and system calls to kernel services
- Debug support
    - Debug print to QEMU serial (`make DEBUG=1`)
    - Debug kernel source using GDB (`make debug`)
    - GDB extension to view important kernel structures
- Testing
    - The kernel has a self-test suite that creates a process that runs and monitors various test cases
    - Test cases
        - Memory
        - ELF and raw binary user creation
            - Syscall instruction validation
        - Multitasking and sleep

## Build Dependencies

- `base-devel`
- `mingw-w64-gcc`
- `mingw-w64-crt`
- `mingw-w64-headers`
- `mtools`
- `dosfstools`
- `parted`
- `qemu-full`
- `clang`
- `llvm`
- `gnu-efi`
- `nasm`
- `gdb`
- `python`
- `cpio`

## Build Instructions

Build the full OS image from the repository root:

```sh
make
```

This produces the bootloader, kernel, initramfs, `TwistedOS.img`, and a persistent `TwistedDrive.img` used by the full QEMU configuration.

Run the OS in QEMU:

```sh
make qemu
```

Run a more minimal QEMU configuration:

```sh
make qemu-basic
```

Build with debug serial logging enabled:

```sh
make DEBUG=1
```

Clean generated build artifacts:

```sh
make clean
```

Format all C and C++ source files:

```sh
make format
```

## GDB

- `make debug` launches QEMU and GDB.
- Available debug extension commands:
    - `twistedos-help`
    - `twistedos-processes`
    - `twistedos-ready-queue`
    - `twistedos-sleep-queue`
    - `twistedos-address-space`
    - `twistedos-physical-memory`

## Resources

- https://uefi.org/sites/default/files/resources/UEFI_Spec_2_10_Aug29.pdf
- https://wiki.osdev.org/UEFI
- https://www.isec.tugraz.at/teaching/materials/os/tutorials/paging-on-intel-x86-64/
- http://wiki.osdev.org/Paging
- https://wiki.osdev.org/GDT_Tutorial
- https://wiki.osdev.org/Global_Descriptor_Table
- https://wiki.osdev.org/Initrd
- https://wiki.osdev.org/Model_Specific_Registers
- https://wiki.osdev.org/ELF
- https://wiki.osdev.org/Exceptions
