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

PROD_IOC = tableRecord
# tableRecord.dbd will be created and installed
DBD += tableRecord.dbd

# tableRecord.dbd will be made up from these files:
tableRecord_DBD += base.dbd
tableRecord_DBD += tableRecordSup.dbd

# Include dbd files from all support applications:
#tableRecord_DBD += xxx.dbd

# Add all the support libraries needed by this IOC
#tableRecord_LIBS += xxx

# tableRecord_registerRecordDeviceDriver.cpp derives from tableRecord.dbd
tableRecord_SRCS += tableRecord_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
tableRecord_SRCS_DEFAULT += tableRecordMain.cpp
tableRecord_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#tableRecord_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
tableRecord_LIBS += tableRecordSup pvxsIoc pvxs $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
