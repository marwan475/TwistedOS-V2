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

BOOT_GFX_WIDTH ?= 1280
BOOT_GFX_HEIGHT ?= 1024

CFLAGS += -DBOOT_GFX_WIDTH=$(BOOT_GFX_WIDTH) -DBOOT_GFX_HEIGHT=$(BOOT_GFX_HEIGHT)

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

POSIX_SYSCALLS_CFLAGS = -fno-jump-tables -fno-tree-switch-conversion

ifeq ($(DEBUG),1)
CFLAGS += -DDEBUG_BUILD
KERNEL_CFLAGS += -DDEBUG_BUILD
endif

ifeq ($(STEST),1)
KERNEL_CFLAGS += -DSTEST_BUILD
endif

BIN = bin/
BUILD = build/
OUTPUT = TwistedOS.img

EFI = BOOTX64.EFI
KERNEL = kernel.bin
INITRAMFS = initramfs.cpio
IMG = $(OUTPUT)
ROOTFS_DIR = initramfs/rootfs
ROOTFS_BIN_DIR = $(ROOTFS_DIR)/bin
ROOTFS_BUSYBOX = $(ROOTFS_BIN_DIR)/busybox
ROOTFS_SH = $(ROOTFS_BIN_DIR)/sh
ROOTFS_LS = $(ROOTFS_BIN_DIR)/ls
ROOTFS_CAT = $(ROOTFS_BIN_DIR)/cat
ROOTFS_ECHO = $(ROOTFS_BIN_DIR)/echo
INIT2_CC = gcc
MUSL_INIT_CC ?= $(or $(shell command -v x86_64-linux-musl-gcc 2>/dev/null),$(shell command -v musl-gcc 2>/dev/null))
INIT_SRC = initramfs/init.c
INIT_BIN = $(ROOTFS_DIR)/init
TEST1_SRC = initramfs/Test1.c
TEST1_BIN = $(ROOTFS_DIR)/Test1
TEST2_SRC = initramfs/Test2.c
TEST2_BIN = $(ROOTFS_DIR)/Test2
ESP_SIZE = 64
ROOTFS_SIZE ?= 256
ROOTFS_HEADROOM_MB ?= 256
EXT2_SOURCE_ROOTFS = RootFileSystem/alpine-rootfs
EXT2_OVERLAY_DIR = RootFileSystem/overlay
ROOTFS_SOURCE_SIZE_MB := $(shell if [ -d "$(EXT2_SOURCE_ROOTFS)" ]; then size=$$(du -sm "$(EXT2_SOURCE_ROOTFS)" 2>/dev/null | awk 'NR==1{print $$1}'); echo $${size:-0}; else echo 0; fi)
ROOTFS_REQUIRED_SIZE_MB := $(shell echo $$(( $(ROOTFS_SOURCE_SIZE_MB) + $(ROOTFS_HEADROOM_MB) )))
ROOTFS_EFFECTIVE_SIZE_MB := $(shell req=$(ROOTFS_REQUIRED_SIZE_MB); min=$(ROOTFS_SIZE); if [ $$req -gt $$min ]; then echo $$req; else echo $$min; fi)
IMG_SECTORS = $(shell echo $$(( ($(ESP_SIZE) + $(ROOTFS_EFFECTIVE_SIZE_MB) + 2) * 2048 )))
ESP_SECTORS = $(shell echo $$(( $(ESP_SIZE) * 2048 )))
ROOTFS_SECTORS = $(shell echo $$(( $(ROOTFS_EFFECTIVE_SIZE_MB) * 2048 )))
GDB = gdb
QEMU_GDB_PORT = 1234
QEMU_DEBUG_SERIAL_LOG = $(BUILD)qemu-debug-serial.log
QEMU = qemu-system-x86_64
QEMU_MEMORY ?= 3072M
QEMU_FW = \
	-drive if=pflash,format=raw,readonly=on,file=Firmware/code.fd \
	-drive if=pflash,format=raw,file=Firmware/TwistedOS_VARS.fd
QEMU_COMMON = \
	-m $(QEMU_MEMORY) \
	$(QEMU_FW) \
	-serial stdio
QEMU_FULL = \
	$(QEMU_COMMON) \
	-drive if=none,id=data,file=$(IMG),format=raw \
	-device virtio-blk-pci,drive=data \
	-device virtio-gpu-gl-pci \
	-display gtk,gl=on \
	-netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80 \
	-device virtio-net-pci,netdev=net0 \
	-device nec-usb-xhci,id=xhci \
	-device usb-kbd,bus=xhci.0 \
	-device usb-mouse,bus=xhci.0

all: bin build $(EFI) $(KERNEL) $(INITRAMFS) $(IMG)

clean:
	rm -rf $(BIN) $(BUILD) $(OUTPUT) *.pcap $(INIT_BIN) $(TEST1_BIN) $(TEST2_BIN) 

build:
	mkdir $(BUILD)

bin:
	mkdir $(BIN)

$(EFI): Bootloader/bootloader.cpp utils/printf.cpp utils/CommonUtils.cpp Bootloader/Console.cpp Bootloader/FileSystem.cpp Bootloader/MemoryManager.cpp
	$(CC) $(CFLAGS) -I. -I./Bootloader -I./utils -o $(BIN)$@ $^ \
		-L /usr/lib -l:libefi.a -l:libgnuefi.a

$(KERNEL): Kernel/kernel.cpp Kernel/Testing/KernelSelfTests.cpp Kernel/Layers/Dispatcher.cpp Kernel/Layers/Resource/ResourceLayer.cpp Kernel/Layers/Resource/FrameBuffer.cpp Kernel/Layers/Resource/TTY.cpp Kernel/Layers/Resource/Keyboard.cpp Kernel/Layers/Resource/KernelHeapManager.cpp Kernel/Layers/Resource/RamFileSystemManager.cpp Kernel/Layers/Resource/VirtualAddressSpace.cpp Kernel/Layers/Resource/DeviceManager.cpp Kernel/Layers/Resource/PartitionManager.cpp Kernel/Layers/Resource/ExtendedFileSystemManager.cpp Kernel/Layers/Resource/Drivers/IDEController.cpp Kernel/Layers/Logic/ELFManager.cpp Kernel/Layers/Logic/InterProcessComunicationManager.cpp Kernel/Layers/Logic/LogicLayer.cpp Kernel/Layers/Logic/ProcessManager.cpp Kernel/Layers/Logic/Scheduler.cpp Kernel/Layers/Logic/SynchronizationManager.cpp Kernel/Layers/Logic/VirtualFileSystem.cpp Kernel/Layers/Translation/TranslationLayer.cpp Kernel/Layers/Translation/POSIX_SystemCalls.cpp Kernel/Memory/KernelHeapAllocations.cpp Kernel/Memory/PhysicalMemoryManager.cpp Kernel/Memory/VirtualMemoryManager.cpp Kernel/Logging/FrameBufferConsole.cpp Kernel/Arch/x86.cpp Kernel/Arch/Interrupts.asm Kernel/Arch/task.asm Kernel/Arch/syscall.asm utils/printf.cpp utils/CommonUtils.cpp Kernel/linker.ld
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/kernel.cpp -o $(BUILD)kernel.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Testing/KernelSelfTests.cpp -o $(BUILD)kernel_self_tests.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Dispatcher.cpp -o $(BUILD)dispatcher.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/ResourceLayer.cpp -o $(BUILD)resource_layer.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/FrameBuffer.cpp -o $(BUILD)frame_buffer.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/TTY.cpp -o $(BUILD)tty.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/Keyboard.cpp -o $(BUILD)keyboard.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/KernelHeapManager.cpp -o $(BUILD)kernel_heap_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/RamFileSystemManager.cpp -o $(BUILD)ram_file_system_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/VirtualAddressSpace.cpp -o $(BUILD)virtual_address_space.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/DeviceManager.cpp -o $(BUILD)device_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/PartitionManager.cpp -o $(BUILD)partition_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/ExtendedFileSystemManager.cpp -o $(BUILD)extended_file_system_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Resource/Drivers/IDEController.cpp -o $(BUILD)ide_controller.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Logic/ELFManager.cpp -o $(BUILD)elf_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Logic/InterProcessComunicationManager.cpp -o $(BUILD)inter_process_comunication_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Logic/LogicLayer.cpp -o $(BUILD)logic_layer.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Logic/ProcessManager.cpp -o $(BUILD)process_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Logic/Scheduler.cpp -o $(BUILD)scheduler.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Logic/SynchronizationManager.cpp -o $(BUILD)synchronization_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Logic/VirtualFileSystem.cpp -o $(BUILD)virtual_file_system.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Translation/TranslationLayer.cpp -o $(BUILD)translation_layer.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(POSIX_SYSCALLS_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Layers/Translation/POSIX_SystemCalls.cpp -o $(BUILD)posix_system_calls.o
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
	$(KERNEL_LD) $(KERNEL_LDFLAGS) $(BUILD)kernel.o $(BUILD)kernel_self_tests.o $(BUILD)dispatcher.o $(BUILD)resource_layer.o $(BUILD)frame_buffer.o $(BUILD)tty.o $(BUILD)keyboard.o $(BUILD)kernel_heap_manager.o $(BUILD)ram_file_system_manager.o $(BUILD)virtual_address_space.o $(BUILD)device_manager.o $(BUILD)partition_manager.o $(BUILD)extended_file_system_manager.o $(BUILD)ide_controller.o $(BUILD)elf_manager.o $(BUILD)inter_process_comunication_manager.o $(BUILD)logic_layer.o $(BUILD)process_manager.o $(BUILD)scheduler.o $(BUILD)synchronization_manager.o $(BUILD)virtual_file_system.o $(BUILD)translation_layer.o $(BUILD)posix_system_calls.o $(BUILD)kernel_heap_allocations.o $(BUILD)physical_memory_manager.o $(BUILD)virtual_memory_manager.o $(BUILD)framebuffer_console.o $(BUILD)x86.o $(BUILD)interrupts.o $(BUILD)task_switch.o $(BUILD)syscall.o $(BUILD)printf.o $(BUILD)common_utils.o -o $(BUILD)kernel.elf
	objcopy -O binary --set-section-flags .bss=alloc,load,contents $(BUILD)kernel.elf $(BIN)$@


$(INIT_BIN): $(INIT_SRC) | build
	@if [ -z "$(MUSL_INIT_CC)" ]; then \
		echo "Error: musl compiler not found. Install x86_64-linux-musl-gcc (or musl-gcc), or set MUSL_INIT_CC=<path>." >&2; \
		exit 1; \
	fi
	$(MUSL_INIT_CC) -static -nostartfiles -fno-pie -no-pie -fno-stack-protector -fno-stack-clash-protection -Wl,-e,_start $(INIT_SRC) -o $(INIT_BIN)
	@if readelf -l $(INIT_BIN) | grep -q 'Requesting program interpreter'; then \
		echo "Error: $(INIT_BIN) is dynamically linked; expected static binary." >&2; \
		exit 1; \
	fi

$(TEST1_BIN): $(TEST1_SRC) | build
	$(INIT2_CC) -static -nostdlib -fno-pie -no-pie -fno-stack-protector -fno-stack-clash-protection -Wl,-e,_start $(TEST1_SRC) -o $(TEST1_BIN)

$(TEST2_BIN): $(TEST2_SRC) | build
	$(INIT2_CC) -static -nostdlib -fno-pie -no-pie -fno-stack-protector -fno-stack-clash-protection -Wl,-e,_start $(TEST2_SRC) -o $(TEST2_BIN)

busybox-rootfs: scripts/busyboxconfig.sh
	@if [ -x "$(ROOTFS_BUSYBOX)" ]; then \
		echo "BusyBox already exists at $(ROOTFS_BUSYBOX), skipping build."; \
	else \
		echo "BusyBox not found at $(ROOTFS_BUSYBOX), building..."; \
		BUSYBOX_DEBUG=$(DEBUG) bash ./scripts/busyboxconfig.sh; \
	fi

$(INITRAMFS): $(INIT_BIN) $(TEST1_BIN) $(TEST2_BIN) busybox-rootfs $(ROOTFS_BUSYBOX) $(ROOTFS_SH) $(ROOTFS_LS) $(ROOTFS_CAT) $(ROOTFS_ECHO)
	chmod +x $(INIT_BIN) $(TEST1_BIN) $(TEST2_BIN)
	(cd $(ROOTFS_DIR) && find . -print | cpio -o -H newc) > $(BIN)$@

$(IMG): $(EFI) $(KERNEL) $(INITRAMFS)
	rm -f $@
	@echo "Using ROOTFS size $(ROOTFS_EFFECTIVE_SIZE_MB) MiB (source: $(ROOTFS_SOURCE_SIZE_MB) MiB, headroom: $(ROOTFS_HEADROOM_MB) MiB, minimum: $(ROOTFS_SIZE) MiB)"
	dd if=/dev/zero of=$@ bs=512 count=$(IMG_SECTORS) status=none
	parted $@ --script mklabel gpt
	parted $@ --script mkpart ESP fat32 1MiB $$((1 + $(ESP_SIZE)))MiB
	parted $@ --script set 1 boot on
	parted $@ --script set 1 esp on
	parted $@ --script mkpart ROOTFS ext2 $$((1 + $(ESP_SIZE)))MiB 100%
	START=$$(parted -s $@ unit s print | awk '/^ 1/ {print $$2}' | sed 's/s//'); \
	ROOTFS_START=$$(parted -s $@ unit s print | awk '/^ 2/ {print $$2}' | sed 's/s//'); \
	ESP=esp.fat; \
	ROOTFS=rootfs.ext2; \
	dd if=/dev/zero of=$$ESP bs=512 count=$(ESP_SECTORS) status=none; \
	mkfs.fat -F32 $$ESP >/dev/null; \
	export MTOOLS_SKIP_CHECK=1; \
	mmd -i $$ESP ::/EFI; mmd -i $$ESP ::/EFI/BOOT; \
	mcopy -i $$ESP $(BIN)$(EFI) ::/EFI/BOOT/BOOTX64.EFI; \
	mcopy -i $$ESP $(BIN)$(KERNEL) ::/kernel.bin; \
	mcopy -i $$ESP $(BIN)$(INITRAMFS) ::/initramfs.cpio; \
	dd if=/dev/zero of=$$ROOTFS bs=512 count=$(ROOTFS_SECTORS) status=none; \
	if command -v mke2fs >/dev/null 2>&1 && mke2fs -F -t ext2 -m 0 -d $(EXT2_SOURCE_ROOTFS) $$ROOTFS >/dev/null 2>&1; then \
		echo "Populated EXT2 rootfs via mke2fs -d"; \
	elif mkfs.ext2 -F -m 0 -d $(EXT2_SOURCE_ROOTFS) $$ROOTFS >/dev/null 2>&1; then \
		echo "Populated EXT2 rootfs via mkfs.ext2 -d"; \
	else \
		echo "mke2fs/mkfs.ext2 -d unavailable, falling back to debugfs population"; \
		mkfs.ext2 -F -m 0 $$ROOTFS >/dev/null; \
		bash ./scripts/populate-ext2-rootfs.sh $$ROOTFS $(EXT2_SOURCE_ROOTFS); \
	fi; \
	bash ./scripts/apply-ext2-overlay.sh $$ROOTFS $(EXT2_OVERLAY_DIR); \
	dd if=$$ESP of=$@ bs=512 seek=$$START conv=notrunc status=none; \
	dd if=$$ROOTFS of=$@ bs=512 seek=$$ROOTFS_START conv=notrunc status=none; \
	rm -f $$ESP $$ROOTFS

qemu: 
	$(QEMU) $(QEMU_FULL)

qemu-debug qemu-basic-debug gdb-kernel debug: DEBUG=1

qemu-debug: all
	@mkdir -p $(BUILD)
	@echo "QEMU GDB server listening on localhost:$(QEMU_GDB_PORT)"
	$(QEMU) $(QEMU_FULL) \
		-serial file:$(QEMU_DEBUG_SERIAL_LOG) \
		-gdb tcp::$(QEMU_GDB_PORT) \
		-S -no-reboot -no-shutdown

qemu-basic: 
	$(QEMU) $(QEMU_COMMON) -drive file=$(IMG),format=raw

qemu-basic-debug: all
	@mkdir -p $(BUILD)
	@echo "QEMU GDB server listening on localhost:$(QEMU_GDB_PORT)"
	$(QEMU) \
		-m $(QEMU_MEMORY) \
		$(QEMU_FW) \
		-drive file=$(IMG),format=raw \
		-serial file:$(QEMU_DEBUG_SERIAL_LOG) \
		-gdb tcp::$(QEMU_GDB_PORT) \
		-S -no-reboot -no-shutdown

gdb-kernel: all
	$(GDB) $(BUILD)kernel.elf \
		-ex "set architecture i386:x86-64" \
		-ex "target remote localhost:$(QEMU_GDB_PORT)" \
		-ex "hbreak KernelEntry"

debug:
	./scripts/debug-kernel.sh

ALL_SOURCE_FILES := $(shell find . \
	\( -path './busybox' -o -path './RootFileSystem/alpine-rootfs' -o -path './build' -o -path './bin' \) -prune -o \
	-type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print 2>/dev/null)

.PHONY: format qemu qemu-basic qemu-debug qemu-basic-debug gdb-kernel debug busybox-rootfs

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