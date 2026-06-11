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
#include "tableRecordUtil.h"
#include "tableVStr.h"

/*
 * Random Sim device support for the table record.
 *
 * sim_init_record sets NUMCOLS based on the first contiguous run of non-empty
 * names set in the .db file. sim_read_table fills every active column
 * with MAXROWS random elements typed by COLxxTYPE and sets NUMROWS.
 *
 * STRING columns use the vstring codec; roughly every 3rd row generates a
 * string > 39 bytes to exercise the overflow cell path.
 */

struct TableSimPrivate {
    std::vector<TableRecordWrapper::DataColumn> data_cols;
};

static long sim_init_record(struct dbCommon *prec) {
    TableRecordWrapper rec(prec);
    TableSimPrivate *pvt = new TableSimPrivate();
    rec.data_cols(pvt->data_cols);
    rec.set_private(pvt);
    return 0;
}

template<typename T>
static void fill_random_int_values(void *val, size_t num_rows) {
    for (size_t row = 0; row < num_rows; ++row)
        ((T*)val)[row] = (T)random();
}

template<typename T>
static void fill_random_flt_values(void *val, size_t num_rows) {
    for (size_t row = 0; row < num_rows; ++row)
        ((T*)val)[row] = (T)random() * 0.001;
}

static void fill_random_str_values(void *val, size_t num_rows) {
    for (size_t row = 0; row < num_rows; ++row) {
        long rnd_val = random();
        char tmp[128];
        int len = epicsSnprintf(tmp, sizeof(tmp), "val: %ld", rnd_val);
        /* ~every 3rd row: append filler to exceed 39 bytes and exercise
         * the vstring overflow cell path */
        if (rnd_val % 3 == 0)
            len += epicsSnprintf(tmp + len, (int)sizeof(tmp) - len,
                                 " -- long filler %ld%ld", rnd_val, rnd_val);
        tablerec_vstr_write(val, (epicsUInt32)row, tmp, (epicsUInt32)len);
    }
}

static void fill_random_values(void *val, epicsEnum16 type, size_t num_rows) {
    switch (type) {
        case DBF_STRING: fill_random_str_values              (val, num_rows); break;
        case DBF_CHAR:   fill_random_int_values<epicsInt8>   (val, num_rows); break;
        case DBF_UCHAR:  fill_random_int_values<epicsUInt8>  (val, num_rows); break;
        case DBF_SHORT:  fill_random_int_values<epicsInt16>  (val, num_rows); break;
        case DBF_USHORT: fill_random_int_values<epicsUInt16> (val, num_rows); break;
        case DBF_LONG:   fill_random_int_values<epicsInt32>  (val, num_rows); break;
        case DBF_ULONG:  fill_random_int_values<epicsUInt32> (val, num_rows); break;
        case DBF_INT64:  fill_random_int_values<epicsInt64>  (val, num_rows); break;
        case DBF_UINT64: fill_random_int_values<epicsUInt64> (val, num_rows); break;
        case DBF_FLOAT:  fill_random_flt_values<epicsFloat32>(val, num_rows); break;
        case DBF_DOUBLE: fill_random_flt_values<epicsFloat64>(val, num_rows); break;
        default:
            break;
    }
}

static long sim_read_table(tableRecord *prec) {
    TableRecordWrapper rec(*prec);
    TableSimPrivate *pvt = rec.get_private<TableSimPrivate>();

    for (auto & col : pvt->data_cols) {
        if (!*col.val)
            continue;

        fill_random_values(*col.val, col.config.type, rec.max_data_rows());
        *col.numrows = (epicsUInt32)rec.max_data_rows();
        *col.chgd = 1;
    }

    return 0;
}

tabledset devTableSim = {
    {5, NULL, NULL, sim_init_record, NULL},
    sim_read_table
};
epicsExportAddress(dset, devTableSim);
