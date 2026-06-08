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

/*
 * Random Sim device support for the table record.
 *
 * sim_init_record sets NUMCOLS based on the first contiguous run of non-empty
 * names set in the .db file. sim_read_table fills every active column
 * with MAXROWS random elements typed by COLxxTYPE and sets NUMROWS.
 */

struct TableSimPrivate {
    std::vector<TableRecordWrapper::DataColumn> data_cols;

    TableSimPrivate(const std::vector<TableRecordWrapper::DataColumn> & data_cols)
    : data_cols(data_cols)
    {}
};

static long sim_init_record(struct dbCommon *prec) {
    TableRecordWrapper rec(prec);
    TableSimPrivate *pvt = new TableSimPrivate(rec.data_cols());
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
        char *s = (char *)val + row * MAX_STRING_SIZE;
        epicsSnprintf(s, MAX_STRING_SIZE, "val: %ld", rnd_val);
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

    // Set all changed fields
    // TODO: do we need chgd?
    for (size_t col = 0; col < rec.num_data_cols(); ++col) {
        *(&prec->c00chgd + col) = 1;
    }

    // Generate random data for each column
    for (auto & col : pvt->data_cols) {
        if (!*col.val)
            continue;

        fill_random_values(*col.val, col.config.type, rec.max_data_rows());
        *col.numrows = (epicsUInt32)rec.max_data_rows();
    }

    return 0;
}

tabledset devTableSim = {
    {5, NULL, NULL, sim_init_record, NULL},
    sim_read_table
};
epicsExportAddress(dset, devTableSim);
