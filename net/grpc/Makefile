#
# Makefile for the HCC Remote Procedure Call layer
#

obj-$(CONFIG_GRPC) := grpc.o

grpc-y := rpc.o comlayer.o thread_pool.o rpclayer.o monitor.o rpc_hotplug.o synchro.o

EXTRA_CFLAGS += -I$(M) -Wall -Werror

# end of file
