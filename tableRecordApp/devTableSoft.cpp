#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "dbAccess.h"
#include "dbFldTypes.h"
#include "dbLink.h"
#include "devSup.h"
#include "epicsExport.h"
#include "tableRecord.h"
#include "tableRecordUtil.h"

/*
 * Soft Channel device support for the table record.
 */

struct DevTableSoftPvt {
    std::vector<TableRecordWrapper::DataColumn> *data_cols;
    std::vector<TableRecordWrapper::OptColumn> *opt_cols;

    DevTableSoftPvt() {
        data_cols = new std::vector<TableRecordWrapper::DataColumn>();
        opt_cols = new std::vector<TableRecordWrapper::OptColumn>();
    }
};

static long soft_init_record(struct dbCommon *prec)
{
    /* Ask record to call us in pass 1 instead */
    if (prec->pact != TABLEREC_DEVINIT_PASS1)
        return TABLEREC_DEVINIT_PASS1;

    TableRecordWrapper rec(prec);
    DevTableSoftPvt *pvt = new DevTableSoftPvt();

    rec.data_cols(*pvt->data_cols);
    rec.opt_cols(*pvt->opt_cols);
    rec.set_private(pvt);

    // Load constant links
    for (auto & c : *pvt->data_cols) {
        if (c.inp && *c.val) {
            long n_req = rec.max_data_rows();

            if (!dbLoadLinkArray(c.inp, c.config.type, *c.val, &n_req)) {
                *c.numrows = (epicsUInt32)n_req;
                prec->udf = FALSE;
            }
        }
    }

    for (auto & c : *pvt->opt_cols) {
        if (c.inp && *c.val) {
            long n_req = rec.max_opt_rows();

            if (!dbLoadLinkArray(c.inp, c.config.type, *c.val, &n_req)) {
                *c.numrows = (epicsUInt32)n_req;
                prec->udf = FALSE;
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

        long n_req = rec.max_data_rows();

        if (dbGetLink(c.inp, c.config.type, *c.val, 0, &n_req) == 0)
            *c.numrows = (epicsUInt32)n_req;
    }

    for (auto & c : *pvt->opt_cols) {
        if (dbLinkIsConstant(c.inp) || !*c.val)
            continue;

        long n_req = rec.max_opt_rows();

        if (dbGetLink(c.inp, c.config.type, *c.val, 0, &n_req) == 0)
            *c.numrows = (epicsUInt32)n_req;
    }
    return 0;
}

tabledset devTableSoft = {
    {5, NULL, NULL, soft_init_record, NULL},
    soft_read_table
};
epicsExportAddress(dset, devTableSoft);
