ifneq ($(KERNELRELEASE),)
obj-m := ticket.o
else
KDIR ?= /lib/modules/$(shell uname -r)/build

all: module client autoclient

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

client: client.c
	gcc -Wall -o $@ $<

autoclient: autoclient.c
	gcc -Wall -o $@ $<

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f client autoclient

.PHONY: all module clean
endif
