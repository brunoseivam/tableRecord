#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dbDefs.h"
#include "dbAccess.h"
#include "dbEvent.h"
#include "dbFldTypes.h"
#include "dbScan.h"
#include "dbLink.h"
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

/* Per-column accessor structure — built once in init_record to avoid
   repeated switch statements in hot paths. */
typedef struct {
    char          *name;
    epicsEnum16   *ftvl;
    DBLINK        *inp;
    epicsUInt32   *nelm;
    epicsUInt32   *nord;
    void         **val;
    epicsUInt8    *chgd;
} ColAcc;

static void buildColAcc(tableBRecord *prec, ColAcc acc[TABLB_MAX_COLS])
{
    acc[0].name = prec->col00name; acc[0].ftvl = &prec->col00ftvl;
    acc[0].inp  = &prec->col00inp; acc[0].nelm = &prec->col00nelm;
    acc[0].nord = &prec->col00nord;acc[0].val  = &prec->col00val;
    acc[0].chgd = &prec->col00chgd;

    acc[1].name = prec->col01name; acc[1].ftvl = &prec->col01ftvl;
    acc[1].inp  = &prec->col01inp; acc[1].nelm = &prec->col01nelm;
    acc[1].nord = &prec->col01nord;acc[1].val  = &prec->col01val;
    acc[1].chgd = &prec->col01chgd;

    acc[2].name = prec->col02name; acc[2].ftvl = &prec->col02ftvl;
    acc[2].inp  = &prec->col02inp; acc[2].nelm = &prec->col02nelm;
    acc[2].nord = &prec->col02nord;acc[2].val  = &prec->col02val;
    acc[2].chgd = &prec->col02chgd;

    acc[3].name = prec->col03name; acc[3].ftvl = &prec->col03ftvl;
    acc[3].inp  = &prec->col03inp; acc[3].nelm = &prec->col03nelm;
    acc[3].nord = &prec->col03nord;acc[3].val  = &prec->col03val;
    acc[3].chgd = &prec->col03chgd;

    acc[4].name = prec->col04name; acc[4].ftvl = &prec->col04ftvl;
    acc[4].inp  = &prec->col04inp; acc[4].nelm = &prec->col04nelm;
    acc[4].nord = &prec->col04nord;acc[4].val  = &prec->col04val;
    acc[4].chgd = &prec->col04chgd;

    acc[5].name = prec->col05name; acc[5].ftvl = &prec->col05ftvl;
    acc[5].inp  = &prec->col05inp; acc[5].nelm = &prec->col05nelm;
    acc[5].nord = &prec->col05nord;acc[5].val  = &prec->col05val;
    acc[5].chgd = &prec->col05chgd;

    acc[6].name = prec->col06name; acc[6].ftvl = &prec->col06ftvl;
    acc[6].inp  = &prec->col06inp; acc[6].nelm = &prec->col06nelm;
    acc[6].nord = &prec->col06nord;acc[6].val  = &prec->col06val;
    acc[6].chgd = &prec->col06chgd;

    acc[7].name = prec->col07name; acc[7].ftvl = &prec->col07ftvl;
    acc[7].inp  = &prec->col07inp; acc[7].nelm = &prec->col07nelm;
    acc[7].nord = &prec->col07nord;acc[7].val  = &prec->col07val;
    acc[7].chgd = &prec->col07chgd;
}

/* Retrieve the val pointer for column i (for cvt_dbaddr dispatching) */
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

static epicsUInt32 colNelm(tableBRecord *prec, int i)
{
    epicsUInt32 n;
    switch (i) {
    case 0: n = prec->col00nelm; break;
    case 1: n = prec->col01nelm; break;
    case 2: n = prec->col02nelm; break;
    case 3: n = prec->col03nelm; break;
    case 4: n = prec->col04nelm; break;
    case 5: n = prec->col05nelm; break;
    case 6: n = prec->col06nelm; break;
    case 7: n = prec->col07nelm; break;
    default: n = 0; break;
    }
    return (n > 0) ? n : prec->maxrows;
}

static epicsUInt32 colNord(tableBRecord *prec, int i)
{
    switch (i) {
    case 0: return prec->col00nord;
    case 1: return prec->col01nord;
    case 2: return prec->col02nord;
    case 3: return prec->col03nord;
    case 4: return prec->col04nord;
    case 5: return prec->col05nord;
    case 6: return prec->col06nord;
    case 7: return prec->col07nord;
    default: return 0;
    }
}

static void setColNord(tableBRecord *prec, int i, epicsUInt32 n)
{
    switch (i) {
    case 0: prec->col00nord = n; break;
    case 1: prec->col01nord = n; break;
    case 2: prec->col02nord = n; break;
    case 3: prec->col03nord = n; break;
    case 4: prec->col04nord = n; break;
    case 5: prec->col05nord = n; break;
    case 6: prec->col06nord = n; break;
    case 7: prec->col07nord = n; break;
    default: break;
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
        if (prec->ncol > TABLB_MAX_COLS)
            prec->ncol = TABLB_MAX_COLS;

        for (i = 0; i < prec->ncol; i++) {
            epicsEnum16 ftvl = colFtvl(prec, i);
            if (ftvl > DBF_ENUM)
                ftvl = DBF_UCHAR;
            epicsUInt32 nelm = colNelm(prec, i);
            *colValAddr(prec, i) = callocMustSucceed(nelm,
                                                     dbValueSize(ftvl),
                                                     "tableB: column data");
            setColNord(prec, i, 0);
        }
        return 0;
    }

    /* pass 1: check device support */
    if (!(pdset = (tableBdset *)prec->dset)) {
        recGblRecordError(S_dev_noDSET, prec, "tableB: init_record");
        return S_dev_noDSET;
    }
    if (pdset->common.number < 5 || !pdset->read_table) {
        recGblRecordError(S_dev_missingSup, prec, "tableB: init_record");
        return S_dev_missingSup;
    }
    if (pdset->common.init_record)
        return pdset->common.init_record(pcommon);

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
    prec->val = (epicsInt32)status;
    if (status == 0)
        prec->udf = FALSE;

    recGblGetTimeStamp(prec);
    recGblResetAlarms(prec);

    db_post_events(prec, &prec->val, DBE_VALUE | DBE_LOG);
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
        int i = (fi - tableBRecordCOL00VAL) / 7;
        epicsEnum16 ftvl = colFtvl(prec, i);
        void **vp = colValAddr(prec, i);
        paddr->pfield         = vp ? *vp : NULL;
        paddr->field_type     = ftvl;
        paddr->field_size     = dbValueSize(ftvl);
        paddr->dbr_field_type = ftvl;
        paddr->no_elements    = colNelm(prec, i);
    }
    return 0;
}

static long get_array_info(DBADDR *paddr, long *no_elements, long *offset)
{
    tableBRecord *prec = (tableBRecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    *offset = 0;
    if (fi >= tableBRecordCOL00VAL && fi <= tableBRecordCOL07VAL) {
        int i = (fi - tableBRecordCOL00VAL) / 7;
        *no_elements = colNord(prec, i);
    } else {
        *no_elements = 0;
    }
    return 0;
}

static long put_array_info(DBADDR *paddr, long nNew)
{
    tableBRecord *prec = (tableBRecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    if (fi >= tableBRecordCOL00VAL && fi <= tableBRecordCOL07VAL) {
        int i = (fi - tableBRecordCOL00VAL) / 7;
        epicsUInt32 max = colNelm(prec, i);
        setColNord(prec, i, ((epicsUInt32)nNew <= max) ? (epicsUInt32)nNew : max);
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
