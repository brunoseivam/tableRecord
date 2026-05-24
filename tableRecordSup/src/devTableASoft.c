#include <stddef.h>

#include "dbAccess.h"
#include "devSup.h"
#include "epicsExport.h"
#include "tableARecord.h"

/*
 * Soft Channel device support for tableA.
 *
 * In Soft Channel mode clients write column data directly via CA/PVA to the
 * COL00..COL07 fields.  read_table is a no-op; the record simply publishes
 * whatever has been written into the column buffers.
 */

static long read_table(tableARecord *prec)
{
    (void)prec;
    return 0;
}

tableAdset devTableASoft = {
    {5, NULL, NULL, NULL, NULL},
    read_table
};
epicsExportAddress(dset, devTableASoft);
