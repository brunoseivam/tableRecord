TOP = ..
include $(TOP)/configure/CONFIG

USR_CFLAGS   += -DUSE_TYPED_RSET -DUSE_TYPED_DSET -DDUSE_TYPED_DRVET
USR_CXXFLAGS += -DUSE_TYPED_RSET -DUSE_TYPED_DSET -DDUSE_TYPED_DRVET

DBINC += tableRecord

DBD += tableRecord.dbd

LIBRARY_IOC += tableRecord
tableRecord_SRCS += tableRecord.c
tableRecord_SRCS += devTableSoft.c
tableRecord_SRCS += tableSource.cpp
tableRecord_SRCS += tableSourceRegistrar.cpp
tableRecord_LIBS += tableRecord pvxsIoc pvxs $(EPICS_BASE_IOC_LIBS)


include $(TOP)/configure/RULES
