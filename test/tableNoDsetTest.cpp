/*
 * Unit test for the "no dset defined" check in tableRecord's init_record.
 *
 * This program registers the table record type but NO device support, so a
 * tableRecord instance ends up with a NULL dset and init_record takes its
 * very first error branch.  iocInit() discards init_record's return value, so
 * we re-invoke init_record ourselves and read the status code directly.
 */

#include "dbAccess.h"
#include "devSup.h"
#include "recSup.h"
#include "dbUnitTest.h"
#include "errlog.h"

#include "testMain.h"

extern "C" int tableNoDsetTest_registerRecordDeviceDriver(struct dbBase *);

MAIN(tableNoDsetTest)
{
    testPlan(1);

    testdbPrepare();
    testdbReadDatabase("tableNoDsetTest.dbd", NULL, NULL);
    tableNoDsetTest_registerRecordDeviceDriver(pdbbase);
    testdbReadDatabase("tableNoDsetTest.db", NULL, NULL);

    eltc(0);
    testIocInitOk();
    eltc(1);

    /* tableRecord has no VAL field, so address the record via NUMCOLS. */
    dbCommon *prec = testdbRecordPtr("nodset:tbl.NUMCOLS");
    testOk(prec->rset->init_record(prec, 0) == S_dev_noDSET,
           "nodset:tbl -> S_dev_noDSET");

    testIocShutdownOk();
    testdbCleanup();

    return testDone();
}
