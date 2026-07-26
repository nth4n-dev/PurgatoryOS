# CROSS — set to the prefix of your toolchain:
#   macOS (Homebrew):      aarch64-elf-
#   Ubuntu / Debian:       aarch64-linux-gnu-
CROSS   ?= aarch64-elf-

AS      := $(CROSS)as
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJDUMP := $(CROSS)objdump

ASFLAGS := -g
CFLAGS  := -g -ffreestanding -nostdlib -mcpu=cortex-a53 -O2 -I include
LDFLAGS := -T src/link.ld

C_SRCS  := $(shell find src -name '*.c' 2>/dev/null)
S_SRCS  := $(shell find src -name '*.S' 2>/dev/null)

C_OBJS  := $(patsubst %.c, build/%.o, $(C_SRCS))
S_OBJS  := $(patsubst %.S, build/%.o, $(S_SRCS))
OBJS    := $(C_OBJS) $(S_OBJS)

TARGET  := kernel.elf

.PHONY: all run gdb dump clean

all: $(TARGET)

$(TARGET): $(OBJS) src/link.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

build/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

run: $(TARGET)
	qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -kernel $(TARGET)

gdb: $(TARGET)
	qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -kernel $(TARGET) -S -gdb tcp::1234

dump: $(TARGET)
	$(OBJDUMP) -d $(TARGET)

clean:
	rm -rf build/ $(TARGET)
