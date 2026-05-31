#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "dbAccess.h"
#include "dbFldTypes.h"
#include "dbLink.h"
#include "devSup.h"
#include "epicsExport.h"
#include "tableRecord.h"

/*
 * Soft Channel device support for the table record.
 *
 * soft_init_record derives NUMCOLS by counting the first unbroken streak of
 * non-empty COLxxNAME fields.  read_table fetches each active column via its
 * COLiINP link and sets NUMROWS to the maximum element count returned.
 */

static long soft_init_record(struct dbCommon *pcommon)
{
    tableRecord *prec = (tableRecord *)pcommon;
    char *names[16] = {
        prec->col00name, prec->col01name, prec->col02name, prec->col03name,
        prec->col04name, prec->col05name, prec->col06name, prec->col07name,
        prec->col08name, prec->col09name, prec->col0Aname, prec->col0Bname,
        prec->col0Cname, prec->col0Dname, prec->col0Ename, prec->col0Fname
    };
    epicsUInt32 n = 0;
    while (n < 16 && names[n][0] != '\0') n++;
    prec->numcols = n;
    return 0;
}

typedef struct { DBLINK *inp; epicsEnum16 *type; void **val; epicsUInt8 *chgd; } Col;

#define COL(n) { &prec->col##n##inp, &prec->col##n##type, \
                  &prec->col##n##val, &prec->col##n##chgd }

static long read_table(tableRecord *prec)
{
    Col cols[] = {
        COL(00), COL(01), COL(02), COL(03),
        COL(04), COL(05), COL(06), COL(07),
        COL(08), COL(09), COL(10), COL(11),
        COL(12), COL(13), COL(14), COL(15)
    };
    epicsUInt32 i;
    epicsUInt32 numrows = 0;

    for (i = 0; i < prec->numcols && i < 16; i++) {
        long nReq = (long)prec->maxrows;
        /* Constant links are loaded once at init time (tableRecord.c).
           Only re-read non-constant (CA/DB) links here, matching devWfSoft. */
        if (dbLinkIsConstant(cols[i].inp)) continue;
        if (!*cols[i].val) continue;
        if (dbGetLink(cols[i].inp, *cols[i].type, *cols[i].val, 0, &nReq) == 0) {
            *cols[i].chgd = 1;
            if ((epicsUInt32)nReq > numrows)
                numrows = (epicsUInt32)nReq;
        }
    }
    prec->numrows = numrows;
    return 0;
}
#undef COL

tabledset devTableSoft = {
    {5, NULL, NULL, soft_init_record, NULL},
    read_table
};
epicsExportAddress(dset, devTableSoft);
