TOP = ../..
include $(TOP)/configure/CONFIG

USR_CFLAGS += -DUSE_TYPED_RSET -DUSE_TYPED_DSET -DDUSE_TYPED_DRVET
USR_CXXFLAGS += -DUSE_TYPED_RSET -DUSE_TYPED_DSET -DDUSE_TYPED_DRVET

DBDINC += tableARecord
DBDINC += tableBRecord

DBD += tableRecordSup.dbd

LIBRARY_IOC += tableRecordSup
tableRecordSup_SRCS += tableARecord.c
tableRecordSup_SRCS += devTableASoft.c
tableRecordSup_SRCS += devTableASim.c
tableRecordSup_SRCS += tableBRecord.c
tableRecordSup_SRCS += devTableBSoft.c
tableRecordSup_SRCS += devTableBSim.c
tableRecordSup_SRCS += tableSource.cpp
tableRecordSup_SRCS += tableSourceRegistrar.cpp
tableRecordSup_LIBS += pvxsIoc pvxs $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
