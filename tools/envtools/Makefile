#
# (C) Copyright 2002-2006
# Wolfgang Denk, DENX Software Engineering, wd@denx.de.
#
# See file CREDITS for list of people who contributed to this
# project.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston,
# MA 02111-1307 USA
#
ifeq ($(MAKERULES),)
include ../../Rules.make
else
include $(MAKERULES)
endif

ifeq ($(PXA_SRC_PVK_UBOOT_DIR),)
PXA_SRC_PVK_UBOOT_DIR:=../preview-kit/uboot
endif

MTD_VERSION :=old
ifeq ($(MTD_VERSION),old)
CFLAGS += -DMTD_OLD
endif



CFLAGS +=-DUSE_HOSTCC -I$(PXA_SRC_PVK_UBOOT_DIR)/include

AOBJS   :=
COBJS	:=
CPPOBJS := crc32.o  fw_env.o  fw_env_main.o
SRCS	:= $(AOBJS:.o=.S) $(COBJS:.o=.c) $(CPPOBJS:.o=.cpp)
OBJS	:= $(addprefix $(obj),$(AOBJS) $(COBJS) $(CPPOBJS))


.PHONY: fw_printenv  install-host install-target clean clean-local \
	uninstall-host uninstall-target

all: fw_printenv install-host install-target 

fw_printenv: ${OBJS} 
	${CC} $(CFLAGS) $(LDFLAGS) -o $@ ${OBJS} 
	$(STRIP) $@


install-host:
	
install-target:
	$(SUDO) cp -f fw_env.config $(PXA_TARGET_FS_DIR)/etc
	$(SUDO) cp -f fw_printenv $(PXA_TARGET_BIN_DIR)
	(cd $(PXA_TARGET_BIN_DIR);if [ -f fw_setenv ];then $(SUDO) rm -fr fw_setenv;fi;$(SUDO) ln -s fw_printenv fw_setenv)
	

clean: clean-local uninstall-host uninstall-target

clean-local:
	-rm -f ${OBJS}
	-rm -f fw_printenv 

uninstall-host:

uninstall-target:
	$(SUDO) rm -f $(PXA_TARGET_BIN_DIR)/fw_saveenv
	$(SUDO) rm -f $(PXA_TARGET_BIN_DIR)/fw_printenv

