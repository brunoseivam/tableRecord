# SPDX-FileCopyrightText: 2003 Argonne National Laboratory
#
# SPDX-License-Identifier: EPICS

TOP=../..

include $(TOP)/configure/CONFIG

PROD_IOC = tableIoc

DBD += $(PROD_IOC).dbd
$(PROD_IOC)_DBD += base.dbd
$(PROD_IOC)_DBD += tableSupport.dbd

$(PROD_IOC)_SRCS += $(PROD_IOC)_registerRecordDeviceDriver.cpp
$(PROD_IOC)_SRCS += $(PROD_IOC)Main.cpp

$(PROD_IOC)_LIBS += tableRecord
$(PROD_IOC)_LIBS += pvxsIoc pvxs
$(PROD_IOC)_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES

