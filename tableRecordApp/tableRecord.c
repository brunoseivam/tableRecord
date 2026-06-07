#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dbDefs.h"
#include "dbAccess.h"
#include "dbEvent.h"
#include "dbFldTypes.h"
#include "dbLink.h"
#include "dbScan.h"
#include "devSup.h"
#include "errMdef.h"
#include "errlog.h"
#include "recSup.h"
#include "recGbl.h"
#include "cantProceed.h"

#define GEN_SIZE_OFFSET
#include "tableRecord.h"
#undef GEN_SIZE_OFFSET
#include "epicsExport.h"

#define report       NULL
#define initialize   NULL
static long init_record(struct dbCommon *, int);
static long process(struct dbCommon *);
#define special      NULL
#define get_value    NULL
static long cvt_dbaddr(DBADDR *);
static long get_array_info(DBADDR *, long *, long *);
static long put_array_info(DBADDR *, long);
static long get_units(DBADDR *, char *);
static long get_precision(const DBADDR *, long *);
#define get_enum_str    NULL
#define get_enum_strs   NULL
#define put_enum_str    NULL
#define get_graphic_double NULL
#define get_control_double NULL
#define get_alarm_double   NULL

rset tableRSET = {
    RSETNUMBER,
    report, initialize, init_record, process, special, get_value,
    cvt_dbaddr, get_array_info, put_array_info, get_units, get_precision,
    get_enum_str, get_enum_strs, put_enum_str,
    get_graphic_double, get_control_double, get_alarm_double
};
epicsExportAddress(rset, tableRSET);

/* Pointer-arithmetic helpers — valid because column fields are
   declared in consecutive groups in the DBD, so the struct members are laid
   out without intervening fields of other types. */

static void **tablerec_col_val_addr(tableRecord *prec, size_t i)
{
    assert(i < TABLEREC_MAX_DATA_COLS);
    return &prec->col00val + i;
}

static epicsEnum16 tablerec_col_type(tableRecord *prec, size_t i)
{
    assert(i < TABLEREC_MAX_DATA_COLS);
    return (&prec->col00type)[i];
}

static char* tablerec_col_name(tableRecord *prec, size_t i)
{
    assert(i < TABLEREC_MAX_DATA_COLS);
    return prec->col00name + i*sizeof(prec->col00name);
}

static void **tablerec_col_opt_val_addr(tableRecord *prec, size_t i)
{
    assert(i < TABLEREC_MAX_OPT_COLS);
    return &prec->colopt00val + i;
}

static epicsEnum16 tablerec_col_opt_type(tableRecord *prec, size_t i)
{
    assert(i < TABLEREC_MAX_OPT_COLS);
    return (&prec->colopt00type)[i];
}

static char* tablerec_col_opt_name(tableRecord *prec, size_t i)
{
    assert(i < TABLEREC_MAX_OPT_COLS);
    return prec->colopt00name + i*sizeof(prec->colopt00name);
}

static long init_record(struct dbCommon *pcommon, int pass)
{
    tableRecord *prec = (tableRecord *)pcommon;
    tabledset *pdset = (tabledset *)(prec->dset);

    /* must have dset defined */
    if (!pdset) {
        recGblRecordError(S_dev_noDSET, prec, "table: init_record");
        return S_dev_noDSET;
    }

    /* must have read_table function defined */
    if (pdset->common.number < 5 || !pdset->read_table) {
        recGblRecordError(S_dev_missingSup, prec, "table: init_record");
        return S_dev_missingSup;
    }

    if (pass == 0) {
        if (prec->maxrows == 0)
            prec->maxrows = 1;

        /* call pdset->init_record() in pass 0 so it can do its own
         * memory allocation and set prec->colXXval and prec->coloptXXval,
         * which must be set by the end of pass 0.
         */
        if (pdset->common.init_record) {
            long status = pdset->common.init_record(pcommon);

            if (status == TABLEREC_DEVINIT_PASS1) {
                /* requesting pass 1 callback, remember to do that */
                prec->pact = TABLEREC_DEVINIT_PASS1;
            }
            else if (status)
                return status;
        }

        prec->numrows = 0;

        /* calculate NUMCOLS after devsup had a chance to fill column names */
        prec->numcols = 0;
        for (size_t i = 0; i < TABLEREC_MAX_DATA_COLS; ++i) {
            if (strlen(tablerec_col_name(prec, i)) == 0)
                break;

            ++prec->numcols;
        }

        /* ensure that all remaining data columns have empty names (no gaps) */
        for (size_t i = prec->numcols; i < TABLEREC_MAX_DATA_COLS; ++i) {
            if (strlen(tablerec_col_name(prec, i)) != 0) {
                recGblRecordError(S_db_errArg, prec, "table: init_record");
                return S_db_errArg;
            }
        }

        /* calculate NUMOPTCOLS after devsup had a chance to fill column names */
        prec->numoptcols = 0;
        for (size_t i = 0; i < TABLEREC_MAX_OPT_COLS; ++i) {
            if (strlen(tablerec_col_opt_name(prec, i)) == 0)
                break;

            ++prec->numoptcols;
        }

        /* ensure that all remaining optional columns have empty names (no gaps) */
        for (size_t i = prec->numoptcols; i < TABLEREC_MAX_OPT_COLS; ++i) {
            if (strlen(tablerec_col_opt_name(prec, i)) != 0) {
                recGblRecordError(S_db_errArg, prec, "table: init_record");
                return S_db_errArg;
            }
        }

        /* allocate memory for value fields that were not allocated by devsup */
        for (size_t i = 0; i < prec->numcols; ++i) {
            void **val = tablerec_col_val_addr(prec, i);

            if (*val)
                continue;

            epicsEnum16 type = tablerec_col_type(prec, i);
            if (type > DBF_ENUM)
                type = DBF_DOUBLE;

            *val = callocMustSucceed(
                prec->maxrows, dbValueSize(type), "table: column data");
        }

        for (size_t i = 0; i < prec->numoptcols; ++i) {
            void **val = tablerec_col_opt_val_addr(prec, i);

            if (*val)
                continue;

            epicsEnum16 type = tablerec_col_opt_type(prec, i);
            if (type > DBF_ENUM)
                type = DBF_DOUBLE;

            *val = callocMustSucceed(
                prec->numcols, dbValueSize(type), "table: optional column data");
        }

        return 0;
    }

    if (prec->pact == TABLEREC_DEVINIT_PASS1) {
        /* device support asked for an init_record() callback in pass 1 */
        long status = pdset->common.init_record(pcommon);
        if (status)
            return status;
        prec->pact = FALSE;
    }

    return 0;
}

static long process(struct dbCommon *pcommon)
{
    tableRecord *prec = (tableRecord *)pcommon;
    tabledset *pdset = (tabledset *)prec->dset;
    long status;

    if (!pdset || !pdset->read_table) {
        prec->pact = TRUE;
        recGblRecordError(S_dev_missingSup, prec, "table: process");
        return S_dev_missingSup;
    }

    status = pdset->read_table(prec);
    if (status == 0)
        prec->udf = FALSE;

    recGblGetTimeStamp(prec);
    recGblResetAlarms(prec);

    if (prec->col00val)
        db_post_events(prec, prec->col00val, DBE_VALUE | DBE_LOG);

    recGblFwdLink(prec);
    prec->pact = FALSE;
    return status;
}

static long cvt_dbaddr(DBADDR *paddr)
{
    tableRecord *prec = (tableRecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    if (fi >= tableRecordCOL00VAL && fi <= tableRecordCOL0FVAL) {
        int i = fi - tableRecordCOL00VAL;
        epicsEnum16 type = tablerec_col_type(prec, i);
        void **vp = tablerec_col_val_addr(prec, i);
        paddr->pfield         = vp ? *vp : NULL;
        paddr->field_type     = type;
        paddr->field_size     = dbValueSize(type);
        paddr->dbr_field_type = type;
        paddr->no_elements    = prec->maxrows;
    } else if (fi >= tableRecordCOLOPT00VAL && fi <= tableRecordCOLOPT0FVAL) {
        int i = fi - tableRecordCOLOPT00VAL;
        epicsEnum16 type = tablerec_col_opt_type(prec, i);
        void **vp = tablerec_col_opt_val_addr(prec, i);
        paddr->pfield         = vp ? *vp : NULL;
        paddr->field_type     = type;
        paddr->field_size     = dbValueSize(type);
        paddr->dbr_field_type = type;
        paddr->no_elements    = prec->maxrows;
    }
    return 0;
}

static long get_array_info(DBADDR *paddr, long *no_elements, long *offset)
{
    tableRecord *prec = (tableRecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    *offset = 0;
    if (fi >= tableRecordCOL00VAL && fi <= tableRecordCOL0FVAL)
        *no_elements = prec->numrows;
    else if (fi >= tableRecordCOLOPT00VAL && fi <= tableRecordCOLOPT0FVAL)
        *no_elements = prec->numrows;
    else
        *no_elements = 0;
    return 0;
}

static long put_array_info(DBADDR *paddr, long nNew)
{
    tableRecord *prec = (tableRecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    if (fi >= tableRecordCOL00VAL && fi <= tableRecordCOL0FVAL) {
        epicsUInt32 n = (epicsUInt32)nNew;
        if (n > prec->maxrows) n = prec->maxrows;
        prec->numrows = n;
    } else if (fi >= tableRecordCOLOPT00VAL && fi <= tableRecordCOLOPT0FVAL) {
        epicsUInt32 n = (epicsUInt32)nNew;
        if (n > prec->maxrows) n = prec->maxrows;
        if (n > prec->numrows) prec->numrows = n;
    }
    return 0;
}

static long get_units(DBADDR *paddr, char *units)
{
    (void)paddr;
    units[0] = '\0';
    return 0;
}

static long get_precision(const DBADDR *paddr, long *precision)
{
    *precision = 4;
    recGblGetPrec(paddr, precision);
    return 0;
}
