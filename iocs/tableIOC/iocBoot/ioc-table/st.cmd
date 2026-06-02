#!../../bin/linux-x86_64/tableIoc

< envPaths

dbLoadDatabase("$(TOP)/dbd/tableIoc.dbd")
tableIoc_registerRecordDeviceDriver(pdbbase) 

# Load record instances
#dbLoadRecords("../../db/tableIoc.db","user=bmartins")

iocInit()

