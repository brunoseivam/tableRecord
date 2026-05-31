TOP = ../..
include $(TOP)/configure/CONFIG

USR_CFLAGS += -DUSE_TYPED_RSET -DUSE_TYPED_DSET -DDUSE_TYPED_DRVET
USR_CXXFLAGS += -DUSE_TYPED_RSET -DUSE_TYPED_DSET -DDUSE_TYPED_DRVET

DBDINC += tableRecord

DBD += tableRecordSup.dbd

LIBRARY_IOC += tableRecordSup
tableRecordSup_SRCS += tableRecord.c
tableRecordSup_SRCS += devTableSoft.c
tableRecordSup_SRCS += devTableSim.c
tableRecordSup_SRCS += tableSource.cpp
tableRecordSup_SRCS += tableSourceRegistrar.cpp
tableRecordSup_LIBS += pvxsIoc pvxs $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
