#!../../bin/linux-x86_64/tableIoc

#- SPDX-FileCopyrightText: 2005 Argonne National Laboratory
#-
#- SPDX-License-Identifier: EPICS

#- You may have to change tableIoc to something else
#- everywhere it appears in this file

#< envPaths

## Register all support components
dbLoadDatabase "../../dbd/tableIoc.dbd"
tableIoc_registerRecordDeviceDriver(pdbbase) 

## Load record instances
#dbLoadRecords("../../db/tableIoc.db","user=bmartins")

iocInit()

## Start any sequence programs
#seq snctableIoc,"user=bmartins"
