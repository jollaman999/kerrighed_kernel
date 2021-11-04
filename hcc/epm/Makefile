#
# HCC's Enhanced Process Management (EPM)
#

obj-$(CONFIG_HCC_EPM) := hcc_epm.o
obj-$(CONFIG_HCC_EPM) += application/

hcc_epm-y := epm.o \
	signal.o sighand.o children.o pid.o pidmap.o \
	action.o ghost.o network_ghost.o \
	remote_clone.o migration.o checkpoint.o restart.o \
	hotplug.o \
	procfs.o

EXTRA_CFLAGS += -Wall -Werror
