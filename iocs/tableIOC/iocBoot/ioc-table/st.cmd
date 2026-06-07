#!../../bin/linux-x86_64/tableIoc

< envPaths

dbLoadDatabase("$(TOP)/dbd/tableIoc.dbd")
tableIoc_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("$(TOP)/db/table.db")

iocInit()

addTableSource()

