obj-m += tg_dev.o

DIR ?= $(HOME)/Workspace/LinuxMIPT-2026/root/lib/modules/6.18.8/build

all:
	make -C $(DIR) M=$(PWD) modules

clean:
	make -C $(DIR) M=$(PWD) clean

