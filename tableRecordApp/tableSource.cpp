#include <cstring>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>
#include <map>

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
#include "tableRecord.h"

DEFINE_LOGGER(tlog, "pvxs.table.source");

namespace table {

/* Simple RAII record lock using the EPICS dbScanLock/dbScanUnlock API */
struct RecLock {
    dbCommon *prec_;
    explicit RecLock(dbCommon *p) : prec_(p) { dbScanLock(p); }
    ~RecLock() { dbScanUnlock(prec_); }
};

/* Map EPICS menuFtype → pvxs scalar TypeCode */
static pvxs::TypeCode ftypeToTC(epicsEnum16 type)
{
    switch (type) {
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
static void fillCol(pvxs::Value col, epicsEnum16 type, const void *buf, epicsUInt32 nrow)
{
    if (!col.valid() || !buf || nrow == 0) return;

    if (type == DBF_STRING) {
        pvxs::shared_array<std::string> arr(nrow);
        for (epicsUInt32 r = 0; r < nrow; r++)
            arr[r] = (const char *)buf + r * MAX_STRING_SIZE;
        col.from(arr.freeze());
        return;
    }

    switch (ftypeToTC(type).code) {
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
static void drainCol(pvxs::Value col, epicsEnum16 type, void *buf,
                     epicsUInt32 maxelm, epicsUInt32 &nout)
{
    if (!col.valid() || !buf) return;

    if (type == DBF_STRING) {
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

    switch (ftypeToTC(type).code) {
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

struct Col {
    const char  *name;
    const char  *label;
    epicsEnum16  type;
    void        *val;
};

static void getCols(tableRecord *prec, Col cols[16])
{
    cols[ 0] = {prec->col00name, prec->col00label, prec->col00type, prec->col00val};
    cols[ 1] = {prec->col01name, prec->col01label, prec->col01type, prec->col01val};
    cols[ 2] = {prec->col02name, prec->col02label, prec->col02type, prec->col02val};
    cols[ 3] = {prec->col03name, prec->col03label, prec->col03type, prec->col03val};
    cols[ 4] = {prec->col04name, prec->col04label, prec->col04type, prec->col04val};
    cols[ 5] = {prec->col05name, prec->col05label, prec->col05type, prec->col05val};
    cols[ 6] = {prec->col06name, prec->col06label, prec->col06type, prec->col06val};
    cols[ 7] = {prec->col07name, prec->col07label, prec->col07type, prec->col07val};
    cols[ 8] = {prec->col08name, prec->col08label, prec->col08type, prec->col08val};
    cols[ 9] = {prec->col09name, prec->col09label, prec->col09type, prec->col09val};
    cols[10] = {prec->col0aname, prec->col0alabel, prec->col0atype, prec->col0aval};
    cols[11] = {prec->col0bname, prec->col0blabel, prec->col0btype, prec->col0bval};
    cols[12] = {prec->col0cname, prec->col0clabel, prec->col0ctype, prec->col0cval};
    cols[13] = {prec->col0dname, prec->col0dlabel, prec->col0dtype, prec->col0dval};
    cols[14] = {prec->col0ename, prec->col0elabel, prec->col0etype, prec->col0eval};
    cols[15] = {prec->col0fname, prec->col0flabel, prec->col0ftype, prec->col0fval};
}

struct OptCol {
    const char  *name;
    epicsEnum16  type;
    void        *val;
};

static void getOptCols(tableRecord *prec, OptCol cols[16])
{
    cols[ 0] = {prec->colopt00name, prec->colopt00type, prec->colopt00val};
    cols[ 1] = {prec->colopt01name, prec->colopt01type, prec->colopt01val};
    cols[ 2] = {prec->colopt02name, prec->colopt02type, prec->colopt02val};
    cols[ 3] = {prec->colopt03name, prec->colopt03type, prec->colopt03val};
    cols[ 4] = {prec->colopt04name, prec->colopt04type, prec->colopt04val};
    cols[ 5] = {prec->colopt05name, prec->colopt05type, prec->colopt05val};
    cols[ 6] = {prec->colopt06name, prec->colopt06type, prec->colopt06val};
    cols[ 7] = {prec->colopt07name, prec->colopt07type, prec->colopt07val};
    cols[ 8] = {prec->colopt08name, prec->colopt08type, prec->colopt08val};
    cols[ 9] = {prec->colopt09name, prec->colopt09type, prec->colopt09val};
    cols[10] = {prec->colopt0aname, prec->colopt0atype, prec->colopt0aval};
    cols[11] = {prec->colopt0bname, prec->colopt0btype, prec->colopt0bval};
    cols[12] = {prec->colopt0cname, prec->colopt0ctype, prec->colopt0cval};
    cols[13] = {prec->colopt0dname, prec->colopt0dtype, prec->colopt0dval};
    cols[14] = {prec->colopt0ename, prec->colopt0etype, prec->colopt0eval};
    cols[15] = {prec->colopt0fname, prec->colopt0ftype, prec->colopt0fval};
}

/* Snapshot a table record into a Value clone (caller holds lock) */
static void snapshotTable(tableRecord *prec, pvxs::Value &v)
{
    Col cols[16];
    getCols(prec, cols);
    for (epicsUInt32 i = 0; i < prec->numcols; i++) {
        auto col = v["value"][cols[i].name];
        fillCol(col, cols[i].type, cols[i].val, prec->numrows);
    }

    OptCol optcols[16];
    getOptCols(prec, optcols);
    for (epicsUInt32 i = 0; i < prec->numoptcols; i++) {
        if (!optcols[i].name || !optcols[i].name[0]) continue;
        /* pvxs uses '.' as path separator, so "meta.field" accesses v["meta"]["field"] */
        auto col = v[optcols[i].name];
        fillCol(col, optcols[i].type, optcols[i].val, prec->numrows);
    }
}

/* Build a snapshot Value from the current record state.
   withMeta=true: include static metadata (labels, descriptor) that only needs
   to be sent once per client — on the initial monitor post and every GET. */
static pvxs::Value snapshot(const RecInfo &ri, const pvxs::Value &proto,
                             bool withMeta = false)
{
    pvxs::Value v = withMeta ? proto.clone() : proto.cloneEmpty();
    v["timeStamp.secondsPastEpoch"] = (int64_t)ri.prec->time.secPastEpoch;
    v["timeStamp.nanoseconds"]      = (int32_t)ri.prec->time.nsec;
    if (withMeta)
        v["descriptor"] = std::string(ri.prec->desc);
    snapshotTable((tableRecord *)ri.prec, v);
    return v;
}

struct ColPut {
    const char  *name;
    epicsEnum16  type;
    void       **val;
    epicsUInt8  *chgd;
};

static void getColsPut(tableRecord *prec, ColPut cols[16])
{
    cols[ 0] = {prec->col00name, prec->col00type, &prec->col00val, &prec->col00chgd};
    cols[ 1] = {prec->col01name, prec->col01type, &prec->col01val, &prec->col01chgd};
    cols[ 2] = {prec->col02name, prec->col02type, &prec->col02val, &prec->col02chgd};
    cols[ 3] = {prec->col03name, prec->col03type, &prec->col03val, &prec->col03chgd};
    cols[ 4] = {prec->col04name, prec->col04type, &prec->col04val, &prec->col04chgd};
    cols[ 5] = {prec->col05name, prec->col05type, &prec->col05val, &prec->col05chgd};
    cols[ 6] = {prec->col06name, prec->col06type, &prec->col06val, &prec->col06chgd};
    cols[ 7] = {prec->col07name, prec->col07type, &prec->col07val, &prec->col07chgd};
    cols[ 8] = {prec->col08name, prec->col08type, &prec->col08val, &prec->col08chgd};
    cols[ 9] = {prec->col09name, prec->col09type, &prec->col09val, &prec->col09chgd};
    cols[10] = {prec->col0aname, prec->col0atype, &prec->col0aval, &prec->col0achgd};
    cols[11] = {prec->col0bname, prec->col0btype, &prec->col0bval, &prec->col0bchgd};
    cols[12] = {prec->col0cname, prec->col0ctype, &prec->col0cval, &prec->col0cchgd};
    cols[13] = {prec->col0dname, prec->col0dtype, &prec->col0dval, &prec->col0dchgd};
    cols[14] = {prec->col0ename, prec->col0etype, &prec->col0eval, &prec->col0echgd};
    cols[15] = {prec->col0fname, prec->col0ftype, &prec->col0fval, &prec->col0fchgd};
}

/* Write NTTable value into a table record (caller holds lock + calls dbProcess) */
static void putValueTable(tableRecord *prec, const pvxs::Value &val)
{
    ColPut cols[16];
    getColsPut(prec, cols);
    for (epicsUInt32 i = 0; i < prec->numcols; i++) {
        auto col = val["value"][cols[i].name];
        if (!col.valid() || !col.isMarked(true, true)) continue;
        epicsUInt32 nout = 0;
        drainCol(col, cols[i].type, *cols[i].val, prec->maxrows, nout);
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
    tableRecord *prec = (tableRecord *)ri.prec;
    Col cols[16];
    getCols(prec, cols);
    for (epicsUInt32 i = 0; i < prec->numcols; i++) {
        const char *name = cols[i].name;
        if (!name || !name[0]) {
            snprintf(fallback, sizeof(fallback), "col%u", i);
            name = fallback;
        }
        const char *label = cols[i].label;
        if (!label || !label[0]) label = name;
        builder.add_column(ftypeToTC(cols[i].type), name, label);
    }

    /* Extend the NTTable TypeDef with optional top-level fields.
       Names containing '.' (e.g. "meta.field") are grouped into a
       nested Struct named by the prefix; order of first occurrence is kept. */
    OptCol optcols[16];
    getOptCols(prec, optcols);

    /* ordered list of group keys (empty key = top-level scalar fields) */
    std::vector<std::string> groupOrder;
    /* prefix → list of array Members within that group */
    std::map<std::string, std::vector<pvxs::Member>> groups;

    for (epicsUInt32 i = 0; i < prec->numoptcols; i++) {
        const char *fullname = optcols[i].name;
        if (!fullname || !fullname[0]) continue;

        std::string fname(fullname);
        auto dot = fname.find('.');
        std::string prefix, fieldname;
        if (dot == std::string::npos) {
            prefix    = "";
            fieldname = fname;
        } else {
            prefix    = fname.substr(0, dot);
            fieldname = fname.substr(dot + 1);
        }

        if (groups.find(prefix) == groups.end()) {
            groupOrder.push_back(prefix);
            groups[prefix] = {};
        }
        pvxs::TypeCode arrCode = ftypeToTC(optcols[i].type).arrayOf();
        groups[prefix].emplace_back(arrCode, fieldname);
    }

    if (!groupOrder.empty()) {
        std::vector<pvxs::Member> extras;
        for (const auto &key : groupOrder) {
            auto &members = groups[key];
            if (key.empty()) {
                /* top-level scalar-array fields */
                for (auto &m : members)
                    extras.push_back(m);
            } else {
                extras.emplace_back(pvxs::TypeCode::Struct, key, members);
            }
        }
        auto def = builder.build();
        def += extras;
        return def.create();
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

    if (dbFindRecordType(&dbe, "table") == 0) {
        for (long s = dbFirstRecord(&dbe); !s; s = dbNextRecord(&dbe)) {
            const char *rname = dbGetRecordName(&dbe);
            dbCommon *prec = (dbCommon *)dbe.precnode->precord;
            records_[rname] = RecInfo{prec};
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
                    putValueTable((tableRecord *)ri.prec, val);
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

            /* Open a dbChannel on COL00VAL to subscribe for value-change events. */
            std::string chname = std::string(ri.prec->name) + ".COL00VAL";
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
                       via eventCallback omit them. */
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
