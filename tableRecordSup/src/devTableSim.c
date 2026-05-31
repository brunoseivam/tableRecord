#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dbAccess.h"
#include "dbFldTypes.h"
#include "devSup.h"
#include "epicsStdio.h"
#include "epicsExport.h"
#include "tableRecord.h"

/*
 * Random Sim device support for the table record.
 *
 * sim_init_record sets NUMCOLS and fills in default names/labels for any
 * column left unset in the .db file.  read_table fills every active column
 * with MAXROWS random elements typed by COLiTYPE and sets NUMROWS.
 */

#define TABLE_SIM_NCOL 4

static const char *SIM_NAMES[] = {
    "x",   "y",   "label", "flag",
    "c4",  "c5",  "c6",    "c7",
    "c8",  "c9",  "c10",   "c11",
    "c12", "c13", "c14",   "c15"
};
static const char *SIM_LABELS[] = {
    "X axis", "Y axis", "Label", "Flag",
    "C4",     "C5",     "C6",   "C7",
    "C8",     "C9",     "C10",  "C11",
    "C12",    "C13",    "C14",  "C15"
};

static long sim_init_record(struct dbCommon *pcommon)
{
    tableRecord *prec = (tableRecord *)pcommon;
    char *names[16] = {
        prec->col00name, prec->col01name, prec->col02name, prec->col03name,
        prec->col04name, prec->col05name, prec->col06name, prec->col07name,
        prec->col08name, prec->col09name, prec->col0Aname, prec->col0Bname,
        prec->col0Cname, prec->col0Dname, prec->col0Ename, prec->col0Fname
    };
    char *labels[16] = {
        prec->col00label, prec->col01label, prec->col02label, prec->col03label,
        prec->col04label, prec->col05label, prec->col06label, prec->col07label,
        prec->col08label, prec->col09label, prec->col0Alabel, prec->col0Blabel,
        prec->col0Clabel, prec->col0Dlabel, prec->col0Elabel, prec->col0Flabel
    };
    epicsUInt32 i;
    prec->numcols = TABLE_SIM_NCOL;
    for (i = 0; i < TABLE_SIM_NCOL; i++) {
        if (!names[i][0])
            strncpy(names[i],  SIM_NAMES[i],  MAX_STRING_SIZE - 1);
        if (!labels[i][0])
            strncpy(labels[i], SIM_LABELS[i], MAX_STRING_SIZE - 1);
    }
    return 0;
}

typedef struct { epicsEnum16 *type; void **val; epicsUInt8 *chgd; } Col;

#define COL(n) { &prec->col##n##type, &prec->col##n##val, &prec->col##n##chgd }

static long read_table(tableRecord *prec)
{
    Col cols[] = {
        COL(00), COL(01), COL(02), COL(03),
        COL(04), COL(05), COL(06), COL(07),
        COL(08), COL(09), COL(10), COL(11),
        COL(12), COL(13), COL(14), COL(15)
    };
    epicsUInt32 col, row;

    for (col = 0; col < prec->numcols && col < 16; col++) {
        epicsEnum16 type = *cols[col].type;
        void *buf        = *cols[col].val;

        if (!buf) continue;

        for (row = 0; row < prec->maxrows; row++) {
            long rval = random();
            switch (type) {
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

        *cols[col].chgd = 1;
    }
    prec->numrows = prec->maxrows;
    return 0;
}
#undef COL

tabledset devTableSim = {
    {5, NULL, NULL, sim_init_record, NULL},
    read_table
};
epicsExportAddress(dset, devTableSim);
