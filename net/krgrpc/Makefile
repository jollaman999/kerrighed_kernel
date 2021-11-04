#
# Makefile for the HCC Remote Procedure Call layer
#

obj-$(CONFIG_HCCRPC) := hccrpc.o

hccrpc-y := rpc.o comlayer.o thread_pool.o rpclayer.o monitor.o rpc_hotplug.o synchro.o

EXTRA_CFLAGS += -I$(M) -Wall -Werror

# end of file
