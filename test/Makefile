obj-m += scroll_blocker.o

KDIR := /lib/modules/6.15.1-arch1-2/build

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

install:
	sudo insmod scroll_blocker.ko

uninstall:
	sudo rmmod scroll_blocker

toggle-on:
	echo "1" | sudo tee /proc/scroll_blocker

toggle-off:
	echo "0" | sudo tee /proc/scroll_blocker

status:
	cat /proc/scroll_blocker
