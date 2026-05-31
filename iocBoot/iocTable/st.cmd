#!../../bin/linux-x86_64/table

dbLoadDatabase("../../dbd/table.dbd", 0, 0)
table_registerRecordDeviceDriver(pdbbase)

dbLoadRecords("../../db/table_demo.db")

iocInit()

addTableSource()
