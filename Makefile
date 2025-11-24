
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
	-fno-rtti

BIN = bin/
BUILD = build/
OUTPUT = TwistedOS.img

EFI = BOOTX64.EFI
IMG = $(OUTPUT)
DRIVE = TwistedDrive.img
ESP_SIZE = 64
SECTORS = $(shell echo $$(( $(ESP_SIZE) * 2048 )))

all: bin build $(IMG) $(DRIVE)

clean:
	rm -rf $(BIN) $(BUILD) $(OUTPUT) *.pcap

build:
	mkdir $(BUILD)

bin:
	mkdir $(BIN)

$(EFI): Bootloader/bootloader.cpp utils/printf.cpp Bootloader/Console.cpp
	$(CC) $(CFLAGS) -I. -I./Bootloader -I./utils -o $(BIN)$@ $^ \
		-L /usr/lib -l:libefi.a -l:libgnuefi.a

$(IMG): $(EFI)
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
	dd if=$$ESP of=$@ bs=512 seek=$$START conv=notrunc status=none; \
	rm -f $$ESP

$(DRIVE):
	@if [ ! -f $@ ]; then \
		echo "Creating 4GB TwistedDrive.img..."; \
		dd if=/dev/zero of=$@ bs=1M count=$$((4*1024)) status=progress; \
	else \
		echo "$@ already exists, skipping."; \
	fi

qemu-uefi:
	qemu-system-x86_64 -bios UEFI64.bin -drive file=TwistedOS.img,format=raw -drive if=none,id=data,file=TwistedDrive.img,format=raw -device virtio-blk-pci,drive=data -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80 -object filter-dump,id=f1,netdev=net0,file=netdump.pcap -device virtio-net-pci,netdev=net0

qemu-gl:
	qemu-system-x86_64 -bios UEFI64.bin -drive file=TwistedOS.img,format=raw -drive if=none,id=data,file=TwistedDrive.img,format=raw -device virtio-blk-pci,drive=data -device virtio-gpu-gl-pci -display gtk,gl=on -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80 -object filter-dump,id=f1,netdev=net0,file=netdump.pcap -device virtio-net-pci,netdev=net0

#qemu-system-x86_64 -bios UEFI64.bin -net none   -drive file=TwistedOS.img,format=raw -device virtio-gpu-pci -display gtk -full-screen
#ATI Rage 128 Pro ati-vga

# Find all C/C++ source and header files in the repo
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
			ColumnLimit: 100, \
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


