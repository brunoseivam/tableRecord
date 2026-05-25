#!../../bin/linux-x86_64/tableRecord

dbLoadDatabase("../../dbd/tableRecord.dbd", 0, 0)
tableRecord_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/tableA_demo.db")
dbLoadRecords("../../db/tableB_demo.db")

iocInit()

addTableSource()
