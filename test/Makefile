TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

# Use the typed RSET/DSET definitions, matching the tableRecord library
USR_CPPFLAGS += -DUSE_TYPED_RSET -DUSE_TYPED_DSET

# Access to the record's public and private headers
USR_CPPFLAGS += -I$(TOP)/include
USR_CPPFLAGS += -I$(TOP)/tableRecordApp

#==================================================
# tableInitTest: full registration (record + soft device support + stub
# read_table device supports defined in the test itself).  Covers every
# init_record check except the no-dset case.

TARGETS += $(COMMON_DIR)/tableInitTest.dbd
tableInitTest_DBD += base.dbd
tableInitTest_DBD += tableSupport.dbd
tableInitTest_DBD += tableTestDev.dbd

TESTPROD_HOST += tableInitTest
tableInitTest_SRCS += tableInitTest.cpp
tableInitTest_SRCS += tableInitTest_registerRecordDeviceDriver.cpp
tableInitTest_LIBS += tableRecord pvxsIoc pvxs $(EPICS_BASE_IOC_LIBS)
testHarness_SRCS += tableInitTest.cpp
TESTFILES += $(COMMON_DIR)/tableInitTest.dbd ../tableInitTest.db
TESTS += tableInitTest

#==================================================
# tableNoDsetTest: record type registered with NO device support, so a
# tableRecord instance ends up with a NULL dset.  Covers the no-dset check.

TARGETS += $(COMMON_DIR)/tableNoDsetTest.dbd
tableNoDsetTest_DBD += base.dbd
tableNoDsetTest_DBD += tableRecord.dbd

TESTPROD_HOST += tableNoDsetTest
tableNoDsetTest_SRCS += tableNoDsetTest.cpp
tableNoDsetTest_SRCS += tableNoDsetTest_registerRecordDeviceDriver.cpp
tableNoDsetTest_LIBS += tableRecord pvxsIoc pvxs $(EPICS_BASE_IOC_LIBS)
testHarness_SRCS += tableNoDsetTest.cpp
TESTFILES += $(COMMON_DIR)/tableNoDsetTest.dbd ../tableNoDsetTest.db
TESTS += tableNoDsetTest

#==================================================
# tablePartialTest: serves a table over PVA, subscribes a monitor, and asserts
# that unchanged columns / labels / optional-columns are NOT encoded/transported
# in the monitor delta.  Uses a controllable device support defined in the test.

TARGETS += $(COMMON_DIR)/tablePartialTest.dbd
tablePartialTest_DBD += base.dbd
tablePartialTest_DBD += tableSupport.dbd
tablePartialTest_DBD += tablePartialTestDev.dbd

TESTPROD_HOST += tablePartialTest
tablePartialTest_SRCS += tablePartialTest.cpp
tablePartialTest_SRCS += tablePartialTest_registerRecordDeviceDriver.cpp
tablePartialTest_LIBS += tableRecord pvxsIoc pvxs $(EPICS_BASE_IOC_LIBS)
testHarness_SRCS += tablePartialTest.cpp
TESTFILES += $(COMMON_DIR)/tablePartialTest.dbd ../tablePartialTest.db
TESTS += tablePartialTest

#==================================================

TESTSCRIPTS_HOST += $(TESTS:%=%.t)

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
