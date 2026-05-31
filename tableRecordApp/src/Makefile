# SPDX-FileCopyrightText: 2003 Argonne National Laboratory
#
# SPDX-License-Identifier: EPICS

TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CFLAGS += -DUSE_TYPED_RSET -DUSE_TYPED_DSET -DDUSE_TYPED_DRVET

#=============================
# Build the IOC application

PROD_IOC = table
# table.dbd will be created and installed
DBD += table.dbd

# table.dbd will be made up from these files:
table_DBD += base.dbd
table_DBD += tableRecordSup.dbd

# Include dbd files from all support applications:
#table_DBD += xxx.dbd

# Add all the support libraries needed by this IOC
#table_LIBS += xxx

# table_registerRecordDeviceDriver.cpp derives from table.dbd
table_SRCS += table_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
table_SRCS_DEFAULT += tableRecordMain.cpp
table_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#table_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
table_LIBS += tableRecordSup pvxsIoc pvxs $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
