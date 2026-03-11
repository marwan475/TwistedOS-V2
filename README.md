# TwistedOS
The goal is to build a POSIX complaint portable operating system

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
    - Initialize GDT and TSS

Currently developing
- Kernel Entry

Build Dependencies:
- base-devel mingw-w64-gcc mingw-w64-crt mingw-w64-headers mtools dosfstools parted qemu-full clang llvm gnu-efi

Resources:
- https://uefi.org/sites/default/files/resources/UEFI_Spec_2_10_Aug29.pdf
- https://wiki.osdev.org/UEFI
- https://www.isec.tugraz.at/teaching/materials/os/tutorials/paging-on-intel-x86-64/
- http://wiki.osdev.org/Paging
- https://wiki.osdev.org/GDT_Tutorial
- https://wiki.osdev.org/Global_Descriptor_Table
