#
# HCC's configurable scheduler framework (SCHED)
#

obj-$(CONFIG_HCC_SCHED) := core.o info.o \
	global_lock.o string_list.o hashed_string_list.o global_config.o \
	policy.o scheduler.o \
	pipe.o remote_pipe.o \
	probe.o port.o filter.o \
	process_set.o placement.o

obj-$(CONFIG_HCC_SCHED) += probes/
obj-$(CONFIG_HCC_SCHED) += filters/
obj-$(CONFIG_HCC_SCHED) += policies/

EXTRA_CFLAGS += -Wall -Werror
