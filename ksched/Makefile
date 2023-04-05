#/usr/src/linux-raspi-headers-5.19.0-1015/include
KDIR ?= /lib/modules/$(shell uname -r)/build
BUILD_DIR ?= $(PWD)/build
BUILD_DIR_MAKEFILE ?= $(PWD)/build/Makefile


LDFLAGS += -L/usr/lib/aarch64-linux-gnu

default: $(BUILD_DIR_MAKEFILE)
	make -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) modules LDFLAGS="$(LDFLAGS)"

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR_MAKEFILE): $(BUILD_DIR)
	touch "$@"

clean:
	make -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) clean
