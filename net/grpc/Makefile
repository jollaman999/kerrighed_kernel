#
# Makefile for the HCC Remote Procedure Call layer
#

obj-$(CONFIG_GRPC) := grpc.o

grpc-y := grpc.o comlayer.o thread_pool.o grpclayer.o monitor.o grpc_hotplug.o synchro.o

EXTRA_CFLAGS += -I$(M) -Wall -Werror

# end of file
