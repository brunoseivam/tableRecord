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
#include "tableBRecord.h"
#undef GEN_SIZE_OFFSET
#include "epicsExport.h"

#define TABLB_MAX_COLS 8
/* Derived from the generated header — equals the number of fields per column bundle.
   Recomputed automatically if the bundle size changes. */
#define TABLB_COL_STRIDE (tableBRecordCOL01VAL - tableBRecordCOL00VAL)

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

rset tableBRSET = {
    RSETNUMBER,
    report, initialize, init_record, process, special, get_value,
    cvt_dbaddr, get_array_info, put_array_info, get_units, get_precision,
    get_enum_str, get_enum_strs, put_enum_str,
    get_graphic_double, get_control_double, get_alarm_double
};
epicsExportAddress(rset, tableBRSET);

static DBLINK *colInpAddr(tableBRecord *prec, int i)
{
    switch (i) {
    case 0: return &prec->col00inp;
    case 1: return &prec->col01inp;
    case 2: return &prec->col02inp;
    case 3: return &prec->col03inp;
    case 4: return &prec->col04inp;
    case 5: return &prec->col05inp;
    case 6: return &prec->col06inp;
    case 7: return &prec->col07inp;
    default: return NULL;
    }
}

static void **colValAddr(tableBRecord *prec, int i)
{
    switch (i) {
    case 0: return &prec->col00val;
    case 1: return &prec->col01val;
    case 2: return &prec->col02val;
    case 3: return &prec->col03val;
    case 4: return &prec->col04val;
    case 5: return &prec->col05val;
    case 6: return &prec->col06val;
    case 7: return &prec->col07val;
    default: return NULL;
    }
}

static epicsEnum16 colFtvl(tableBRecord *prec, int i)
{
    switch (i) {
    case 0: return prec->col00ftvl;
    case 1: return prec->col01ftvl;
    case 2: return prec->col02ftvl;
    case 3: return prec->col03ftvl;
    case 4: return prec->col04ftvl;
    case 5: return prec->col05ftvl;
    case 6: return prec->col06ftvl;
    case 7: return prec->col07ftvl;
    default: return DBF_UCHAR;
    }
}

static long init_record(struct dbCommon *pcommon, int pass)
{
    tableBRecord *prec = (tableBRecord *)pcommon;
    tableBdset *pdset;
    epicsUInt32 i;

    if (pass == 0) {
        if (prec->maxrows == 0)
            prec->maxrows = 16;
        prec->numrows = 0;
        return 0;
    }

    /* pass 1: validate DSET, call its init_record (which sets numcols),
       then allocate per-column data buffers. */
    if (!(pdset = (tableBdset *)prec->dset)) {
        recGblRecordError(S_dev_noDSET, prec, "tableB: init_record");
        return S_dev_noDSET;
    }
    if (pdset->common.number < 5 || !pdset->read_table) {
        recGblRecordError(S_dev_missingSup, prec, "tableB: init_record");
        return S_dev_missingSup;
    }
    if (pdset->common.init_record) {
        long s = pdset->common.init_record(pcommon);
        if (s) return s;
    }

    if (prec->numcols > TABLB_MAX_COLS)
        prec->numcols = TABLB_MAX_COLS;

    for (i = 0; i < prec->numcols; i++) {
        epicsEnum16 ftvl = colFtvl(prec, i);
        if (ftvl > DBF_ENUM)
            ftvl = DBF_DOUBLE;
        *colValAddr(prec, i) = callocMustSucceed(prec->maxrows,
                                                 dbValueSize(ftvl),
                                                 "tableB: column data");
    }

    /* Load constant COLxxINP links into column buffers at init time.
       Mirrors devWfSoft: constant links are loaded here; dbGetLink is only
       for non-constant (CA/DB) links at process time. */
    for (i = 0; i < prec->numcols; i++) {
        DBLINK *inp = colInpAddr(prec, i);
        void *buf = *colValAddr(prec, i);
        long nReq = prec->maxrows;
        if (inp && buf && !dbLoadLinkArray(inp, colFtvl(prec, i), buf, &nReq)) {
            if ((epicsUInt32)nReq > prec->numrows)
                prec->numrows = (epicsUInt32)nReq;
            prec->udf = FALSE;
        }
    }
    return 0;
}

static long process(struct dbCommon *pcommon)
{
    tableBRecord *prec = (tableBRecord *)pcommon;
    tableBdset *pdset = (tableBdset *)prec->dset;
    long status;

    if (!pdset || !pdset->read_table) {
        prec->pact = TRUE;
        recGblRecordError(S_dev_missingSup, prec, "tableB: process");
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
    tableBRecord *prec = (tableBRecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    if (fi >= tableBRecordCOL00VAL && fi <= tableBRecordCOL07VAL) {
        int i = (fi - tableBRecordCOL00VAL) / TABLB_COL_STRIDE;
        epicsEnum16 ftvl = colFtvl(prec, i);
        void **vp = colValAddr(prec, i);
        paddr->pfield         = vp ? *vp : NULL;
        paddr->field_type     = ftvl;
        paddr->field_size     = dbValueSize(ftvl);
        paddr->dbr_field_type = ftvl;
        paddr->no_elements    = prec->maxrows;
    }
    return 0;
}

static long get_array_info(DBADDR *paddr, long *no_elements, long *offset)
{
    tableBRecord *prec = (tableBRecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    *offset = 0;
    if (fi >= tableBRecordCOL00VAL && fi <= tableBRecordCOL07VAL)
        *no_elements = prec->numrows;
    else
        *no_elements = 0;
    return 0;
}

static long put_array_info(DBADDR *paddr, long nNew)
{
    tableBRecord *prec = (tableBRecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    if (fi >= tableBRecordCOL00VAL && fi <= tableBRecordCOL07VAL) {
        epicsUInt32 n = (epicsUInt32)nNew;
        if (n > prec->maxrows) n = prec->maxrows;
        prec->numrows = n;
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
