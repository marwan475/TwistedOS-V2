
CC = x86_64-w64-mingw32-gcc

BIN = bin/
BUILD = build/
OUTPUT = TwsitedOS.img


all: bin build img

clean:
	rm -rf $(BIN) $(BUILD) $(OUTPUT)

build:
	mkdir $(BUILD)

bin:
	mkdir $(BIN)

qemu-uefi:
	qemu-system-x86_64 -bios UEFI64.bin -net none

qemu:
	qemu-system-i386 -m 512M -drive file=$(OUTPUT),format=raw,if=floppy

img: bootsector
	dd if=/dev/zero of=$(OUTPUT) bs=512 count=2880
	mkfs.fat -F 12 -n "NBOS" $(OUTPUT)
	dd if=$(BIN)bootsector.bin of=$(OUTPUT) conv=notrunc

bootsector: bootsector.asm
	nasm -f bin $^ -o $(BIN)$@.bin
