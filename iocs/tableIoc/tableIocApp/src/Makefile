# SPDX-FileCopyrightText: 2003 Argonne National Laboratory
#
# SPDX-License-Identifier: EPICS

TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

PROD_IOC = tableIoc
# tableIoc.dbd will be created and installed
DBD += tableIoc.dbd

# tableIoc.dbd will be made up from these files:
tableIoc_DBD += base.dbd

# Include dbd files from all support applications:
#tableIoc_DBD += xxx.dbd

# Add all the support libraries needed by this IOC
#tableIoc_LIBS += xxx

# tableIoc_registerRecordDeviceDriver.cpp derives from tableIoc.dbd
tableIoc_SRCS += tableIoc_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
tableIoc_SRCS_DEFAULT += tableIocMain.cpp
tableIoc_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#tableIoc_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
tableIoc_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

