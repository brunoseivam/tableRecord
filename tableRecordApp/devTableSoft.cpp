#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>

#include "dbAccess.h"
#include "dbFldTypes.h"
#include "dbLink.h"
#include "devSup.h"
#include "cantProceed.h"
#include "epicsExport.h"
#include "epicsString.h"
#include "tableRecord.h"
#include "tableRecordUtil.h"

/*
 * Soft Channel device support for the table record.
 *
 * STRING columns are staged through a temporary buffer before being re-encoded
 * into vstring cells.  This is required because numeric→string link conversions
 * (e.g. getDoubleString) do not zero-pad the 40-byte cells, which would leave
 * stale bytes in positions 32..39 that the vstring discriminator would misread
 * as overflow pointers.
 */

struct DevTableSoftPvt {
    std::vector<TableRecordWrapper::DataColumn> *data_cols;
    std::vector<TableRecordWrapper::OptColumn>  *opt_cols;
    char   *stage;      /* staging buffer for STRING column loads */
    size_t  stage_rows; /* number of rows in the staging buffer   */

    DevTableSoftPvt() : stage(nullptr), stage_rows(0) {
        data_cols = new std::vector<TableRecordWrapper::DataColumn>();
        opt_cols  = new std::vector<TableRecordWrapper::OptColumn>();
    }
};

/* Load a STRING column through the staging buffer and re-encode each row as
 * a vstring cell.  Uses dbLoadLinkArray when constant=true, dbGetLink otherwise. */
static long loadStringColumn(struct link *plnk, void *val, long maxrows,
                              char *stage, epicsUInt32 *numrows, epicsUInt8 *chgd,
                              bool constant)
{
    long n_req = maxrows;
    memset(stage, 0, (size_t)maxrows * MAX_STRING_SIZE);

    long status = constant
        ? dbLoadLinkArray(plnk, DBF_STRING, stage, &n_req)
        : dbGetLink      (plnk, DBF_STRING, stage, 0, &n_req);

    if (status != 0)
        return status;

    for (long r = 0; r < n_req; r++) {
        const char *cell = stage + r * MAX_STRING_SIZE;
        epicsUInt32 len  = (epicsUInt32)epicsStrnLen(cell, MAX_STRING_SIZE - 1);
        tablerec_vstr_write(val, (epicsUInt32)r, cell, len);
    }
    *numrows = (epicsUInt32)n_req;
    *chgd    = 1;
    return 0;
}

static long soft_init_record(struct dbCommon *prec)
{
    /* Ask record to call us in pass 1 instead */
    if (prec->pact != TABLEREC_DEVINIT_PASS1)
        return TABLEREC_DEVINIT_PASS1;

    TableRecordWrapper rec(prec);
    DevTableSoftPvt *pvt = new DevTableSoftPvt();

    rec.data_cols(*pvt->data_cols);
    rec.opt_cols(*pvt->opt_cols);

    /* Allocate a staging buffer large enough for the larger of the two row sizes */
    pvt->stage_rows = std::max(rec.max_data_rows(), rec.max_opt_rows());
    if (pvt->stage_rows == 0)
        pvt->stage_rows = 1;
    pvt->stage = (char *)callocMustSucceed(
        pvt->stage_rows, MAX_STRING_SIZE, "devTableSoft: stage");

    rec.set_private(pvt);

    /* Load constant links */
    for (auto & c : *pvt->data_cols) {
        if (!c.inp || !*c.val)
            continue;

        if (c.config.type == DBF_STRING) {
            if (loadStringColumn(c.inp, *c.val, (long)rec.max_data_rows(),
                                 pvt->stage, c.numrows, c.chgd, true) == 0)
                prec->udf = FALSE;
        } else {
            long n_req = (long)rec.max_data_rows();
            if (dbLoadLinkArray(c.inp, c.config.type, *c.val, &n_req) == 0) {
                *c.numrows = (epicsUInt32)n_req;
                *c.chgd    = 1;
                prec->udf  = FALSE;
            }
        }
    }

    for (auto & c : *pvt->opt_cols) {
        if (!c.inp || !*c.val)
            continue;

        if (c.config.type == DBF_STRING) {
            if (loadStringColumn(c.inp, *c.val, (long)rec.max_opt_rows(),
                                 pvt->stage, c.numrows, c.chgd, true) == 0)
                prec->udf = FALSE;
        } else {
            long n_req = (long)rec.max_opt_rows();
            if (dbLoadLinkArray(c.inp, c.config.type, *c.val, &n_req) == 0) {
                *c.numrows = (epicsUInt32)n_req;
                *c.chgd    = 1;
                prec->udf  = FALSE;
            }
        }
    }

    return 0;
}

static long soft_read_table(tableRecord *prec)
{
    TableRecordWrapper rec(*prec);
    DevTableSoftPvt *pvt = rec.get_private<DevTableSoftPvt>();

    for (auto & c : *pvt->data_cols) {
        if (dbLinkIsConstant(c.inp) || !*c.val)
            continue;

        if (c.config.type == DBF_STRING) {
            loadStringColumn(c.inp, *c.val, (long)rec.max_data_rows(),
                             pvt->stage, c.numrows, c.chgd, false);
        } else {
            long n_req = (long)rec.max_data_rows();
            if (dbGetLink(c.inp, c.config.type, *c.val, 0, &n_req) == 0) {
                *c.numrows = (epicsUInt32)n_req;
                *c.chgd    = 1;
            }
        }
    }

    for (auto & c : *pvt->opt_cols) {
        if (dbLinkIsConstant(c.inp) || !*c.val)
            continue;

        if (c.config.type == DBF_STRING) {
            loadStringColumn(c.inp, *c.val, (long)rec.max_opt_rows(),
                             pvt->stage, c.numrows, c.chgd, false);
        } else {
            long n_req = (long)rec.max_opt_rows();
            if (dbGetLink(c.inp, c.config.type, *c.val, 0, &n_req) == 0) {
                *c.numrows = (epicsUInt32)n_req;
                *c.chgd    = 1;
            }
        }
    }
    return 0;
}

tabledset devTableSoft = {
    {5, NULL, NULL, soft_init_record, NULL},
    soft_read_table
};
epicsExportAddress(dset, devTableSoft);
