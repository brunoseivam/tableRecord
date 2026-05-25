#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dbDefs.h"
#include "dbAccess.h"
#include "dbEvent.h"
#include "dbFldTypes.h"
#include "dbScan.h"
#include "devSup.h"
#include "errMdef.h"
#include "errlog.h"
#include "recSup.h"
#include "recGbl.h"
#include "cantProceed.h"

#define GEN_SIZE_OFFSET
#include "tableARecord.h"
#undef GEN_SIZE_OFFSET
#include "epicsExport.h"

#define TABLA_MAX_COLS 8

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

rset tableARSET = {
    RSETNUMBER,
    report, initialize, init_record, process, special, get_value,
    cvt_dbaddr, get_array_info, put_array_info, get_units, get_precision,
    get_enum_str, get_enum_strs, put_enum_str,
    get_graphic_double, get_control_double, get_alarm_double
};
epicsExportAddress(rset, tableARSET);

static void *colptr(tableARecord *prec, int i)
{
    void *cols[] = {
        prec->col00, prec->col01, prec->col02, prec->col03,
        prec->col04, prec->col05, prec->col06, prec->col07
    };
    return (i >= 0 && i < TABLA_MAX_COLS) ? cols[i] : NULL;
}

static long init_record(struct dbCommon *pcommon, int pass)
{
    tableARecord *prec = (tableARecord *)pcommon;
    tableAdset *pdset;

    if (pass == 0) {
        if (prec->maxcols == 0 || prec->maxcols > TABLA_MAX_COLS)
            prec->maxcols = TABLA_MAX_COLS;
        if (prec->maxrows == 0)
            prec->maxrows = 16;
        if (prec->ncol > prec->maxcols)
            prec->ncol = prec->maxcols;

        prec->colnames  = callocMustSucceed(prec->maxcols, MAX_STRING_SIZE,
                                            "tableA: colnames");
        prec->collabels = callocMustSucceed(prec->maxcols, MAX_STRING_SIZE,
                                            "tableA: collabels");
        prec->coltypes  = (epicsUInt16 *)callocMustSucceed(prec->maxcols,
                                            sizeof(epicsUInt16), "tableA: coltypes");
        prec->chgd      = (epicsUInt8 *)callocMustSucceed(prec->maxcols,
                                            sizeof(epicsUInt8), "tableA: chgd");
        return 0;
    }

    /* pass 1: allocate per-column data buffers */
    {
        void **colfields[] = {
            &prec->col00, &prec->col01, &prec->col02, &prec->col03,
            &prec->col04, &prec->col05, &prec->col06, &prec->col07
        };
        epicsUInt32 i;
        for (i = 0; i < prec->ncol && i < prec->maxcols; i++) {
            epicsUInt16 ftvl = prec->coltypes[i];
            if (ftvl > DBF_ENUM)
                ftvl = DBF_UCHAR;
            *colfields[i] = callocMustSucceed(prec->maxrows,
                                              dbValueSize(ftvl),
                                              "tableA: column data");
        }
        prec->val = prec->col00;
    }

    if (!(pdset = (tableAdset *)prec->dset)) {
        recGblRecordError(S_dev_noDSET, prec, "tableA: init_record");
        return S_dev_noDSET;
    }
    if (pdset->common.number < 5 || !pdset->read_table) {
        recGblRecordError(S_dev_missingSup, prec, "tableA: init_record");
        return S_dev_missingSup;
    }
    if (pdset->common.init_record)
        return pdset->common.init_record(pcommon);

    return 0;
}

static long process(struct dbCommon *pcommon)
{
    tableARecord *prec = (tableARecord *)pcommon;
    tableAdset *pdset = (tableAdset *)prec->dset;
    long status;

    if (!pdset || !pdset->read_table) {
        prec->pact = TRUE;
        recGblRecordError(S_dev_missingSup, prec, "tableA: process");
        return S_dev_missingSup;
    }

    status = pdset->read_table(prec);
    if (status == 0)
        prec->udf = FALSE;

    recGblGetTimeStamp(prec);
    recGblResetAlarms(prec);

    /* fire monitors on VAL (col00 buffer) so TableSource subscriptions fire */
    if (prec->col00)
        db_post_events(prec, prec->col00, DBE_VALUE | DBE_LOG);

    recGblFwdLink(prec);
    prec->pact = FALSE;
    return status;
}

static long cvt_dbaddr(DBADDR *paddr)
{
    tableARecord *prec = (tableARecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    if (fi == tableARecordCOLNAMES) {
        paddr->pfield         = prec->colnames;
        paddr->field_type     = DBF_STRING;
        paddr->field_size     = MAX_STRING_SIZE;
        paddr->dbr_field_type = DBR_STRING;
        paddr->no_elements    = prec->maxcols;
    } else if (fi == tableARecordCOLLABELS) {
        paddr->pfield         = prec->collabels;
        paddr->field_type     = DBF_STRING;
        paddr->field_size     = MAX_STRING_SIZE;
        paddr->dbr_field_type = DBR_STRING;
        paddr->no_elements    = prec->maxcols;
    } else if (fi == tableARecordCOLTYPES) {
        paddr->pfield         = prec->coltypes;
        paddr->field_type     = DBF_USHORT;
        paddr->field_size     = sizeof(epicsUInt16);
        paddr->dbr_field_type = DBR_SHORT;
        paddr->no_elements    = prec->maxcols;
    } else if (fi == tableARecordCHGD) {
        paddr->pfield         = prec->chgd;
        paddr->field_type     = DBF_UCHAR;
        paddr->field_size     = sizeof(epicsUInt8);
        paddr->dbr_field_type = DBR_CHAR;
        paddr->no_elements    = prec->maxcols;
    } else if (fi == tableARecordVAL ||
               (fi >= tableARecordCOL00 && fi <= tableARecordCOL07)) {
        int i = (fi == tableARecordVAL) ? 0 : (fi - tableARecordCOL00);
        epicsUInt16 ftvl = (i < (int)prec->ncol) ? prec->coltypes[i] : DBF_UCHAR;
        void *buf = colptr(prec, i);
        paddr->pfield         = buf;
        paddr->field_type     = ftvl;
        paddr->field_size     = dbValueSize(ftvl);
        paddr->dbr_field_type = ftvl;
        paddr->no_elements    = prec->maxrows;
    }
    return 0;
}

static long get_array_info(DBADDR *paddr, long *no_elements, long *offset)
{
    tableARecord *prec = (tableARecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    *offset = 0;
    if (fi == tableARecordCOLNAMES ||
        fi == tableARecordCOLLABELS ||
        fi == tableARecordCOLTYPES ||
        fi == tableARecordCHGD) {
        *no_elements = prec->ncol;
    } else {
        *no_elements = prec->nrow;
    }
    return 0;
}

static long put_array_info(DBADDR *paddr, long nNew)
{
    tableARecord *prec = (tableARecord *)paddr->precord;
    int fi = dbGetFieldIndex(paddr);

    if (fi == tableARecordCOLNAMES ||
        fi == tableARecordCOLLABELS ||
        fi == tableARecordCOLTYPES ||
        fi == tableARecordCHGD) {
        if ((epicsUInt32)nNew <= prec->maxcols)
            prec->ncol = (epicsUInt32)nNew;
    } else {
        if ((epicsUInt32)nNew <= prec->maxrows)
            prec->nrow = (epicsUInt32)nNew;
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
