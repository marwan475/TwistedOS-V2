CC = x86_64-w64-mingw32-g++ \
		-Wl,--subsystem,10 \
		-e efi_main

LD = x86_64-w64-mingw32-ld

CFLAGS = \
	-Wall \
	-Wextra \
	-Wpedantic \
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
    -O0 \
    -Wall \
    -Wextra

KERNEL_LDFLAGS = \
    -nostdlib \
    -e kernel_main \
    -T Kernel/linker.ld

KERNEL_ASFLAGS = \
	-f elf64

BIN = bin/
BUILD = build/
OUTPUT = TwistedOS.img

EFI = BOOTX64.EFI
KERNEL = kernel.bin
IMG = $(OUTPUT)
DRIVE = TwistedDrive.img
ESP_SIZE = 64
SECTORS = $(shell echo $$(( $(ESP_SIZE) * 2048 )))

all: bin build $(EFI) $(KERNEL) $(IMG) $(DRIVE)

clean:
	rm -rf $(BIN) $(BUILD) $(OUTPUT) *.pcap

build:
	mkdir $(BUILD)

bin:
	mkdir $(BIN)

$(EFI): Bootloader/bootloader.cpp utils/printf.cpp utils/CommonUtils.cpp Bootloader/Console.cpp Bootloader/FileSystem.cpp Bootloader/MemoryManager.cpp
	$(CC) $(CFLAGS) -I. -I./Bootloader -I./utils -o $(BIN)$@ $^ \
		-L /usr/lib -l:libefi.a -l:libgnuefi.a

$(KERNEL): Kernel/kernel.cpp Kernel/PhysicalMemoryManager.cpp Kernel/Logging/FrameBufferConsole.cpp Kernel/Arch/x86.cpp Kernel/Arch/Interrupts.asm utils/printf.cpp utils/CommonUtils.cpp Kernel/linker.ld
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/kernel.cpp -o $(BUILD)kernel.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/PhysicalMemoryManager.cpp -o $(BUILD)physical_memory_manager.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Logging/FrameBufferConsole.cpp -o $(BUILD)framebuffer_console.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c Kernel/Arch/x86.cpp -o $(BUILD)x86.o
	$(KERNEL_AS) $(KERNEL_ASFLAGS) Kernel/Arch/Interrupts.asm -o $(BUILD)interrupts.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c utils/printf.cpp -o $(BUILD)printf.o
	$(KERNEL_CC) $(KERNEL_CFLAGS) -I./Kernel -I./Bootloader -I./utils -c utils/CommonUtils.cpp -o $(BUILD)common_utils.o
	$(KERNEL_LD) $(KERNEL_LDFLAGS) $(BUILD)kernel.o $(BUILD)physical_memory_manager.o $(BUILD)framebuffer_console.o $(BUILD)x86.o $(BUILD)interrupts.o $(BUILD)printf.o $(BUILD)common_utils.o -o $(BUILD)kernel.elf
	objcopy -O binary --set-section-flags .bss=alloc,load,contents $(BUILD)kernel.elf $(BIN)$@

$(IMG): $(EFI) $(KERNEL)
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
	qemu-system-x86_64 -m 512M \
	-drive if=pflash,format=raw,readonly=on,file=Firmware/code.fd \
	-drive if=pflash,format=raw,file=Firmware/TwistedOS_VARS.fd \
	-drive file=TwistedOS.img,format=raw \
	-drive if=none,id=data,file=TwistedDrive.img,format=raw \
	-device virtio-blk-pci,drive=data \
	-device virtio-gpu-gl-pci \
	-display gtk,gl=on \
	-serial stdio \
	-netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80 \
	-device virtio-net-pci,netdev=net0 \
	-device nec-usb-xhci,id=xhci \
	-device usb-kbd,bus=xhci.0 \
	-device usb-mouse,bus=xhci.0

qemu-basic:
	qemu-system-x86_64 -m 512M \
	-drive if=pflash,format=raw,readonly=on,file=Firmware/code.fd \
	-drive if=pflash,format=raw,file=Firmware/TwistedOS_VARS.fd \
	-drive file=TwistedOS.img,format=raw \
	-serial stdio

ALL_SOURCE_FILES := $(shell find . -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \))

.PHONY: format

format:
	@echo "Formatting all C/C++ files in repository..."

	@for file in $(ALL_SOURCE_FILES); do \
		echo "→ Formatting $$file"; \
		clang-format -i --style="{ \
			BasedOnStyle: llvm, \
			IndentWidth: 4, \
			TabWidth: 4, \
			UseTab: Never, \
			ColumnLimit: 120, \
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