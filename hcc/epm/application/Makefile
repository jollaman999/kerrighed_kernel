#
# HCC's Enhanced Process Management (EPM) - Application
#

obj-$(CONFIG_HCC_EPM) := hcc_epm_application.o

hcc_epm_application-y := app_shared.o \
	app_frontier.o application.o \
	app_checkpoint.o app_restart.o \
	application_cr_api.o

EXTRA_CFLAGS += -Wall -Werror
