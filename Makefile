# CROSS — set to the prefix of your toolchain:
#   macOS (Homebrew):      aarch64-elf-
#   Ubuntu / Debian:       aarch64-linux-gnu-
CROSS ?= aarch64-elf-

AS      = $(CROSS)as
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJDUMP = $(CROSS)objdump

SRCDIR  = src

ASFLAGS = -g
CFLAGS  = -g -ffreestanding -nostdlib -mcpu=cortex-a53 -O2
LDFLAGS = -T $(SRCDIR)/link.ld

TARGET  = kernel.elf
OBJS    = $(SRCDIR)/boot.o $(SRCDIR)/kernel.o

all: $(TARGET)

$(TARGET): $(OBJS) $(SRCDIR)/link.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)


$(SRCDIR)/%.o: $(SRCDIR)/%.S
	$(AS) $(ASFLAGS) -o $@ $<

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a53 \
		-nographic \
		-kernel $(TARGET)

gdb: $(TARGET)
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a53 \
		-nographic \
		-kernel $(TARGET) \
		-S -gdb tcp::1234

dump: $(TARGET)
	$(OBJDUMP) -d $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run gdb dump clean