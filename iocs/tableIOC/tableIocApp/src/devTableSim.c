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
 * sim_init_record sets NUMCOLS based on the first contiguous run of non-empty
 * names and labels set in the .db file. sim_read_table fills every active column
 * with MAXROWS random elements typed by COLxxTYPE and sets NUMROWS.
 */

// TODO: get this from record utils
static const size_t MAX_COLS = 16;

struct TableSimPrivate {
    size_t num_data_cols;
    size_t num_opt_cols;
};

static long sim_init_record(struct dbCommon *pcommon)
{
    tableRecord *prec = (tableRecord *)pcommon;

    struct TableSimPrivate *pvt = (struct TableSimPrivate*)malloc(sizeof(*pvt));
    pvt->num_data_cols = 0;
    pvt->num_opt_cols = 0;

    // Determine number of named columns
    for (size_t i = 0; i < MAX_COLS; ++i) {
        char *name = prec->col00name + sizeof(prec->col00name)*i;

        if (strlen(name) == 0)
            break;

        pvt->num_data_cols++;
    }

    // Determine number of named optional columns
    for (size_t i = 0; i < MAX_COLS; ++i) {
        char *name = prec->colopt00name + sizeof(prec->colopt00name)*i;

        if (strlen(name) == 0)
            break;

        pvt->num_opt_cols++;
    }

    prec->numcols = pvt->num_data_cols;
    prec->numoptcols = pvt->num_opt_cols;
    prec->dpvt = (void*)pvt;
    return 0;
}

static void fill_random_value(void *val, epicsEnum16 type, size_t num_rows) {

    for (size_t row = 0; row < num_rows; ++row) {
        long rnd_val = random();

        switch (type) {
            case DBF_STRING:
                char *s = (char *)val + row * MAX_STRING_SIZE;
                epicsSnprintf(s, MAX_STRING_SIZE, "val: %ld", rnd_val);
                break;
            case DBF_CHAR:
                ((epicsInt8 *)val)[row]    = (epicsInt8)rnd_val;   break;
            case DBF_UCHAR:
                ((epicsUInt8 *)val)[row]   = (epicsUInt8)rnd_val;  break;
            case DBF_SHORT:
                ((epicsInt16 *)val)[row]   = (epicsInt16)rnd_val;  break;
            case DBF_USHORT:
                ((epicsUInt16 *)val)[row]  = (epicsUInt16)rnd_val; break;
            case DBF_LONG:
                ((epicsInt32 *)val)[row]   = (epicsInt32)rnd_val;  break;
            case DBF_ULONG:
                ((epicsUInt32 *)val)[row]  = (epicsUInt32)rnd_val; break;
            case DBF_INT64:
                ((epicsInt64 *)val)[row]   = (epicsInt64)rnd_val;  break;
            case DBF_UINT64:
                ((epicsUInt64 *)val)[row]  = (epicsUInt64)rnd_val; break;
            case DBF_FLOAT:
                ((epicsFloat32 *)val)[row] = (epicsFloat32)rnd_val * 0.001f; break;
            case DBF_DOUBLE:
                ((epicsFloat64 *)val)[row] = (epicsFloat64)rnd_val * 0.001;  break;
            default:
                ((epicsUInt8 *)val)[row]   = 0; break;
        }
    }
}

static long sim_read_table(tableRecord *prec)
{
    struct TableSimPrivate *pvt = (struct TableSimPrivate*)prec->dpvt;

    // Set all changed fields to zero
    for (size_t col = 0; col < pvt->num_data_cols; ++col) {
        *(&prec->col00chgd + col) = 0;
    }

    // Generate random data for each column
    for (size_t col = 0; col < pvt->num_data_cols; ++col) {
        epicsEnum16 type = *(&prec->col00type + col);
        void **val = (&prec->col00val) + col;

        if (!*val)
            continue;

        fill_random_value(*val, type, prec->maxrows);
        *(&prec->col00chgd + col) = 1;
    }
    prec->numrows = prec->maxrows;

    return 0;
}

tabledset devTableSim = {
    {5, NULL, NULL, sim_init_record, NULL},
    sim_read_table
};
epicsExportAddress(dset, devTableSim);
