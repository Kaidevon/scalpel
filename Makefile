obj-m := scalpel.o
scalpel-y := scalpel_main.o hide/hide_inode.o hide/hide_module.o ksym_get.o memrw/memrw.o memrw/vmem_rw.o

ifdef obj
$(obj)/scalpel_main.o: $(src)/scalpel.c FORCE
	$(call if_changed,cc_o_c)
endif

ccflags-y := -I$(src) -I$(src)/hide -I$(src)/memrw

all:
	$(MAKE) -C $(KDIR) O=$(O_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) O=$(O_DIR) M=$(PWD) clean

.PHONY: all clean
