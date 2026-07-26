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
C_OBJS  := $(filter-out build/src/kernel/heap.o, $(C_OBJS))
S_OBJS  := $(patsubst %.S, build/%.o, $(S_SRCS))
OBJS    := $(C_OBJS) $(S_OBJS)

RUST_DIR    := rust
RUST_TARGET := aarch64-unknown-none-softfloat
RUST_LIB    := $(RUST_DIR)/target/$(RUST_TARGET)/release/libpurgatory_rs.a

TARGET  := kernel.elf

.PHONY: all run gdb dump clean

all: $(TARGET)

$(RUST_LIB): $(shell find $(RUST_DIR)/src -name '*.rs' 2>/dev/null) \
             $(RUST_DIR)/Cargo.toml \
             $(RUST_DIR)/.cargo/config.toml
	cd $(RUST_DIR) && cargo +nightly build --release


$(TARGET): $(OBJS) $(RUST_LIB) src/link.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(RUST_LIB)

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
