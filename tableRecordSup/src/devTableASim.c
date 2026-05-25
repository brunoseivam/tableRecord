#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dbAccess.h"
#include "dbFldTypes.h"
#include "devSup.h"
#include "epicsStdio.h"
#include "epicsExport.h"
#include "tableARecord.h"

/*
 * Random Sim device support for tableA.
 *
 * On each process() call, fills every active column with random data typed
 * according to coltypes[i].  String columns receive "row_N" values.
 */

static const char *SIM_NAMES[]  = {"x", "y", "label", "flag", "c4", "c5", "c6", "c7"};
static const char *SIM_LABELS[] = {"X axis", "Y axis", "Label", "Flag", "C4", "C5", "C6", "C7"};
static const epicsUInt16 SIM_TYPES[] = {
    DBF_DOUBLE, DBF_DOUBLE, DBF_STRING, DBF_UCHAR,
    DBF_DOUBLE, DBF_DOUBLE, DBF_DOUBLE, DBF_DOUBLE
};

static long sim_init_record(struct dbCommon *pcommon)
{
    tableARecord *prec = (tableARecord *)pcommon;
    epicsUInt32 i;
    for (i = 0; i < prec->ncol && i < 8; i++) {
        char *name = prec->colnames + i * MAX_STRING_SIZE;
        if (name[0] == '\0')
            strncpy(name, SIM_NAMES[i], MAX_STRING_SIZE - 1);
        char *lbl = prec->collabels + i * MAX_STRING_SIZE;
        if (lbl[0] == '\0')
            strncpy(lbl, SIM_LABELS[i], MAX_STRING_SIZE - 1);
        if (prec->coltypes[i] > DBF_ENUM)
            prec->coltypes[i] = SIM_TYPES[i];
        if (prec->coltypes[i] == 0 && name[0] == SIM_NAMES[i][0])
            prec->coltypes[i] = SIM_TYPES[i];
    }
    return 0;
}

static long read_table(tableARecord *prec)
{
    void *colbufs[] = {
        prec->col00, prec->col01, prec->col02, prec->col03,
        prec->col04, prec->col05, prec->col06, prec->col07
    };
    epicsUInt32 row, col;

    prec->nrow = prec->maxrows;

    for (col = 0; col < prec->ncol && col < 8; col++) {
        epicsUInt16 ftvl = prec->coltypes[col];
        void *buf = colbufs[col];
        if (!buf)
            continue;

        for (row = 0; row < prec->maxrows; row++) {
            long rval = random();
            switch (ftvl) {
            case DBF_STRING: {
                char *s = (char *)buf + row * MAX_STRING_SIZE;
                epicsSnprintf(s, MAX_STRING_SIZE, "row_%u", row);
                break;
            }
            case DBF_CHAR:
                ((epicsInt8 *)buf)[row]   = (epicsInt8)rval;   break;
            case DBF_UCHAR:
                ((epicsUInt8 *)buf)[row]  = (epicsUInt8)rval;  break;
            case DBF_SHORT:
                ((epicsInt16 *)buf)[row]  = (epicsInt16)rval;  break;
            case DBF_USHORT:
                ((epicsUInt16 *)buf)[row] = (epicsUInt16)rval; break;
            case DBF_LONG:
                ((epicsInt32 *)buf)[row]  = (epicsInt32)rval;  break;
            case DBF_ULONG:
                ((epicsUInt32 *)buf)[row] = (epicsUInt32)rval; break;
            case DBF_INT64:
                ((epicsInt64 *)buf)[row]  = (epicsInt64)rval;  break;
            case DBF_UINT64:
                ((epicsUInt64 *)buf)[row] = (epicsUInt64)rval; break;
            case DBF_FLOAT:
                ((epicsFloat32 *)buf)[row] = (epicsFloat32)rval * 0.001f; break;
            case DBF_DOUBLE:
                ((epicsFloat64 *)buf)[row] = (epicsFloat64)rval * 0.001;  break;
            default:
                ((epicsUInt8 *)buf)[row]  = 0; break;
            }
        }

        prec->chgd[col] = 1;
    }
    return 0;
}

tableAdset devTableASim = {
    {5, NULL, NULL, sim_init_record, NULL},
    read_table
};
epicsExportAddress(dset, devTableASim);
