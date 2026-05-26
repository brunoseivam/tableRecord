#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "dbAccess.h"
#include "dbFldTypes.h"
#include "dbLink.h"
#include "devSup.h"
#include "epicsExport.h"
#include "tableBRecord.h"

/*
 * Soft Channel device support for tableB.
 *
 * soft_init_record derives NUMCOLS by counting the first unbroken streak of
 * non-empty COLxxNAME fields.  read_table fetches each active column via its
 * COLiINP link and sets NUMROWS to the maximum element count returned.
 */

static long soft_init_record(struct dbCommon *pcommon)
{
    tableBRecord *prec = (tableBRecord *)pcommon;
    char *names[8] = {
        prec->col00name, prec->col01name, prec->col02name, prec->col03name,
        prec->col04name, prec->col05name, prec->col06name, prec->col07name
    };
    epicsUInt32 n = 0;
    while (n < 8 && names[n][0] != '\0') n++;
    prec->numcols = n;
    return 0;
}

typedef struct { DBLINK *inp; epicsEnum16 *ftvl; void **val; epicsUInt8 *chgd; } ColB;

#define COLB(n) { &prec->col##n##inp, &prec->col##n##ftvl, \
                  &prec->col##n##val, &prec->col##n##chgd }

static long read_table(tableBRecord *prec)
{
    ColB cols[] = {
        COLB(00), COLB(01), COLB(02), COLB(03),
        COLB(04), COLB(05), COLB(06), COLB(07)
    };
    epicsUInt32 i;
    epicsUInt32 numrows = 0;

    for (i = 0; i < prec->numcols && i < 8; i++) {
        long nReq = (long)prec->maxrows;
        /* Constant links are loaded once at init time (tableBRecord.c).
           Only re-read non-constant (CA/DB) links here, matching devWfSoft. */
        if (dbLinkIsConstant(cols[i].inp)) continue;
        if (!*cols[i].val) continue;
        if (dbGetLink(cols[i].inp, *cols[i].ftvl, *cols[i].val, 0, &nReq) == 0) {
            *cols[i].chgd = 1;
            if ((epicsUInt32)nReq > numrows)
                numrows = (epicsUInt32)nReq;
        }
    }
    prec->numrows = numrows;
    return 0;
}
#undef COLB

tableBdset devTableBSoft = {
    {5, NULL, NULL, soft_init_record, NULL},
    read_table
};
epicsExportAddress(dset, devTableBSoft);
