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

Kernel debugging with QEMU:
- The kernel is linked as `build/kernel.elf` with DWARF symbols and then converted to `bin/kernel.bin` for boot. Use the ELF file as the debugger symbol file.
- The bootloader image `bin/BOOTX64.EFI` is also built with debug symbols so bootloader source can be debugged in GDB.
- Fast path: run `make debug` to build, start QEMU in paused debug mode, wait for the GDB stub, and launch GDB automatically.
- `make debug` now sets breakpoints for `efi_main`, `FileSystem::SetupForKernel`, and `KernelEntry` so you can step bootloader -> kernel in one session.
- Start a paused QEMU session with the GDB stub enabled:
    - `make qemu-basic-debug`
    - or `make qemu-debug` if you want the full device configuration
- Attach GDB from another terminal:
    - `make gdb-kernel`
- After GDB connects, continue execution and it will stop on the hardware breakpoint at `KernelEntry`:
    - `continue`
- Serial output from debug-mode QEMU is written to `build/qemu-debug-serial.log`.

VS Code debugging:
- Use the `Kernel: Launch QEMU and attach` launch configuration to start QEMU in paused mode and attach GDB automatically.
- Use the `Kernel: Attach to QEMU` launch configuration if QEMU is already running with `make qemu-basic-debug` or `make qemu-debug`.
- The debugger uses `build/kernel.elf` as the symbol file and connects to QEMU on `localhost:1234`.

Resources:
- https://uefi.org/sites/default/files/resources/UEFI_Spec_2_10_Aug29.pdf
- https://wiki.osdev.org/UEFI
- https://www.isec.tugraz.at/teaching/materials/os/tutorials/paging-on-intel-x86-64/
- http://wiki.osdev.org/Paging
- https://wiki.osdev.org/GDT_Tutorial
- https://wiki.osdev.org/Global_Descriptor_Table
