#include <cstring>
#include <stdexcept>
#include <memory>

#include <dbAccess.h>
#include <dbChannel.h>
#include <dbEvent.h>
#include <dbFldTypes.h>
#include <dbLock.h>
#include <dbStaticLib.h>
#include <epicsString.h>

#include <pvxs/log.h>
#include <pvxs/nt.h>

#include "tableSource.h"
#include "tableARecord.h"
#include "tableBRecord.h"

DEFINE_LOGGER(tlog, "pvxs.table.source");

namespace table {

/* Simple RAII record lock using the EPICS dbScanLock/dbScanUnlock API */
struct RecLock {
    dbCommon *prec_;
    explicit RecLock(dbCommon *p) : prec_(p) { dbScanLock(p); }
    ~RecLock() { dbScanUnlock(prec_); }
};

/* Map EPICS menuFtype → pvxs scalar TypeCode */
static pvxs::TypeCode ftypeToTC(epicsEnum16 ftvl)
{
    switch (ftvl) {
    case DBF_STRING: return pvxs::TypeCode::String;
    case DBF_CHAR:   return pvxs::TypeCode::Int8;
    case DBF_UCHAR:  return pvxs::TypeCode::UInt8;
    case DBF_SHORT:  return pvxs::TypeCode::Int16;
    case DBF_USHORT: return pvxs::TypeCode::UInt16;
    case DBF_LONG:   return pvxs::TypeCode::Int32;
    case DBF_ULONG:  return pvxs::TypeCode::UInt32;
    case DBF_INT64:  return pvxs::TypeCode::Int64;
    case DBF_UINT64: return pvxs::TypeCode::UInt64;
    case DBF_FLOAT:  return pvxs::TypeCode::Float32;
    case DBF_DOUBLE: return pvxs::TypeCode::Float64;
    default:         return pvxs::TypeCode::UInt8;
    }
}

/* Copy one column buffer into a pvxs Value column field (called under lock) */
static void fillCol(pvxs::Value col, epicsEnum16 ftvl, const void *buf, epicsUInt32 nrow)
{
    if (!col.valid() || !buf || nrow == 0) return;

    if (ftvl == DBF_STRING) {
        pvxs::shared_array<std::string> arr(nrow);
        for (epicsUInt32 r = 0; r < nrow; r++)
            arr[r] = (const char *)buf + r * MAX_STRING_SIZE;
        col.from(arr.freeze());
        return;
    }

    switch (ftypeToTC(ftvl).code) {
#define COPY(TC_VAL, CTYPE) \
    case pvxs::TypeCode::TC_VAL: { \
        pvxs::shared_array<CTYPE> arr(nrow); \
        memcpy(arr.data(), buf, nrow * sizeof(CTYPE)); \
        col.from(arr.freeze()); break; }
    COPY(Int8,    epicsInt8)
    COPY(UInt8,   epicsUInt8)
    COPY(Int16,   epicsInt16)
    COPY(UInt16,  epicsUInt16)
    COPY(Int32,   epicsInt32)
    COPY(UInt32,  epicsUInt32)
    COPY(Int64,   int64_t)
    COPY(UInt64,  uint64_t)
    COPY(Float32, epicsFloat32)
    COPY(Float64, epicsFloat64)
#undef COPY
    default: break;
    }
}

/* Extract data from a pvxs Value column and write into a record buffer (under lock) */
static void drainCol(pvxs::Value col, epicsEnum16 ftvl, void *buf,
                     epicsUInt32 maxelm, epicsUInt32 &nout)
{
    if (!col.valid() || !buf) return;

    if (ftvl == DBF_STRING) {
        auto arr = col.as<pvxs::shared_array<const std::string>>();
        epicsUInt32 n = (epicsUInt32)arr.size();
        if (n > maxelm) n = maxelm;
        for (epicsUInt32 r = 0; r < n; r++) {
            char *dst = (char *)buf + r * MAX_STRING_SIZE;
            strncpy(dst, arr[r].c_str(), MAX_STRING_SIZE - 1);
            dst[MAX_STRING_SIZE - 1] = '\0';
        }
        nout = n;
        return;
    }

    switch (ftypeToTC(ftvl).code) {
#define PUT(TC_VAL, CTYPE) \
    case pvxs::TypeCode::TC_VAL: { \
        auto arr = col.as<pvxs::shared_array<const CTYPE>>(); \
        epicsUInt32 n = (epicsUInt32)arr.size(); \
        if (n > maxelm) n = maxelm; \
        memcpy(buf, arr.data(), n * sizeof(CTYPE)); \
        nout = n; break; }
    PUT(Int8,    epicsInt8)
    PUT(UInt8,   epicsUInt8)
    PUT(Int16,   epicsInt16)
    PUT(UInt16,  epicsUInt16)
    PUT(Int32,   epicsInt32)
    PUT(UInt32,  epicsUInt32)
    PUT(Int64,   int64_t)
    PUT(UInt64,  uint64_t)
    PUT(Float32, epicsFloat32)
    PUT(Float64, epicsFloat64)
#undef PUT
    default: break;
    }
}

/* Snapshot a tableA record into a Value clone (caller holds lock) */
static void snapshotA(tableARecord *prec, pvxs::Value &v)
{
    void *colbufs[] = {
        prec->col00, prec->col01, prec->col02, prec->col03,
        prec->col04, prec->col05, prec->col06, prec->col07
    };
    for (epicsUInt32 i = 0; i < prec->ncol; i++) {
        const char *cname = prec->colnames + i * MAX_STRING_SIZE;
        auto col = v["value"][cname];
        fillCol(col, prec->coltypes[i], colbufs[i], prec->nrow);
    }
}

struct ColB {
    const char  *name;
    const char  *label;
    epicsEnum16  ftvl;
    void        *val;
};
static void getColsB(tableBRecord *prec, ColB cols[8])
{
    cols[0] = {prec->col00name, prec->col00label, prec->col00ftvl, prec->col00val};
    cols[1] = {prec->col01name, prec->col01label, prec->col01ftvl, prec->col01val};
    cols[2] = {prec->col02name, prec->col02label, prec->col02ftvl, prec->col02val};
    cols[3] = {prec->col03name, prec->col03label, prec->col03ftvl, prec->col03val};
    cols[4] = {prec->col04name, prec->col04label, prec->col04ftvl, prec->col04val};
    cols[5] = {prec->col05name, prec->col05label, prec->col05ftvl, prec->col05val};
    cols[6] = {prec->col06name, prec->col06label, prec->col06ftvl, prec->col06val};
    cols[7] = {prec->col07name, prec->col07label, prec->col07ftvl, prec->col07val};
}

/* Snapshot a tableB record into a Value clone (caller holds lock) */
static void snapshotB(tableBRecord *prec, pvxs::Value &v)
{
    ColB cols[8];
    getColsB(prec, cols);
    for (epicsUInt32 i = 0; i < prec->numcols; i++) {
        auto col = v["value"][cols[i].name];
        fillCol(col, cols[i].ftvl, cols[i].val, prec->numrows);
    }
}

/* Build a snapshot Value from the current record state.
   withMeta=true: include static metadata (labels, descriptor) that only needs
   to be sent once per client — on the initial monitor post and every GET.
   The client's cache_sync mechanism re-populates these from its accumulated
   prototype for all subsequent monitor updates.
   timeStamp is included in every snapshot regardless of withMeta. */
static pvxs::Value snapshot(const RecInfo &ri, const pvxs::Value &proto,
                             bool withMeta = false)
{
    pvxs::Value v = withMeta ? proto.clone() : proto.cloneEmpty();
    v["timeStamp.secondsPastEpoch"] = (int64_t)ri.prec->time.secPastEpoch;
    v["timeStamp.nanoseconds"]      = (int32_t)ri.prec->time.nsec;
    if (withMeta)
        v["descriptor"] = std::string(ri.prec->desc);
    if (std::strcmp(ri.type, "tableA") == 0)
        snapshotA((tableARecord *)ri.prec, v);
    else
        snapshotB((tableBRecord *)ri.prec, v);
    return v;
}

/* Write NTTable value into a tableA record (caller holds lock + calls dbProcess) */
static void putValueA(tableARecord *prec, const pvxs::Value &val)
{
    void *colbufs[] = {
        prec->col00, prec->col01, prec->col02, prec->col03,
        prec->col04, prec->col05, prec->col06, prec->col07
    };
    for (epicsUInt32 i = 0; i < prec->ncol; i++) {
        const char *cname = prec->colnames + i * MAX_STRING_SIZE;
        auto col = val["value"][cname];
        if (!col.valid() || !col.isMarked(true, true)) continue;
        epicsUInt32 nout = 0;
        drainCol(col, prec->coltypes[i], colbufs[i], prec->maxrows, nout);
        prec->nrow = nout;
        prec->chgd[i] = 1;
    }
}

struct ColBPut {
    const char  *name;
    epicsEnum16  ftvl;
    void       **val;
    epicsUInt8  *chgd;
};
static void getColsBPut(tableBRecord *prec, ColBPut cols[8])
{
    cols[0] = {prec->col00name, prec->col00ftvl, &prec->col00val, &prec->col00chgd};
    cols[1] = {prec->col01name, prec->col01ftvl, &prec->col01val, &prec->col01chgd};
    cols[2] = {prec->col02name, prec->col02ftvl, &prec->col02val, &prec->col02chgd};
    cols[3] = {prec->col03name, prec->col03ftvl, &prec->col03val, &prec->col03chgd};
    cols[4] = {prec->col04name, prec->col04ftvl, &prec->col04val, &prec->col04chgd};
    cols[5] = {prec->col05name, prec->col05ftvl, &prec->col05val, &prec->col05chgd};
    cols[6] = {prec->col06name, prec->col06ftvl, &prec->col06val, &prec->col06chgd};
    cols[7] = {prec->col07name, prec->col07ftvl, &prec->col07val, &prec->col07chgd};
}

/* Write NTTable value into a tableB record (caller holds lock + calls dbProcess) */
static void putValueB(tableBRecord *prec, const pvxs::Value &val)
{
    ColBPut cols[8];
    getColsBPut(prec, cols);
    for (epicsUInt32 i = 0; i < prec->numcols; i++) {
        auto col = val["value"][cols[i].name];
        if (!col.valid() || !col.isMarked(true, true)) continue;
        epicsUInt32 nout = 0;
        drainCol(col, cols[i].ftvl, *cols[i].val, prec->maxrows, nout);
        if (nout > prec->numrows) prec->numrows = nout;
        *cols[i].chgd = 1;
    }
}

/* EPICS event callback — fires on the event thread after a record processes */
static void eventCallback(void *userArg, struct dbChannel * /*chan*/,
                           int /*eventsRemaining*/, struct db_field_log * /*pfl*/)
{
    SubCtx *ctx = static_cast<SubCtx *>(userArg);
    if (!ctx->ctrl) return;

    try {
        pvxs::Value v;
        {
            RecLock lk(ctx->ri.prec);
            v = snapshot(ctx->ri, ctx->proto);
        }
        ctx->ctrl->post(v);
    } catch (std::exception &e) {
        log_exc_printf(tlog, "eventCallback: %s\n", e.what());
    }
}

/* Build NTTable prototype from record metadata (must be called under lock) */
pvxs::Value TableSource::makeProto(const RecInfo &ri) const
{
    pvxs::nt::NTTable builder;
    char fallback[32];
    if (std::strcmp(ri.type, "tableA") == 0) {
        tableARecord *prec = (tableARecord *)ri.prec;
        for (epicsUInt32 i = 0; i < prec->ncol; i++) {
            const char *name = prec->colnames + i * MAX_STRING_SIZE;
            if (!name[0]) {
                snprintf(fallback, sizeof(fallback), "col%u", i);
                name = fallback;
            }
            const char *label = prec->collabels + i * MAX_STRING_SIZE;
            if (!label[0]) label = name;
            builder.add_column(ftypeToTC(prec->coltypes[i]), name, label);
        }
    } else {
        tableBRecord *prec = (tableBRecord *)ri.prec;
        ColB cols[8];
        getColsB(prec, cols);
        for (epicsUInt32 i = 0; i < prec->numcols; i++) {
            const char *name = cols[i].name;
            if (!name || !name[0]) {
                snprintf(fallback, sizeof(fallback), "col%u", i);
                name = fallback;
            }
            const char *label = cols[i].label;
            if (!label || !label[0]) label = name;
            builder.add_column(ftypeToTC(cols[i].ftvl), name, label);
        }
    }
    return builder.create();
}

/* ------------------------------------------------------------------ */

TableSource::TableSource()
    : eventCtx_(nullptr)
{
    auto names = std::make_shared<std::set<std::string>>();
    DBENTRY dbe;
    dbInitEntry(pdbbase, &dbe);

    const char *rtypes[] = {"tableA", "tableB", nullptr};
    for (int ti = 0; rtypes[ti]; ti++) {
        const char *rtype = rtypes[ti];
        if (dbFindRecordType(&dbe, rtype) != 0)
            continue;
        for (long s = dbFirstRecord(&dbe); !s; s = dbNextRecord(&dbe)) {
            const char *rname = dbGetRecordName(&dbe);
            dbCommon *prec = (dbCommon *)dbe.precnode->precord;
            records_[rname] = RecInfo{rtype, prec};
            names->insert(rname);
        }
    }
    dbFinishEntry(&dbe);
    names_ = std::move(names);

    eventCtx_ = db_init_events();
    if (!eventCtx_)
        throw std::runtime_error("TableSource: db_init_events() failed");
    if (db_start_events(eventCtx_, "tableSrc", nullptr, nullptr,
                        epicsThreadPriorityCAServerLow - 1))
        throw std::runtime_error("TableSource: db_start_events() failed");
}

TableSource::~TableSource()
{
    if (eventCtx_)
        db_close_events(eventCtx_);
}

void TableSource::onSearch(Search &op)
{
    for (auto &pv : op) {
        if (records_.count(pv.name()))
            pv.claim();
    }
}

void TableSource::onCreate(std::unique_ptr<pvxs::server::ChannelControl> &&chan)
{
    auto it = records_.find(chan->name());
    if (it == records_.end())
        return;

    RecInfo ri = it->second;

    pvxs::Value proto;
    {
        RecLock lk(ri.prec);
        try {
            proto = makeProto(ri);
        } catch (std::exception &e) {
            log_err_printf(tlog, "makeProto failed for '%s': %s\n",
                           chan->name().c_str(), e.what());
            return;
        }
    }

    /* GET / PUT */
    chan->onOp([ri, proto](std::unique_ptr<pvxs::server::ConnectOp> &&op) {
        op->connect(proto);

        op->onGet([ri, proto](std::unique_ptr<pvxs::server::ExecOp> &&get) {
            try {
                pvxs::Value v;
                {
                    RecLock lk(ri.prec);
                    v = snapshot(ri, proto, true);
                }
                get->reply(v);
            } catch (std::exception &e) {
                get->error(e.what());
            }
        });

        op->onPut([ri](std::unique_ptr<pvxs::server::ExecOp> &&put,
                       pvxs::Value &&val) {
            try {
                {
                    RecLock lk(ri.prec);
                    if (std::strcmp(ri.type, "tableA") == 0)
                        putValueA((tableARecord *)ri.prec, val);
                    else
                        putValueB((tableBRecord *)ri.prec, val);
                    dbProcess(ri.prec);
                }
                put->reply();
            } catch (std::exception &e) {
                put->error(e.what());
            }
        });
    });

    /* MONITOR */
    dbEventCtx evtCtx = eventCtx_;
    chan->onSubscribe([ri, proto, evtCtx](
                          std::unique_ptr<pvxs::server::MonitorSetupOp> &&sub) {
        try {
            auto ctx      = std::make_shared<SubCtx>();
            ctx->ri       = ri;
            ctx->proto    = proto;
            ctx->evtCtx   = evtCtx;
            ctx->ctrl     = sub->connect(proto);

            /* Open a dbChannel to subscribe for events.
               tableB has no VAL field; use COL00VAL as the trigger instead. */
            std::string chname = ri.prec->name;
            chname += (std::strcmp(ri.type, "tableB") == 0) ? ".COL00VAL" : ".VAL";
            dbChannel *pChan = dbChannelCreate(chname.c_str());
            if (!pChan)
                pChan = dbChannelCreate(ri.prec->name);
            if (!pChan || dbChannelOpen(pChan)) {
                if (pChan) dbChannelDelete(pChan);
                sub->error("TableSource: cannot open dbChannel");
                return;
            }

            ctx->ctrl->onStart([ctx, pChan](bool start) mutable {
                if (start) {
                    ctx->evtSub = db_add_event(
                        ctx->evtCtx, pChan,
                        eventCallback, ctx.get(),
                        DBE_VALUE);
                    db_event_enable(ctx->evtSub);
                    /* Initial snapshot includes labels so the client's
                       cache_sync prototype is seeded; subsequent posts
                       via eventCallback omit them (cache_sync supplies them). */
                    try {
                        pvxs::Value v;
                        {
                            RecLock lk(ctx->ri.prec);
                            v = snapshot(ctx->ri, ctx->proto, true);
                        }
                        ctx->ctrl->post(v);
                    } catch (std::exception &e) {
                        log_exc_printf(tlog, "initial snapshot: %s\n", e.what());
                    }
                } else {
                    if (ctx->evtSub) {
                        db_cancel_event(ctx->evtSub);
                        ctx->evtSub = nullptr;
                    }
                    dbChannelDelete(pChan);
                }
            });

            sub->onClose([ctx](const std::string &) {
                if (ctx->evtSub) {
                    db_cancel_event(ctx->evtSub);
                    ctx->evtSub = nullptr;
                }
                ctx->ctrl.reset();
            });
        } catch (std::exception &e) {
            sub->error(e.what());
        }
    });
}

TableSource::List TableSource::onList()
{
    return List(names_);
}

} /* namespace table */
