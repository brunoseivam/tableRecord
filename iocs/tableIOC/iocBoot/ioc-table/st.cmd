#!../../bin/linux-x86_64/tableIoc

< envPaths

dbLoadDatabase("$(TOP)/dbd/tableIoc.dbd")
tableIoc_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("$(TOP)/db/table.db")
dbLoadRecords("$(TOP)/db/tableCsv.db")

iocInit()

addTableSource()

