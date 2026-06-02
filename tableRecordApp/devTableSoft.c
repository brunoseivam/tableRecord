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
        prec->col08name, prec->col09name, prec->col0aname, prec->col0bname,
        prec->col0cname, prec->col0dname, prec->col0ename, prec->col0fname
    };
    char *optnames[16] = {
        prec->colopt00name, prec->colopt01name, prec->colopt02name, prec->colopt03name,
        prec->colopt04name, prec->colopt05name, prec->colopt06name, prec->colopt07name,
        prec->colopt08name, prec->colopt09name, prec->colopt0aname, prec->colopt0bname,
        prec->colopt0cname, prec->colopt0dname, prec->colopt0ename, prec->colopt0fname
    };
    epicsUInt32 n = 0;
    while (n < 16 && names[n][0] != '\0') n++;
    prec->numcols = n;
    n = 0;
    while (n < 16 && optnames[n][0] != '\0') n++;
    prec->numoptcols = n;
    return 0;
}

typedef struct { DBLINK *inp; epicsEnum16 *type; void **val; epicsUInt8 *chgd; } Col;

#define COL(n) { &prec->col##n##inp, &prec->col##n##type, \
                  &prec->col##n##val, &prec->col##n##chgd }
#define COLOPT(n) { &prec->colopt##n##inp, &prec->colopt##n##type, \
                     &prec->colopt##n##val, &prec->colopt##n##chgd }

static long read_table(tableRecord *prec)
{
    Col cols[] = {
        COL(00), COL(01), COL(02), COL(03),
        COL(04), COL(05), COL(06), COL(07),
        COL(08), COL(09), COL(0a), COL(0b),
        COL(0c), COL(0d), COL(0e), COL(0f)
    };
    Col optcols[] = {
        COLOPT(00), COLOPT(01), COLOPT(02), COLOPT(03),
        COLOPT(04), COLOPT(05), COLOPT(06), COLOPT(07),
        COLOPT(08), COLOPT(09), COLOPT(0a), COLOPT(0b),
        COLOPT(0c), COLOPT(0d), COLOPT(0e), COLOPT(0f)
    };
    epicsUInt32 i;
    epicsUInt32 numrows = 0;

    for (i = 0; i < prec->numcols && i < 16; i++) {
        long nReq = (long)prec->maxrows;
        /* Constant links are loaded once at init time (tableRecord.c).
           Only re-read non-constant (CA/DB) links here. */
        if (dbLinkIsConstant(cols[i].inp)) continue;
        if (!*cols[i].val) continue;
        if (dbGetLink(cols[i].inp, *cols[i].type, *cols[i].val, 0, &nReq) == 0) {
            *cols[i].chgd = 1;
            if ((epicsUInt32)nReq > numrows)
                numrows = (epicsUInt32)nReq;
        }
    }
    prec->numrows = numrows;

    for (i = 0; i < prec->numoptcols && i < 16; i++) {
        long nReq = (long)prec->maxrows;
        if (dbLinkIsConstant(optcols[i].inp)) continue;
        if (!*optcols[i].val) continue;
        if (dbGetLink(optcols[i].inp, *optcols[i].type, *optcols[i].val, 0, &nReq) == 0)
            *optcols[i].chgd = 1;
    }
    return 0;
}
#undef COL
#undef COLOPT

tabledset devTableSoft = {
    {5, NULL, NULL, soft_init_record, NULL},
    read_table
};
epicsExportAddress(dset, devTableSoft);
