#
# Makefile for the HCC Remote Procedure Call layer
#

obj-y := hccinit.o hccsyms.o hcc_tools.o hcc_syscalls.o procfs.o node_discovering.o 

obj-$(CONFIG_HCC_HOTPLUG) += add.o cluster.o failure.o hooks.o hotplug.o hotplug_notifier.o membership.o namespace.o
obj-$(CONFIG_HCC_HOTPLUG_DEL) += remove.o
obj-$(CONFIG_HCC_HOTPLUG_XCH) += replace.o

EXTRA_CFLAGS += -I$(M) -Wall -Werror

# end of file
