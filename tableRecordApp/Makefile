TOP = ..
include $(TOP)/configure/CONFIG

USR_CFLAGS   += -DUSE_TYPED_RSET -DUSE_TYPED_DSET -DDUSE_TYPED_DRVET
USR_CXXFLAGS += -DUSE_TYPED_RSET -DUSE_TYPED_DSET -DDUSE_TYPED_DRVET

DBDINC += tableRecord

DBD += tableSupport.dbd

LIBRARY_IOC += tableRecord

tableRecord_SRCS += tableRecord.cpp
tableRecord_SRCS += tableVStr.c
tableRecord_SRCS += tableRecordUtil.cpp
tableRecord_SRCS += devTableSoft.cpp
tableRecord_SRCS += tableSource.cpp
tableRecord_SRCS += tableSourceRegistrar.cpp

tableRecord_LIBS += pvxsIoc pvxs $(EPICS_BASE_IOC_LIBS)

INC += tableRecordUtil.h
INC += tableVStr.h

# Reference documentation, generated from tableRecord.dbd.pod
HTMLS_DIR = .
DOCS  += tableRecord.md
HTMLS += tableRecord.html

include $(TOP)/configure/RULES
