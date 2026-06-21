#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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
#include "tableVStr.h"

#define GEN_SIZE_OFFSET
#include "tableRecord.h"
#undef GEN_SIZE_OFFSET
#include "tableRecordHook.h"
#include "epicsExport.h"

#define report       NULL
#define initialize   NULL
static long init_record(struct dbCommon *, int);
static long process(struct dbCommon *);
static long special(DBADDR *, int);
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

/* Returns true if name satisfies pvxs identifier rules: [a-zA-Z_][a-zA-Z0-9_]* */
static int valid_pvxs_simple_name(const char *name)
{
    if (!name || name[0] == '\0')
        return 0;

    for (size_t i = 0; name[i]; i++) {
        char c = name[i];

        if (isalpha(c) || c == '_')
            continue;

        if (isdigit(c) && i > 0)
            continue;

        return 0;
    }
    return 1;
}

/* For optional column names: allows one dot separator (prefix.fieldname),
   validating each component with valid_pvxs_simple_name. */
static int valid_pvxs_col_name(const char *name)
{
    if (!name || name[0] == '\0')
        return 0;

    const char *dot = strchr(name, '.');

    if (!dot)
        return valid_pvxs_simple_name(name);

    /* Validate prefix */
    size_t prefix_len = (size_t)(dot - name);
    char prefix[MAX_STRING_SIZE];
    if (prefix_len == 0 || prefix_len >= sizeof(prefix))
        return 0;

    memcpy(prefix, name, prefix_len);
    prefix[prefix_len] = '\0';

    return valid_pvxs_simple_name(prefix) && valid_pvxs_simple_name(dot + 1);
}


/* Pointer-arithmetic helpers — valid because column fields are
   declared in consecutive groups in the DBD, so the struct members are laid
   out without intervening fields of other types. */

static void **tablerec_col_val_addr(tableRecord *prec, size_t i)
{
    assert(i < TABLEREC_MAX_DATA_COLS);
    return &prec->c00val + i;
}

static epicsEnum16 tablerec_col_type(tableRecord *prec, size_t i)
{
    assert(i < TABLEREC_MAX_DATA_COLS);
    return (&prec->c00type)[i];
}

static char* tablerec_col_name(tableRecord *prec, size_t i)
{
    assert(i < TABLEREC_MAX_DATA_COLS);
    return prec->c00name + i*sizeof(prec->c00name);
}

static void **tablerec_col_opt_val_addr(tableRecord *prec, size_t i)
{
    assert(i < TABLEREC_MAX_OPT_COLS);
    return &prec->co00val + i;
}

static epicsEnum16 tablerec_col_opt_type(tableRecord *prec, size_t i)
{
    assert(i < TABLEREC_MAX_OPT_COLS);
    return (&prec->co00type)[i];
}

static char* tablerec_col_opt_name(tableRecord *prec, size_t i)
{
    assert(i < TABLEREC_MAX_OPT_COLS);
    return prec->co00name + i*sizeof(prec->co00name);
}

static long init_record(struct dbCommon *pcommon, int pass)
{
    tableRecord *prec = (tableRecord *)pcommon;
    tabledset *pdset = (tabledset *)(prec->dset);

    /* must have dset defined */
    if (!pdset) {
        errlogPrintf("tableRecord '%s': no dset defined\n", prec->name);
        recGblRecordError(S_dev_noDSET, prec, "table: init_record");
        return S_dev_noDSET;
    }

    /* must have read_table function defined */
    if (pdset->common.number < 5 || !pdset->read_table) {
        errlogPrintf("tableRecord '%s': no read_table defined\n", prec->name);
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

        /* validate column names and calculate NUMCOLS after devsup
         * had a chance to fill column names */
        prec->numcols = 0;
        for (size_t i = 0; i < TABLEREC_MAX_DATA_COLS; ++i) {
            char *name = tablerec_col_name(prec, i);

            if (strlen(name) == 0)
                break;

            if (!valid_pvxs_simple_name(name)) {
                errlogPrintf("tableRecord '%s': C%02lXNAME has invalid pvxs name '%s'"
                    " (must match [a-zA-Z_][a-zA-Z0-9_]*)\n",
                    prec->name, i, name);

                recGblRecordError(S_db_errArg, prec, "table: init_record");
                return S_db_errArg;
            }

            ++prec->numcols;
        }

        /* ensure that all remaining data columns have empty names (no gaps) */
        for (size_t i = prec->numcols; i < TABLEREC_MAX_DATA_COLS; ++i) {
            char *name = tablerec_col_name(prec, i);
            if (strlen(name) != 0) {
                errlogPrintf("tableRecord '%s': non-empty C%02lXNAME='%s' after empty columns\n",
                    prec->name, i, name);

                recGblRecordError(S_db_errArg, prec, "table: init_record");
                return S_db_errArg;
            }
        }

        /* validate column names and calculate NUMOPTCOLS after devsup
         * had a chance to fill column names */
        prec->numoptcols = 0;
        for (size_t i = 0; i < TABLEREC_MAX_OPT_COLS; ++i) {
            char *name = tablerec_col_opt_name(prec, i);

            if (strlen(name) == 0)
                break;

            if (!valid_pvxs_col_name(name)) {
                errlogPrintf("tableRecord '%s': CO%02lXNAME has invalid pvxs name '%s'"
                    " (must match [a-zA-Z_][a-zA-Z0-9_]*(\\.[a-zA-Z_][a-zA-Z0-9_]*)?)\n",
                    prec->name, i, name);

                recGblRecordError(S_db_errArg, prec, "table: init_record");
                return S_db_errArg;
            }

            ++prec->numoptcols;
        }

        /* ensure that all remaining optional columns have empty names (no gaps) */
        for (size_t i = prec->numoptcols; i < TABLEREC_MAX_OPT_COLS; ++i) {
            char *name = tablerec_col_opt_name(prec, i);
            if (strlen(name) != 0) {
                errlogPrintf("tableRecord '%s': non-empty CO%02lXNAME='%s' after empty columns\n",
                    prec->name, i, name);

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

    for (size_t i = 0; i < prec->numcols; ++i)
        *(&prec->c00chgd + i) = 0;
    for (size_t i = 0; i < prec->numoptcols; ++i)
        *(&prec->co00chgd + i) = 0;

    status = pdset->read_table(prec);
    if (status == 0)
        prec->udf = FALSE;

    recGblGetTimeStamp(prec);
    recGblResetAlarms(prec);

    if (prec->c00val)
        db_post_events(prec, prec->c00val, DBE_VALUE | DBE_LOG);

    /* Notify a registered publisher synchronously, while the lock is still held
     * and this cycle's CHGD flags are valid. */
    if (prec->rpvt) {
        tableRecordPvt *hook = (tableRecordPvt *)prec->rpvt;
        if (hook->notify)
            hook->notify((struct tableRecord *)prec);
    }

    recGblFwdLink(prec);
    prec->pact = FALSE;
    return status;
}

static long cvt_dbaddr(DBADDR *paddr)
{
    tableRecord *prec = (tableRecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    if (fi >= tableRecordC00VAL && fi <= tableRecordC0FVAL) {
        int i = fi - tableRecordC00VAL;
        epicsEnum16 type = tablerec_col_type(prec, i);
        void **vp = tablerec_col_val_addr(prec, i);
        paddr->pfield         = vp ? *vp : NULL;
        paddr->field_type     = type;
        paddr->field_size     = dbValueSize(type);
        paddr->dbr_field_type = type;
        paddr->no_elements    = prec->maxrows;
    } else if (fi >= tableRecordCO00VAL && fi <= tableRecordCO0FVAL) {
        int i = fi - tableRecordCO00VAL;
        epicsEnum16 type = tablerec_col_opt_type(prec, i);
        void **vp = tablerec_col_opt_val_addr(prec, i);
        paddr->pfield         = vp ? *vp : NULL;
        paddr->field_type     = type;
        paddr->field_size     = dbValueSize(type);
        paddr->dbr_field_type = type;
        /* opt buffers are allocated with prec->numcols rows (one per data column) */
        paddr->no_elements    = prec->numcols;
    }
    return 0;
}

static long get_array_info(DBADDR *paddr, long *no_elements, long *offset)
{
    tableRecord *prec = (tableRecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    *offset = 0;
    if (fi >= tableRecordC00VAL && fi <= tableRecordC0FVAL)
        *no_elements = *(&prec->c00nrows + (fi - tableRecordC00VAL));
    else if (fi >= tableRecordCO00VAL && fi <= tableRecordCO0FVAL)
        *no_elements = *(&prec->co00nrows + (fi - tableRecordCO00VAL));
    else
        *no_elements = 0;
    return 0;
}

static long put_array_info(DBADDR *paddr, long nNew)
{
    tableRecord *prec = (tableRecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    if (fi >= tableRecordC00VAL && fi <= tableRecordC0FVAL) {
        epicsUInt32 n = (epicsUInt32)nNew;
        if (n > prec->maxrows) n = prec->maxrows;
        *(&prec->c00nrows + (fi - tableRecordC00VAL)) = n;
    } else if (fi >= tableRecordCO00VAL && fi <= tableRecordCO0FVAL) {
        epicsUInt32 n = (epicsUInt32)nNew;
        /* opt buffers are allocated with numcols rows, not maxrows */
        if (n > prec->numcols) n = prec->numcols;
        *(&prec->co00nrows + (fi - tableRecordCO00VAL)) = n;
    }
    return 0;
}

/* special() — called by dbPutSpecial() before (after=0) and after (after=1) a
 * dbPutField to a CxxVAL/COxxVAL.  Verified: with special==NULL, puts to these
 * fields fail with S_db_noSupport (dbAccess.c:139); this function enables them.
 *
 * For STRING-typed columns at after=0 we free all overflow heap pointers and
 * zero the cells BEFORE the strncpy copy lands, preventing leaked heap blocks.
 * base's putStringString always produces valid type-1/2 cells (strncpy + [39]=0),
 * so the sanitized column is in a consistent state after the put.
 *
 * Note: CxxVAL fields have pp(TRUE), so a put to a passive record triggers
 * process(), which calls read_table and may overwrite the put (e.g. CSV/sim
 * dsets).  That is by design: the device support owns the column contents. */
static long special(DBADDR *paddr, int after)
{
    tableRecord *prec = (tableRecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    if (fi >= tableRecordC00VAL && fi <= tableRecordC0FVAL) {
        size_t i = (size_t)(fi - tableRecordC00VAL);
        void **vp = tablerec_col_val_addr(prec, i);

        if (i >= prec->numcols || !vp || !*vp)
            return S_db_noMod;

        if (!after) {
            if (tablerec_col_type(prec, i) == DBF_STRING)
                tablerec_vstr_clear(*vp, prec->maxrows);
        } else {
            *(&prec->c00chgd + i) = 1;
        }
        return 0;
    }

    if (fi >= tableRecordCO00VAL && fi <= tableRecordCO0FVAL) {
        size_t i = (size_t)(fi - tableRecordCO00VAL);
        void **vp = tablerec_col_opt_val_addr(prec, i);

        if (i >= prec->numoptcols || !vp || !*vp)
            return S_db_noMod;

        if (!after) {
            if (tablerec_col_opt_type(prec, i) == DBF_STRING)
                tablerec_vstr_clear(*vp, prec->numcols);
        } else {
            *(&prec->co00chgd + i) = 1;
        }
        return 0;
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
