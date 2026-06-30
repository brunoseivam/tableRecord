/*
 * End-to-end test of the tableRecord partial-update property: on a PVA monitor
 * delta, only columns / labels / optional-columns that actually changed are
 * encoded and transported. Unchanged ones must be absent (unmarked) from the
 * delta the client receives, so the client keeps its previously held value.
 *
 * pvxs serializes only marked fields (src/dataencode.cpp to_wire_valid), so an
 * unmarked field in a received delta is proof it was neither encoded nor sent.
 *
 * A controllable device support (devTablePartial) is required because process()
 * clears every CHGD flag before calling read_table, and the publish reflects
 * exactly what read_table marked. The stock devTableSoft marks every linked
 * column changed each cycle, so it cannot produce a selective delta. This dset
 * changes exactly the column / opt-column / label selected by g_ctl per cycle.
 */

#include <sstream>
#include <string>
#include <vector>
#include <memory>

#include "dbAccess.h"
#include "devSup.h"
#include "dbUnitTest.h"
#include "errlog.h"
#include "epicsEvent.h"
#include "epicsStdio.h"
#include "epicsString.h"
#include "epicsExport.h"
#include "tableRecord.h"
#include "tableRecordUtil.h"

#include <pvxs/client.h>
#include <pvxs/server.h>
#include <pvxs/iochooks.h>
#include <pvxs/unittest.h>

#include "testMain.h"

using namespace pvxs;

extern "C" int tablePartialTest_registerRecordDeviceDriver(struct dbBase *);

/* ------------------------------------------------------------------ */
/* Controllable device support                                        */
/* ------------------------------------------------------------------ */

/* What read_table should change on its next invocation. -1 == leave alone. */
static struct Ctl {
    int  dataCol;
    int  optCol;
    int  labelCol;
    long ctr;
} g_ctl = { -1, -1, -1, 1000 };

static void write_long(void *buf, epicsInt32 v)
{
    ((epicsInt32 *)buf)[0] = v;
}

/* Pass-1 init: seed every data/opt column with one row so the initial monitor
 * snapshot carries them. Labels come from the CxxLABEL db fields. */
static long partial_init_record(struct dbCommon *prec)
{
    if (prec->pact != TABLEREC_DEVINIT_PASS1)
        return TABLEREC_DEVINIT_PASS1;

    TableRecordWrapper rec(prec);
    std::vector<TableRecordWrapper::DataColumn> dcols;
    std::vector<TableRecordWrapper::OptColumn>  ocols;
    rec.data_cols(dcols);
    rec.opt_cols(ocols);

    epicsInt32 k = 1;
    for (auto &c : dcols) {
        if (!*c.val) continue;
        write_long(*c.val, k++);
        *c.numrows = 1;
        *c.chgd = 1;
    }
    for (auto &c : ocols) {
        if (!*c.val) continue;
        write_long(*c.val, k++);
        *c.numrows = 1;
        *c.chgd = 1;
    }
    prec->udf = FALSE;
    return 0;
}

static long partial_read_table(tableRecord *prec)
{
    TableRecordWrapper rec(*prec);
    std::vector<TableRecordWrapper::DataColumn> dcols;
    std::vector<TableRecordWrapper::OptColumn>  ocols;
    rec.data_cols(dcols);
    rec.opt_cols(ocols);

    if (g_ctl.dataCol >= 0 && (size_t)g_ctl.dataCol < dcols.size()) {
        auto &c = dcols[g_ctl.dataCol];
        if (*c.val) {
            write_long(*c.val, (epicsInt32)++g_ctl.ctr);
            *c.numrows = 1;
            *c.chgd = 1;
        }
    }
    if (g_ctl.optCol >= 0 && (size_t)g_ctl.optCol < ocols.size()) {
        auto &c = ocols[g_ctl.optCol];
        if (*c.val) {
            write_long(*c.val, (epicsInt32)++g_ctl.ctr);
            *c.numrows = 1;
            *c.chgd = 1;
        }
    }
    /* Label changes are tracked by string comparison (ctx.lastLabels), not a
     * CHGD flag, so write the live label field and set no chgd. */
    if (g_ctl.labelCol >= 0 && (size_t)g_ctl.labelCol < dcols.size()) {
        auto &c = dcols[g_ctl.labelCol];
        if (c.label)
            epicsSnprintf(c.label, MAX_STRING_SIZE, "lbl%ld", ++g_ctl.ctr);
    }
    return 0;
}

static tabledset devTablePartial = {
    {5, NULL, NULL, partial_init_record, NULL},
    partial_read_table
};
epicsExportAddress(dset, devTablePartial);

/* ------------------------------------------------------------------ */
/* Test harness                                                       */
/* ------------------------------------------------------------------ */

namespace {

/* Block until the next monitor update is available, or fail on timeout. */
Value popUpdate(const std::shared_ptr<client::Subscription> &sub, epicsEvent &evt)
{
    while (true) {
        if (auto v = sub->pop())
            return v;
        if (!evt.wait(5.0)) {
            testFail("timeout waiting for monitor update");
            return Value();
        }
    }
}

/* True iff `path` exists in `top` and its own bit is marked (parents=false). */
bool leafMarked(const Value &top, const char *path)
{
    auto f = top[path];
    return f.valid() && f.isMarked(false);
}

std::string deltaStr(const Value &v)
{
    std::ostringstream oss;
    oss << v.format().delta();
    return oss.str();
}

/* Force one process()/publish cycle and return the resulting delta. */
Value processAndPop(const std::shared_ptr<client::Subscription> &sub, epicsEvent &evt)
{
    testdbPutFieldOk("tbl:partial.PROC", DBF_CHAR, 1);
    return popUpdate(sub, evt);
}

} // namespace

MAIN(tablePartialTest)
{
    /* 30 leaf-mark assertions + 5 PROC puts (testdbPutFieldOk) */
    testPlan(35);
    testSetup();

    pvxs::ioc::TestIOC ioc;
    testdbReadDatabase("tablePartialTest.dbd", NULL, NULL);
    tablePartialTest_registerRecordDeviceDriver(pdbbase);
    testdbReadDatabase("tablePartialTest.db", NULL, NULL);
    ioc.init();

    client::Context cli(pvxs::ioc::server().clientConfig().build());
    epicsEvent evt;
    auto sub = cli.monitor("tbl:partial")
                   .maskConnected(true)
                   .maskDisconnected(true)
                   .event([&evt](client::Subscription &) { evt.signal(); })
                   .exec();

    /* ---- initial full snapshot: everything present ---- */
    {
        Value v = popUpdate(sub, evt);
        testDiag("initial snapshot delta:\n%s", deltaStr(v).c_str());
        testTrue(leafMarked(v, "value.a")) << "init: value.a present";
        testTrue(leafMarked(v, "value.b")) << "init: value.b present";
        testTrue(leafMarked(v, "value.c")) << "init: value.c present";
        testTrue(leafMarked(v, "labels"))  << "init: labels present";
        testTrue(leafMarked(v, "opt0"))    << "init: opt0 present";
    }

    /* ---- A: only data column 'a' changes ---- */
    g_ctl = Ctl{ 0, -1, -1, g_ctl.ctr };
    {
        Value v = processAndPop(sub, evt);
        testDiag("A delta (data col a):\n%s", deltaStr(v).c_str());
        testTrue (leafMarked(v, "value.a")) << "A: value.a changed -> transported";
        testFalse(leafMarked(v, "value.b")) << "A: value.b unchanged -> not transported";
        testFalse(leafMarked(v, "value.c")) << "A: value.c unchanged -> not transported";
        testFalse(leafMarked(v, "labels"))  << "A: labels unchanged -> not transported";
        testFalse(leafMarked(v, "opt0"))    << "A: opt0 unchanged -> not transported";
    }

    /* ---- B: only optional column 'opt0' changes ---- */
    g_ctl = Ctl{ -1, 0, -1, g_ctl.ctr };
    {
        Value v = processAndPop(sub, evt);
        testDiag("B delta (opt col opt0):\n%s", deltaStr(v).c_str());
        testTrue (leafMarked(v, "opt0"))    << "B: opt0 changed -> transported";
        testFalse(leafMarked(v, "value.a")) << "B: value.a unchanged -> not transported";
        testFalse(leafMarked(v, "value.b")) << "B: value.b unchanged -> not transported";
        testFalse(leafMarked(v, "value.c")) << "B: value.c unchanged -> not transported";
        testFalse(leafMarked(v, "labels"))  << "B: labels unchanged -> not transported";
    }

    /* ---- C: only the label of column 'b' changes ---- */
    g_ctl = Ctl{ -1, -1, 1, g_ctl.ctr };
    {
        Value v = processAndPop(sub, evt);
        testDiag("C delta (label of col b):\n%s", deltaStr(v).c_str());
        testTrue (leafMarked(v, "labels"))  << "C: labels changed -> transported";
        testFalse(leafMarked(v, "value.a")) << "C: value.a unchanged -> not transported";
        testFalse(leafMarked(v, "value.b")) << "C: value.b unchanged -> not transported";
        testFalse(leafMarked(v, "value.c")) << "C: value.c unchanged -> not transported";
        testFalse(leafMarked(v, "opt0"))    << "C: opt0 unchanged -> not transported";
    }

    /* ---- D: nothing changes ---- */
    g_ctl = Ctl{ -1, -1, -1, g_ctl.ctr };
    {
        Value v = processAndPop(sub, evt);
        testDiag("D delta (no change):\n%s", deltaStr(v).c_str());
        testFalse(leafMarked(v, "value.a")) << "D: value.a not transported";
        testFalse(leafMarked(v, "value.b")) << "D: value.b not transported";
        testFalse(leafMarked(v, "value.c")) << "D: value.c not transported";
        testFalse(leafMarked(v, "labels"))  << "D: labels not transported";
        testFalse(leafMarked(v, "opt0"))    << "D: opt0 not transported";
    }

    /* ---- E: a data column changes but the (now-stable) label is NOT re-sent ---- */
    g_ctl = Ctl{ 1, -1, -1, g_ctl.ctr };
    {
        Value v = processAndPop(sub, evt);
        testDiag("E delta (data col b, stable label):\n%s", deltaStr(v).c_str());
        testTrue (leafMarked(v, "value.b")) << "E: value.b changed -> transported";
        testFalse(leafMarked(v, "labels"))  << "E: stable labels not re-transported";
        testFalse(leafMarked(v, "value.a")) << "E: value.a unchanged -> not transported";
        testFalse(leafMarked(v, "value.c")) << "E: value.c unchanged -> not transported";
        testFalse(leafMarked(v, "opt0"))    << "E: opt0 unchanged -> not transported";
    }

    sub.reset();
    return testDone();
}
