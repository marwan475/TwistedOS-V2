# TwistedOS
The goal is to build a semi POSIX complaint x86 64 bit operating system

Developed
- Bootstraping a UEFI program on qemu
- UEFI bootloader for x86_64 systems
    - EfiConsole logging
    - Sets up Graphics Output Protocol
    - Reads Kernel From Efi filesystem
    - Gets Memory map
    - exits boot services
    - Memory Allocater using memory map that can be transfered to kernel
    - Sets up paging and identiy maps usable ram and framebuffer
    - Maps kernel to higher half and allocates 64kb stack
    - Transfers control to higher half kernel with Kernel Arguments
- Framebuffer Logging for kernel
- Kernel Entry
    - init IDT
    - Physical memory manager using memory map
    - Virtual memory manager
    - Setup New Kernel virtual Heap
    - Jump to dispatcher
- Layered Kernel
    - Dispatcher
        - Init and manage layers
        - Callable from anywhere in the kernel
        - Called on Interrupt
    - Resource Layer
        - Manages all Hardware resources
        - Stores Physical and Virtual Memory managers
        - Initializes Kernel Heap manager
            - Tracks free memory using bitmap
            - Tracks allocations using header before allocation with size and magic
            - Supports new and delete operators for C++ object allocations in the kernel 
        - exposes Task Switch to Logic Layer
    - Logic Layer
        - Creates Process manager
            - Stores array of Process structs using id to index
        - Exposes Api to Create and run processes
        - Creates Scheduler
            - Schedules Process using timer interrupt
    - Translation Layer
        - TODO (How User process will interract with the kernel)



Build Dependencies:
- base-devel mingw-w64-gcc mingw-w64-crt mingw-w64-headers mtools dosfstools parted qemu-full clang llvm gnu-efi nasm gdb

GDB Helpers:
- `make debug` now loads `scripts/twistedos_gdb.py` automatically.
- `make debug` launches GDB in TUI mode.
- Available commands:
    - `twistedos-help`
    - `twistedos-processes`
    - `twistedos-ready-queue`
    - `twistedos-sleep-queue`
- These commands resolve the active kernel objects through `Dispatcher::ActiveDispatcher` by default.
- Each command also accepts an optional explicit expression if you want to inspect a non-default instance.

Resources:
- https://uefi.org/sites/default/files/resources/UEFI_Spec_2_10_Aug29.pdf
- https://wiki.osdev.org/UEFI
- https://www.isec.tugraz.at/teaching/materials/os/tutorials/paging-on-intel-x86-64/
- http://wiki.osdev.org/Paging
- https://wiki.osdev.org/GDT_Tutorial
- https://wiki.osdev.org/Global_Descriptor_Table
