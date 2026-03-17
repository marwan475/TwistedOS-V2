CC = x86_64-w64-mingw32-g++ \
		-Wl,--subsystem,10 \
		-e efi_main

LD = x86_64-w64-mingw32-ld

CFLAGS = \
	-Wall \
	-Wextra \
	-Wpedantic \
	-g \
	-O0 \
	-mno-red-zone \
	-ffreestanding \
	-nostdlib \
	-fno-exceptions \
	-fno-rtti \
	-Wno-missing-field-initializers

KERNEL_CC = g++
KERNEL_LD = ld
KERNEL_AS = nasm
KERNEL_CFLAGS = \
    -ffreestanding \
	-fno-pic \
	-fno-pie \
	-mcmodel=kernel \
    -mno-red-zone \
    -nostdlib \
    -fno-exceptions \
    -fno-rtti \
    -fno-stack-protector \
	-fno-omit-frame-pointer \
	-g \
    -O0 \
    -Wall \
    -Wextra

KERNEL_LDFLAGS = \
    -nostdlib \
	-e KernelEntry \
    -T Kernel/linker.ld

KERNEL_ASFLAGS = \
	-f elf64 \
	-g \
	-F dwarf

ifeq ($(DEBUG),1)
CFLAGS += -DDEBUG_BUILD
KERNEL_CFLAGS += -DDEBUG_BUILD
endif

BIN = bin/
BUILD = build/
OUTPUT = TwistedOS.img

EFI = BOOTX64.EFI
KERNEL = kernel.bin
INITRAMFS = initramfs.cpio
IMG = $(OUTPUT)
DRIVE = TwistedDrive.img
ROOTFS_DIR = initramfs/rootfs
INIT_SRC = initramfs/init.c
INIT_LD = initramfs/init.ld
INIT_OBJ = $(BUILD)initramfs_init.o
INIT_ELF = $(BUILD)initramfs_init.elf
INIT_BIN = $(ROOTFS_DIR)/init
INIT2_SRC = initramfs/init2.c
INIT2_OBJ = $(BUILD)initramfs_init2.o
INIT2_ELF = $(BUILD)initramfs_init2.elf
INIT2_BIN = $(ROOTFS_DIR)/init2
INIT2_CC = gcc
INIT_CFLAGS = \
	-ffreestanding \
	-fno-pic \
	-fno-pie \
	-mno-red-zone \
	-nostdlib \
	-fno-stack-protector \
	-g \
	-O0 \
	-Wall \
	-Wextra
ESP_SIZE = 64
SECTORS = $(shell echo $$(( $(ESP_SIZE) * 2048 )))
GDB = gdb
QEMU_GDB_PORT = 1234
QEMU_DEBUG_SERIAL_LOG = $(BUILD)qemu-debug-serial.log
QEMU = qemu-system-x86_64
QEMU_FW = \
	-drive if=pflash,format=raw,readonly=on,file=Firmware/code.fd \
	-drive if=pflash,format=raw,file=Firmware/TwistedOS_VARS.fd
QEMU_COMMON = \
	-m 512M \
	$(QEMU_FW) \
	-drive file=TwistedOS.img,format=raw \
	-serial stdio
QEMU_FULL = \
	$(QEMU_COMMON) \
	-drive if=none,id=data,file=TwistedDrive.img,format=raw \
	-device virtio-blk-pci,drive=data \
	-device virtio-gpu-gl-pci \
	-display gtk,gl=on \
	-netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80 \
	-device virtio-net-pci,netdev=net0 \
	-device nec-usb-xhci,id=xhci \
	-device usb-kbd,bus=xhci.0 \
	-device usb-mouse,bus=xhci.0

all: bin build $(EFI) $(KERNEL) $(INITRAMFS) $(IMG) $(DRIVE)

clean:
	rm -rf $(BIN) $(BUILD) $(OUTPUT) *.pcap $(INIT_BIN) $(INIT2_BIN)

build:
	mkdir $(BUILD)

bin:
	mkdir $(BIN)

$(EFI): Bootloader/bootloader.cpp utils/printf.cpp utils/CommonUtils.cpp Bootloader/Console.cpp Bootloader/FileSystem.cpp Bootloader/MemoryManager.cpp
	$(CC) $(CFLAGS) -I. -I./Bootloader -I./utils -o $(BIN)$@ $^ \
		-L /usr/lib -l:libefi.a -l:libgnuefi.a

$(KERNEL): Kernel/kernel.cpp Kernel/Testing/KernelSelfTests.cpp Kernel/Layers/Dispatcher.cpp Kernel/Layers/Resource/ResourceLayer.cpp Kernel/Layers/Resource/KernelHeapManager.cpp Kernel/Layers/Resource/RamFileSystemManager.cpp Kernel/Layers/Resource/VirtualAddressSpace.cpp Kernel/Layers/Logic/ELFManager.cpp Kernel/Layers/Logic/LogicLayer.cpp Kernel/Layers/Logic/ProcessManager.cpp Kernel/Layers/Logic/Scheduler.cpp Kernel/Layers/Logic/SynchronizationManager.cpp Kernel/Layers/Translation/TranslationLayer.cpp Kernel/Memory/KernelHeapAllocations.cpp Kernel/Memory/PhysicalMemoryManager.cpp Kernel/Memory/VirtualMemoryManager.cpp Kernel/Logging/FrameBufferConsole.cpp Kernel/Arch/x86.cpp Kernel/Arch/Interrupts.asm Kernel/Arch/task.asm Kernel/Arch/syscall.asm utils/printf.cpp utils/CommonUtils.cpp Kernel/linker.ld
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/kernel.cpp -o $(BUILD)kernel.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Testing/KernelSelfTests.cpp -o $(BUILD)kernel_self_tests.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Dispatcher.cpp -o $(BUILD)dispatcher.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/ResourceLayer.cpp -o $(BUILD)resource_layer.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/KernelHeapManager.cpp -o $(BUILD)kernel_heap_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/RamFileSystemManager.cpp -o $(BUILD)ram_file_system_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/VirtualAddressSpace.cpp -o $(BUILD)virtual_address_space.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Logic/ELFManager.cpp -o $(BUILD)elf_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Logic/LogicLayer.cpp -o $(BUILD)logic_layer.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Logic/ProcessManager.cpp -o $(BUILD)process_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Logic/Scheduler.cpp -o $(BUILD)scheduler.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Logic/SynchronizationManager.cpp -o $(BUILD)synchronization_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Translation/TranslationLayer.cpp -o $(BUILD)translation_layer.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Memory/KernelHeapAllocations.cpp -o $(BUILD)kernel_heap_allocations.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Memory/PhysicalMemoryManager.cpp -o $(BUILD)physical_memory_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Memory/VirtualMemoryManager.cpp -o $(BUILD)virtual_memory_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Logging/FrameBufferConsole.cpp -o $(BUILD)framebuffer_console.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Arch/x86.cpp -o $(BUILD)x86.o
	$(KERNEL_AS) $(KERNEL_ASFLAGS) Kernel/Arch/Interrupts.asm -o $(BUILD)interrupts.o
	$(KERNEL_AS) $(KERNEL_ASFLAGS) Kernel/Arch/task.asm -o $(BUILD)task_switch.o
	$(KERNEL_AS) $(KERNEL_ASFLAGS) Kernel/Arch/syscall.asm -o $(BUILD)syscall.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c utils/printf.cpp -o $(BUILD)printf.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c utils/CommonUtils.cpp -o $(BUILD)common_utils.o
	$(KERNEL_LD) $(KERNEL_LDFLAGS) $(BUILD)kernel.o $(BUILD)kernel_self_tests.o $(BUILD)dispatcher.o $(BUILD)resource_layer.o $(BUILD)kernel_heap_manager.o $(BUILD)ram_file_system_manager.o $(BUILD)virtual_address_space.o $(BUILD)elf_manager.o $(BUILD)logic_layer.o $(BUILD)process_manager.o $(BUILD)scheduler.o $(BUILD)synchronization_manager.o $(BUILD)translation_layer.o $(BUILD)kernel_heap_allocations.o $(BUILD)physical_memory_manager.o $(BUILD)virtual_memory_manager.o $(BUILD)framebuffer_console.o $(BUILD)x86.o $(BUILD)interrupts.o $(BUILD)task_switch.o $(BUILD)syscall.o $(BUILD)printf.o $(BUILD)common_utils.o -o $(BUILD)kernel.elf
	objcopy -O binary --set-section-flags .bss=alloc,load,contents $(BUILD)kernel.elf $(BIN)$@


$(INIT_BIN): $(INIT_SRC) $(INIT_LD) | build
	$(KERNEL_CC) $(INIT_CFLAGS) -x c -c $(INIT_SRC) -o $(INIT_OBJ)
	$(KERNEL_LD) -nostdlib -static -T $(INIT_LD) $(INIT_OBJ) -o $(INIT_ELF)
	objcopy -O binary $(INIT_ELF) $(INIT_BIN)

$(INIT2_BIN): $(INIT2_SRC) | build
	$(INIT2_CC) -static -nostdlib -fno-pie -no-pie -fno-stack-protector -fno-stack-clash-protection -Wl,-e,_start $(INIT2_SRC) -o $(INIT2_BIN)

$(INITRAMFS): $(INIT_BIN) $(INIT2_BIN)
	chmod +x $(INIT_BIN) $(INIT2_BIN)
	(cd $(ROOTFS_DIR) && find . -print | cpio -o -H newc) > $(BIN)$@

$(IMG): $(EFI) $(KERNEL) $(INITRAMFS)
	rm -f $@
	dd if=/dev/zero of=$@ bs=512 count=$(SECTORS) status=none
	parted $@ --script mklabel gpt
	parted $@ --script mkpart ESP fat32 1MiB 100%
	parted $@ --script set 1 boot on
	START=$$(parted -s $@ unit s print | awk '/^ 1/ {print $$2}' | sed 's/s//'); \
	ESP=esp.fat; \
	dd if=/dev/zero of=$$ESP bs=512 count=$(SECTORS) status=none; \
	mkfs.fat -F32 $$ESP >/dev/null; \
	export MTOOLS_SKIP_CHECK=1; \
	mmd -i $$ESP ::/EFI; mmd -i $$ESP ::/EFI/BOOT; \
	mcopy -i $$ESP $(BIN)$(EFI) ::/EFI/BOOT/BOOTX64.EFI; \
	mcopy -i $$ESP $(BIN)$(KERNEL) ::/kernel.bin; \
	mcopy -i $$ESP $(BIN)$(INITRAMFS) ::/initramfs.cpio; \
	dd if=$$ESP of=$@ bs=512 seek=$$START conv=notrunc status=none; \
	rm -f $$ESP

$(DRIVE):
	@if [ ! -f $@ ]; then \
		echo "Creating 4GB TwistedDrive.img..."; \
		dd if=/dev/zero of=$@ bs=1M count=$$((4*1024)) status=progress; \
	else \
		echo "$@ already exists, skipping."; \
	fi


qemu:
	$(QEMU) $(QEMU_FULL)

qemu-debug: all
	@mkdir -p $(BUILD)
	@echo "QEMU GDB server listening on localhost:$(QEMU_GDB_PORT)"
	$(QEMU) $(QEMU_FULL) \
		-serial file:$(QEMU_DEBUG_SERIAL_LOG) \
		-gdb tcp::$(QEMU_GDB_PORT) \
		-S -no-reboot -no-shutdown

qemu-basic:
	$(QEMU) $(QEMU_COMMON)

qemu-basic-debug: all
	@mkdir -p $(BUILD)
	@echo "QEMU GDB server listening on localhost:$(QEMU_GDB_PORT)"
	$(QEMU) \
		-m 512M \
		$(QEMU_FW) \
		-drive file=TwistedOS.img,format=raw \
		-serial file:$(QEMU_DEBUG_SERIAL_LOG) \
		-gdb tcp::$(QEMU_GDB_PORT) \
		-S -no-reboot -no-shutdown

gdb-kernel: all
	$(GDB) $(BUILD)kernel.elf \
		-ex "set architecture i386:x86-64" \
		-ex "target remote localhost:$(QEMU_GDB_PORT)" \
		-ex "hbreak KernelEntry"

debug: all
	./scripts/debug-kernel.sh

ALL_SOURCE_FILES := $(shell find . -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \))

.PHONY: format qemu qemu-basic qemu-debug qemu-basic-debug gdb-kernel debug

format:
	@echo "Formatting all C/C++ files in repository..."

	@for file in $(ALL_SOURCE_FILES); do \
		echo "→ Formatting $$file"; \
		clang-format -i --style="{ \
			BasedOnStyle: llvm, \
			IndentWidth: 4, \
			TabWidth: 4, \
			UseTab: Never, \
			ColumnLimit: 200, \
			BreakBeforeBraces: Allman, \
			AllowShortIfStatementsOnASingleLine: false, \
			AllowShortLoopsOnASingleLine: false, \
			AllowShortFunctionsOnASingleLine: None, \
			AllowShortBlocksOnASingleLine: Never, \
			PointerAlignment: Left, \
			ReferenceAlignment: Left, \
			AlignOperands: true, \
			AlignConsecutiveAssignments: true, \
			AlignConsecutiveDeclarations: true, \
			AlignTrailingComments: true, \
			AlignAfterOpenBracket: Align, \
			BreakBeforeBinaryOperators: All, \
			SpaceBeforeParens: ControlStatements, \
			SpacesInParentheses: false, \
			SpacesInSquareBrackets: false, \
			SpacesInAngles: false, \
			SpaceAfterCStyleCast: true, \
			SpaceBeforeAssignmentOperators: true, \
			KeepEmptyLinesAtTheStartOfBlocks: false, \
			SortIncludes: true, \
			IncludeBlocks: Regroup, \
			NamespaceIndentation: None, \
			AccessModifierOffset: -4, \
			IndentCaseLabels: true, \
			BreakConstructorInitializersBeforeComma: false, \
			BreakInheritanceList: BeforeColon, \
			ConstructorInitializerIndentWidth: 4, \
			ContinuationIndentWidth: 8, \
			ReflowComments: true, \
			SpacesBeforeTrailingComments: 1, \
			Cpp11BracedListStyle: true \
		}" $$file; \
	done