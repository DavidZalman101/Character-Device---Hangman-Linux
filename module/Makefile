obj-m += hangman.o

PWD := $(CURDIR)

EXTRA_CFLAGS += -DDEBUG

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
