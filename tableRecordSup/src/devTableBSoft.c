#include <stddef.h>
#include <string.h>

#include "dbAccess.h"
#include "dbFldTypes.h"
#include "dbLink.h"
#include "devSup.h"
#include "epicsExport.h"
#include "tableBRecord.h"

/*
 * Soft Channel device support for tableB.
 *
 * For each active column, fetches data from COLiINP via dbGetLink.
 */

typedef struct { DBLINK *inp; epicsEnum16 *ftvl; epicsUInt32 *nelm;
                 epicsUInt32 *nord; void **val; epicsUInt8 *chgd; } ColB;

#define COLB(n) { &prec->col##n##inp, &prec->col##n##ftvl, &prec->col##n##nelm, \
                  &prec->col##n##nord, &prec->col##n##val, &prec->col##n##chgd }

static long read_table(tableBRecord *prec)
{
    ColB cols[] = {
        COLB(00), COLB(01), COLB(02), COLB(03),
        COLB(04), COLB(05), COLB(06), COLB(07)
    };
    epicsUInt32 i;

    for (i = 0; i < prec->ncol && i < 8; i++) {
        epicsUInt32 nelm = *cols[i].nelm ? *cols[i].nelm : prec->maxrows;
        long nReq = (long)nelm;
        if (!*cols[i].val) continue;
        if (dbGetLink(cols[i].inp, *cols[i].ftvl, *cols[i].val, 0, &nReq) == 0) {
            *cols[i].nord = (epicsUInt32)nReq;
            *cols[i].chgd = 1;
        }
    }
    return 0;
}
#undef COLB

tableBdset devTableBSoft = {
    {5, NULL, NULL, NULL, NULL},
    read_table
};
epicsExportAddress(dset, devTableBSoft);
