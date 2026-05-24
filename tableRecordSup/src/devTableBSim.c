#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dbAccess.h"
#include "dbFldTypes.h"
#include "devSup.h"
#include "epicsStdio.h"
#include "epicsExport.h"
#include "tableBRecord.h"

/*
 * Random Sim device support for tableB.
 *
 * Fills every active column with MAXROWS random elements typed by COLiFTVL.
 */

typedef struct { epicsEnum16 *ftvl; epicsUInt32 *nelm;
                 epicsUInt32 *nord; void **val; epicsUInt8 *chgd; } ColB;

#define COLB(n) { &prec->col##n##ftvl, &prec->col##n##nelm, \
                  &prec->col##n##nord, &prec->col##n##val, &prec->col##n##chgd }

static long read_table(tableBRecord *prec)
{
    ColB cols[] = {
        COLB(00), COLB(01), COLB(02), COLB(03),
        COLB(04), COLB(05), COLB(06), COLB(07)
    };
    epicsUInt32 col, row;

    for (col = 0; col < prec->ncol && col < 8; col++) {
        epicsEnum16 ftvl = *cols[col].ftvl;
        void *buf        = *cols[col].val;
        epicsUInt32 nelm = *cols[col].nelm ? *cols[col].nelm : prec->maxrows;

        if (!buf) continue;

        for (row = 0; row < nelm; row++) {
            long rval = random();
            switch (ftvl) {
            case DBF_STRING: {
                char *s = (char *)buf + row * MAX_STRING_SIZE;
                epicsSnprintf(s, MAX_STRING_SIZE, "row_%u", row);
                break;
            }
            case DBF_CHAR:
                ((epicsInt8 *)buf)[row]    = (epicsInt8)rval;   break;
            case DBF_UCHAR:
                ((epicsUInt8 *)buf)[row]   = (epicsUInt8)rval;  break;
            case DBF_SHORT:
                ((epicsInt16 *)buf)[row]   = (epicsInt16)rval;  break;
            case DBF_USHORT:
                ((epicsUInt16 *)buf)[row]  = (epicsUInt16)rval; break;
            case DBF_LONG:
                ((epicsInt32 *)buf)[row]   = (epicsInt32)rval;  break;
            case DBF_ULONG:
                ((epicsUInt32 *)buf)[row]  = (epicsUInt32)rval; break;
            case DBF_INT64:
                ((epicsInt64 *)buf)[row]   = (epicsInt64)rval;  break;
            case DBF_UINT64:
                ((epicsUInt64 *)buf)[row]  = (epicsUInt64)rval; break;
            case DBF_FLOAT:
                ((epicsFloat32 *)buf)[row] = (epicsFloat32)rval * 0.001f; break;
            case DBF_DOUBLE:
                ((epicsFloat64 *)buf)[row] = (epicsFloat64)rval * 0.001;  break;
            default:
                ((epicsUInt8 *)buf)[row]   = 0; break;
            }
        }

        *cols[col].nord = nelm;
        *cols[col].chgd = 1;
    }
    return 0;
}
#undef COLB

tableBdset devTableBSim = {
    {5, NULL, NULL, NULL, NULL},
    read_table
};
epicsExportAddress(dset, devTableBSim);
