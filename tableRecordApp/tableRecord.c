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

/* Pointer-arithmetic helpers — valid because COLxxINP/TYPE/VAL fields are
   declared in consecutive groups in the DBD, so the struct members are laid
   out without intervening fields of other types. */

static DBLINK *colInpAddr(tableRecord *prec, int i)
{
    return &prec->col00inp + i;
}

static void **colValAddr(tableRecord *prec, int i)
{
    return &prec->col00val + i;
}

static epicsEnum16 colType(tableRecord *prec, int i)
{
    return (&prec->col00type)[i];
}

static DBLINK *colOptInpAddr(tableRecord *prec, int i)
{
    return &prec->colopt00inp + i;
}

static void **colOptValAddr(tableRecord *prec, int i)
{
    return &prec->colopt00val + i;
}

static epicsEnum16 colOptType(tableRecord *prec, int i)
{
    return (&prec->colopt00type)[i];
}

static long init_record(struct dbCommon *pcommon, int pass)
{
    tableRecord *prec = (tableRecord *)pcommon;
    tabledset *pdset;

    if (pass == 0) {
        if (prec->maxrows == 0)
            prec->maxrows = 16;
        prec->numrows = 0;
        return 0;
    }

    /* pass 1: validate DSET, call its init_record
       then allocate per-column data buffers. */
    if (!(pdset = (tabledset *)prec->dset)) {
        recGblRecordError(S_dev_noDSET, prec, "table: init_record");
        return S_dev_noDSET;
    }
    if (pdset->common.number < 5 || !pdset->read_table) {
        recGblRecordError(S_dev_missingSup, prec, "table: init_record");
        return S_dev_missingSup;
    }
    if (pdset->common.init_record) {
        long s = pdset->common.init_record(pcommon);
        if (s) return s;
    }

    // Calculate NUMCOLS and NUMOPTCOLS after devsup had a chance to fill
    // data column names
    {
        prec->numcols = 0;
        char *datacolname = prec->col00name;
        for (size_t i = 0; i < TABLEREC_MAX_DATA_COLS && strlen(datacolname) > 0; ++i) {
            ++prec->numcols;
            datacolname += sizeof(prec->col00name);
        }

        prec->numoptcols = 0;
        char *optcolname = prec->colopt00name;
        for (size_t i = 0; i < TABLEREC_MAX_DATA_COLS && strlen(optcolname) > 0; ++i) {
            ++prec->numoptcols;
            optcolname += sizeof(prec->colopt00name);
        }
    }

    // Pre-allocate data value arrays
    for (size_t i = 0; i < prec->numcols; i++) {
        epicsEnum16 type = colType(prec, i);
        if (type > DBF_ENUM)
            type = DBF_DOUBLE;
        *colValAddr(prec, i) = callocMustSucceed(
            prec->maxrows, dbValueSize(type), "table: column data");
    }

    /* Load constant COLxxINP links into column buffers at init time.
       Mirrors devWfSoft: constant links are loaded here; dbGetLink is only
       for non-constant (CA/DB) links at process time. */
    for (size_t i = 0; i < prec->numcols; i++) {
        DBLINK *inp = colInpAddr(prec, i);
        void *buf = *colValAddr(prec, i);
        long nReq = prec->maxrows;
        if (inp && buf && !dbLoadLinkArray(inp, colType(prec, i), buf, &nReq)) {
            if ((epicsUInt32)nReq > prec->numrows)
                prec->numrows = (epicsUInt32)nReq;
            prec->udf = FALSE;
        }
    }

    for (size_t i = 0; i < prec->numoptcols; i++) {
        epicsEnum16 type = colOptType(prec, i);
        if (type > DBF_ENUM)
            type = DBF_DOUBLE;
        *colOptValAddr(prec, i) = callocMustSucceed(prec->numcols,
                                                    dbValueSize(type),
                                                    "table: optional column data");
    }

    for (size_t i = 0; i < prec->numoptcols; i++) {
        DBLINK *inp = colOptInpAddr(prec, i);
        void *buf = *colOptValAddr(prec, i);
        long nReq = prec->maxrows;
        if (inp && buf && !dbLoadLinkArray(inp, colOptType(prec, i), buf, &nReq)) {
            if ((epicsUInt32)nReq > prec->numrows)
                prec->numrows = (epicsUInt32)nReq;
            prec->udf = FALSE;
        }
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
        epicsEnum16 type = colType(prec, i);
        void **vp = colValAddr(prec, i);
        paddr->pfield         = vp ? *vp : NULL;
        paddr->field_type     = type;
        paddr->field_size     = dbValueSize(type);
        paddr->dbr_field_type = type;
        paddr->no_elements    = prec->maxrows;
    } else if (fi >= tableRecordCOLOPT00VAL && fi <= tableRecordCOLOPT0FVAL) {
        int i = fi - tableRecordCOLOPT00VAL;
        epicsEnum16 type = colOptType(prec, i);
        void **vp = colOptValAddr(prec, i);
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
