obj-m := led_button.o
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

.PHONY: all modules_install clean
all:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) clean
