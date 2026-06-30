/*
 * Unit tests for tableRecord's init_record validation.
 *
 * iocInit() discards the value returned by init_record() (see
 * epics-base .../misc/iocInit.c: it just calls prset->init_record(precord, 0)
 * and ignores the result), so a failed validation neither aborts iocInit nor
 * gets stored in any field.
 *
 * To observe a check's outcome we therefore re-invoke the record's init_record
 * ourselves once iocInit has resolved each record's dset, and read the status
 * code straight from the return value.  Re-running pass 0 is safe for these
 * records: failing checks return before any allocation, and the success cases
 * skip already-allocated buffers.
 */

#include "dbAccess.h"
#include "devSup.h"
#include "recSup.h"
#include "dbUnitTest.h"
#include "errlog.h"
#include "epicsStdio.h"
#include "epicsExport.h"
#include "tableRecord.h"
#include "tableRecordUtil.h"

#include "testMain.h"

#include <string>
#include <vector>

extern "C" int tableInitTest_registerRecordDeviceDriver(struct dbBase *);

/* ------------------------------------------------------------------ */
/* Stub device supports used to exercise the read_table check.        */
/* ------------------------------------------------------------------ */

/* read_table pointer is NULL -> S_dev_missingSup */
static tabledset devTableNoRead = {
    {5, NULL, NULL, NULL, NULL},
    NULL
};
epicsExportAddress(dset, devTableNoRead);

/* dset version (common.number) < 5 -> S_dev_missingSup */
static long dummy_read_table(tableRecord *prec)
{
    (void)prec;
    return 0;
}
static tabledset devTableBadVer = {
    {4, NULL, NULL, NULL, NULL},
    &dummy_read_table
};
epicsExportAddress(dset, devTableBadVer);

/* Re-run init_record pass 0 for a record and return its status code.
 * tableRecord has no VAL field, so address the record via an existing field
 * (NUMCOLS) and use addr.precord. */
static long reinit(const char *name)
{
    char pv[64];
    epicsSnprintf(pv, sizeof(pv), "%s.NUMCOLS", name);
    dbCommon *prec = testdbRecordPtr(pv);
    return prec->rset->init_record(prec, 0);
}

static void startIoc(void)
{
    testdbPrepare();
    testdbReadDatabase("tableInitTest.dbd", NULL, NULL);
    tableInitTest_registerRecordDeviceDriver(pdbbase);
    testdbReadDatabase("tableInitTest.db", NULL, NULL);

    eltc(0);                    /* mute expected init error messages */
    testIocInitOk();
    eltc(1);
}

static void stopIoc(void)
{
    testIocShutdownOk();
    testdbCleanup();
}

static void testReadTable(void)
{
    testDiag("read_table check");
    /* read_table pointer is NULL */
    testOk(reinit("bad:noread") == S_dev_missingSup, "bad:noread -> S_dev_missingSup");
    /* dset version (common.number) < 5 */
    testOk(reinit("bad:badver") == S_dev_missingSup, "bad:badver -> S_dev_missingSup");
}

static void testDataColNames(void)
{
    testDiag("data column name checks");
    /* each record isolates a single defect, all reported as S_db_errArg */
    testOk(reinit("bad:cname") == S_db_errArg, "bad:cname (invalid syntax) -> S_db_errArg");
    testOk(reinit("bad:cdup")  == S_db_errArg, "bad:cdup (duplicate) -> S_db_errArg");
    testOk(reinit("bad:cgap")  == S_db_errArg, "bad:cgap (gap) -> S_db_errArg");
}

static void testOptColNames(void)
{
    testDiag("optional column name checks");
    testOk(reinit("bad:oname") == S_db_errArg, "bad:oname (invalid syntax) -> S_db_errArg");
    testOk(reinit("bad:odup")  == S_db_errArg, "bad:odup (duplicate) -> S_db_errArg");
    testOk(reinit("bad:ogap")  == S_db_errArg, "bad:ogap (gap) -> S_db_errArg");
}

static void testSuccess(void)
{
    testDiag("successful init: status 0, maxrows default, column counts, dotted opt name");

    testOk(reinit("good:tbl") == 0, "good:tbl -> 0");
    /* MAXROWS was set to 0 in the db; init_record defaults it to 1 */
    testdbGetFieldEqual("good:tbl.MAXROWS", DBF_ULONG, 1);
    testdbGetFieldEqual("good:tbl.NUMCOLS", DBF_ULONG, 2);
    /* CO00NAME="pre.x" (dotted) and CO01NAME="y" are both accepted */
    testdbGetFieldEqual("good:tbl.NUMOPTCOLS", DBF_ULONG, 2);
}

static void testNamesImmutable(void)
{
    testDiag("CxxNAME / COxxNAME are read-only after init (SPC_NOMOD)");

    /* good:tbl was configured with C00NAME=a, C01NAME=b, CO00NAME=pre.x,
     * CO01NAME=y.  Runtime puts must be rejected with S_db_noMod and leave
     * the field values untouched. */
    testdbPutFieldFail(S_db_noMod, "good:tbl.C00NAME", DBF_STRING, "zzz");
    testdbGetFieldEqual("good:tbl.C00NAME", DBF_STRING, "a");

    testdbPutFieldFail(S_db_noMod, "good:tbl.C01NAME", DBF_STRING, "zzz");
    testdbGetFieldEqual("good:tbl.C01NAME", DBF_STRING, "b");

    testdbPutFieldFail(S_db_noMod, "good:tbl.CO00NAME", DBF_STRING, "zzz");
    testdbGetFieldEqual("good:tbl.CO00NAME", DBF_STRING, "pre.x");

    testdbPutFieldFail(S_db_noMod, "good:tbl.CO01NAME", DBF_STRING, "zzz");
    testdbGetFieldEqual("good:tbl.CO01NAME", DBF_STRING, "y");

    /* an unused column name field is equally protected */
    testdbPutFieldFail(S_db_noMod, "good:tbl.C05NAME", DBF_STRING, "zzz");
    testdbGetFieldEqual("good:tbl.C05NAME", DBF_STRING, "");
}

static void testLoad(void)
{
    testDiag("successful init: buffer allocation + pass-1 constant link load");

    /* C00INP is a constant array of 4 LONGs, loaded by the soft device
     * support during its pass-1 init_record; proves the buffer was allocated
     * and the pass-1 callback ran. */
    testdbGetFieldEqual("good:load.C00NROWS", DBF_ULONG, 4);
    testdbGetFieldEqual("good:load.UDF", DBF_UCHAR, 0);
}

static void testStringTruncation(void)
{
    testDiag("STRING column link load truncates each element to MAX_STRING_SIZE-1");

    /* good:longstr.C00INP is a constant array of strings, two of which exceed
     * 39 chars.  Loaded as DBF_STRING by the soft device support in pass-1, the
     * over-length rows come back clamped to 39 chars -- the vstring type-3
     * overflow path is unreachable through link loads (see devTableSoft.cpp). */
    dbCommon *prec = testdbRecordPtr("good:longstr.NUMCOLS");

    testdbGetFieldEqual("good:longstr.C00NROWS", DBF_ULONG, 3);
    testdbGetFieldEqual("good:longstr.UDF", DBF_UCHAR, 0);

    TableRecordWrapper rec(prec);
    std::vector<TableRecordWrapper::DataColumn> cols;
    rec.data_cols(cols);

    std::vector<std::string> out;
    dbScanLock(prec);
    rec.read_string_column(cols[0], out);
    dbScanUnlock(prec);

    const std::string in0 =
        "this string is definitely longer than forty characters AAAA";
    const std::string in2 =
        "another over-length value bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    const size_t cap = MAX_STRING_SIZE - 1;   /* 39 */

    testOk(out.size() == 3, "good:longstr loaded 3 rows (got %zu)", out.size());
    testOk(out[0].size() == cap,
           "row0 truncated to %zu chars (got %zu)", cap, out[0].size());
    testOk(out[0] == in0.substr(0, cap), "row0 == first %zu chars of input", cap);
    testOk(out[1] == "short one", "row1 (short) preserved verbatim");
    testOk(out[2].size() == cap,
           "row2 truncated to %zu chars (got %zu)", cap, out[2].size());
    testOk(out[2] == in2.substr(0, cap), "row2 == first %zu chars of input", cap);
}

MAIN(tableInitTest)
{
    testPlan(32);

    startIoc();

    testReadTable();
    testDataColNames();
    testOptColNames();
    testSuccess();
    testNamesImmutable();
    testLoad();
    testStringTruncation();

    stopIoc();

    return testDone();
}
