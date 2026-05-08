# set to the prefix of your toolchain:
#   macOS (Homebrew):      aarch64-elf-
#   Ubuntu / Debian:       aarch64-linux-gnu-
CROSS ?= aarch64-elf-

AS      = $(CROSS)as
LD      = $(CROSS)ld
OBJDUMP = $(CROSS)objdump

SRCDIR  = src

ASFLAGS = -g
LDFLAGS = -T $(SRCDIR)/link.ld

TARGET  = kernel.elf
OBJS    = $(SRCDIR)/boot.o

all: $(TARGET)

$(TARGET): $(OBJS) $(SRCDIR)/link.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(SRCDIR)/%.o: $(SRCDIR)/%.S
	$(AS) $(ASFLAGS) -o $@ $<

run: $(TARGET)
	qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -kernel $(TARGET) -semihosting

dump: $(TARGET)
	$(OBJDUMP) -d $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run dump clean
