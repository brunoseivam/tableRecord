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
#include <epicsTime.h>

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

/* Copy one column buffer into a pvxs Value column field (called under lock).
   numrows valid elements are copied from buf; the remaining rows up to nrow are
   padded (empty strings for DBF_STRING, zeros for numeric) so all columns in the
   published NTTable share a uniform length. */
static void fillCol(pvxs::Value col, epicsEnum16 type, const void *buf,
                    epicsUInt32 numrows, epicsUInt32 nrow)
{
    if (!col.valid()) return;
    if (!buf) numrows = 0;
    if (numrows > nrow) numrows = nrow;

    if (type == DBF_STRING) {
        /* shared_array<std::string>(nrow) default-constructs empty strings, so
           rows [numrows, nrow) are already "" — only fill the valid rows. */
        pvxs::shared_array<std::string> arr(nrow);
        for (epicsUInt32 r = 0; r < numrows; r++)
            arr[r] = TableRecordWrapper::vstr_read_cell(buf, r);
        col.from(arr.freeze());
        return;
    }

    switch (ftypeToTC(type).code) {
#define COPY(TC_VAL, CTYPE) \
    case pvxs::TypeCode::TC_VAL: { \
        pvxs::shared_array<CTYPE> arr(nrow); \
        if (numrows) memcpy(arr.data(), buf, numrows * sizeof(CTYPE)); \
        if (nrow > numrows) \
            memset(arr.data() + numrows, 0, (nrow - numrows) * sizeof(CTYPE)); \
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
        /* vstr_write_cell frees any existing overflow pointer before overwriting,
           so long→short→long round-trips don't leak.  Cells in [n, maxelm) keep
           their old contents but are invisible (numrows = n) and will be freed on
           the next write or tablerec_vstr_clear call. */
        for (epicsUInt32 r = 0; r < n; r++)
            TableRecordWrapper::vstr_write_cell(buf, r, arr[r]);
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

/* Snapshot a table record into a Value clone (caller holds lock).
   partial=true: only serialize (mark) columns whose chgd flag is set, so a
   monitor update carries only the columns that changed this process cycle.
   partial=false: serialize all active columns (GET, initial monitor snapshot).
   Each serialized column is padded to the table-wide max row count so the
   NTTable stays internally consistent. */
static void snapshotTable(const TableRecCtx &ctx, pvxs::Value &v, bool partial)
{
    /* NTTable requires uniform column length: use the maximum across all data
       columns (not just the changed ones) so a partial update's columns stay
       consistent with the unchanged columns the client already holds. */
    epicsUInt32 nrow = 0;
    for (const auto &c : ctx.dcols)
        if (*c.numrows > nrow)
            nrow = *c.numrows;

    for (const auto &c : ctx.dcols) {
        if (partial && !*c.chgd) continue;
        auto col = v["value"][c.config.name];
        fillCol(col, c.config.type, *c.val, *c.numrows, nrow);
    }

    /* Optional columns carry one value per data column, so their length is the
       number of active data columns (NUMCOLS), not the data row count (nrow). */
    epicsUInt32 ncols = (epicsUInt32)ctx.dcols.size();

    /* pvxs uses '.' as path separator, so "meta.field" accesses v["meta"]["field"] */
    for (const auto &c : ctx.ocols) {
        if (partial && !*c.chgd) continue;
        auto col = v[c.config.name];
        fillCol(col, c.config.type, *c.val, *c.numrows, ncols);
    }
}

/* Build a snapshot Value from the current record state.
   withMeta=true: include static metadata (labels, descriptor) that only needs
   to be sent once per client — on the initial monitor post and every GET. */
static pvxs::Value snapshot(const TableRecCtx &ctx, bool withMeta = false)
{
    pvxs::Value v = withMeta ? ctx.proto.clone() : ctx.proto.cloneEmpty();
    /* prec->time is in the EPICS epoch (1990); PVA timestamps are POSIX (1970). */
    v["timeStamp.secondsPastEpoch"] =
        (int64_t)ctx.prec->time.secPastEpoch + POSIX_TIME_AT_EPICS_EPOCH;
    v["timeStamp.nanoseconds"]      = (int32_t)ctx.prec->time.nsec;
    if (withMeta)
        v["descriptor"] = std::string(ctx.prec->desc);
    snapshotTable(ctx, v, /*partial=*/ !withMeta);
    return v;
}

/* Write NTTable value into a table record (caller holds lock + calls dbProcess) */
static void putValueTable(const TableRecCtx &ctx, const pvxs::Value &val)
{
    for (const auto &c : ctx.dcols) {
        auto col = val["value"][c.config.name];
        if (!col.valid() || !col.isMarked(true, true)) continue;
        epicsUInt32 nout = 0;
        drainCol(col, c.config.type, *c.val,
                 ((tableRecord *)ctx.prec)->maxrows, nout);
        *c.numrows = nout;
        *c.chgd = 1;
    }
}

/* Build NTTable prototype from record metadata (must be called under lock) */
pvxs::Value TableSource::makeProto(
    const std::vector<TableRecordWrapper::DataColumn> &dcols,
    const std::vector<TableRecordWrapper::OptColumn> &ocols) const
{
    pvxs::nt::NTTable builder;
    for (const auto &c : dcols) {
        const char *label = c.config.label.empty()
                          ? c.config.name.c_str() : c.config.label.c_str();
        builder.add_column(ftypeToTC(c.config.type), c.config.name.c_str(), label);
    }

    /* Extend the NTTable TypeDef with optional top-level fields.
       Names containing '.' (e.g. "meta.field") are grouped into a
       nested Struct named by the prefix; order of first occurrence is kept. */

    /* ordered list of group keys (empty key = top-level scalar fields) */
    std::vector<std::string> groupOrder;
    /* prefix → list of array Members within that group */
    std::map<std::string, std::vector<pvxs::Member>> groups;

    for (const auto &c : ocols) {
        std::string fname = c.config.name;
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
        pvxs::TypeCode arrCode = ftypeToTC(c.config.type).arrayOf();
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
{
    auto names = std::make_shared<std::set<std::string>>();
    DBENTRY dbe;
    dbInitEntry(pdbbase, &dbe);

    if (dbFindRecordType(&dbe, "table") == 0) {
        for (long s = dbFirstRecord(&dbe); !s; s = dbNextRecord(&dbe)) {
            const char *rname = dbGetRecordName(&dbe);
            dbCommon *prec = (dbCommon *)dbe.precnode->precord;

            std::unique_ptr<TableRecCtx> ctx(new TableRecCtx());
            ctx->prec       = prec;
            ctx->src        = this;
            ctx->hdr.notify = &TableSource::onProcess;
            ctx->hdr.self   = ctx.get();

            /* Build the NTTable prototype once — column metadata is fixed after
               device-support init, which has already run by the time
               addTableSource() constructs us. Publishing rpvt under the record
               lock makes it safe against a concurrent process(). */
            try {
                RecLock lk(prec);
                TableRecordWrapper w(prec);
                w.data_cols(ctx->dcols);
                w.opt_cols(ctx->ocols);
                ctx->proto = makeProto(ctx->dcols, ctx->ocols);
                ((tableRecord *)prec)->rpvt = &ctx->hdr;
            } catch (std::exception &e) {
                log_err_printf(tlog, "makeProto failed for '%s': %s\n",
                               rname, e.what());
                continue;
            }

            records_[rname] = ctx.get();
            names->insert(rname);
            ctxs_.push_back(std::move(ctx));
        }
    }
    dbFinishEntry(&dbe);
    names_ = std::move(names);
}

TableSource::~TableSource()
{
    /* Stop process() from calling into us before our state is destroyed. */
    for (auto &ctx : ctxs_) {
        RecLock lk(ctx->prec);
        ((tableRecord *)ctx->prec)->rpvt = nullptr;
    }
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

    TableRecCtx *ctx = it->second;

    /* GET / PUT */
    chan->onOp([ctx](std::unique_ptr<pvxs::server::ConnectOp> &&op) {
        op->connect(ctx->proto);

        op->onGet([ctx](std::unique_ptr<pvxs::server::ExecOp> &&get) {
            try {
                pvxs::Value v;
                {
                    RecLock lk(ctx->prec);
                    v = snapshot(*ctx, true);
                }
                get->reply(v);
            } catch (std::exception &e) {
                get->error(e.what());
            }
        });

        op->onPut([ctx](std::unique_ptr<pvxs::server::ExecOp> &&put,
                       pvxs::Value &&val) {
            try {
                {
                    RecLock lk(ctx->prec);
                    putValueTable(*ctx, val);
                    dbProcess(ctx->prec);   /* process() drives the update via rpvt */
                }
                put->reply();
            } catch (std::exception &e) {
                put->error(e.what());
            }
        });
    });

    /* MONITOR — register the subscription in the record context; updates are
       posted from process() via onProcess(). No dbEvent involved. */
    chan->onSubscribe([ctx](
                          std::unique_ptr<pvxs::server::MonitorSetupOp> &&sub) {
        try {
            auto sc   = std::make_shared<SubCtx>();
            sc->owner = ctx;
            sc->ctrl  = sub->connect(ctx->proto);

            sc->ctrl->onStart([ctx, sc](bool start) {
                if (start) {
                    /* Register and send the initial full snapshot while holding
                       both locks: the record lock keeps a concurrent process()
                       notify from interleaving, and ctx->mu makes the sub
                       visible to later notifies only once it has its baseline.
                       Lock order is record-lock -> ctx->mu, matching onProcess. */
                    try {
                        RecLock lk(ctx->prec);
                        std::lock_guard<std::mutex> g(ctx->mu);
                        ctx->subs.insert(sc);
                        sc->ctrl->post(snapshot(*ctx, true));
                    } catch (std::exception &e) {
                        log_exc_printf(tlog, "initial snapshot: %s\n", e.what());
                    }
                } else {
                    std::lock_guard<std::mutex> g(ctx->mu);
                    ctx->subs.erase(sc);
                }
            });

            sub->onClose([ctx, sc](const std::string &) {
                std::lock_guard<std::mutex> g(ctx->mu);
                ctx->subs.erase(sc);
                sc->ctrl.reset();
            });
        } catch (std::exception &e) {
            sub->error(e.what());
        }
    });
}

/* Synchronous update hook — installed in each record's rpvt->notify and called
   from tableRecord's process() with the record lock held and this cycle's CHGD
   flags valid. Builds one partial snapshot and posts it to every subscriber. */
void TableSource::onProcess(struct tableRecord *prect)
{
    if (!prect->rpvt)
        return;
    tableRecordPvt *hdr = static_cast<tableRecordPvt *>(prect->rpvt);
    TableRecCtx    *ctx = static_cast<TableRecCtx *>(hdr->self);
    if (!ctx)
        return;

    std::lock_guard<std::mutex> g(ctx->mu);
    if (ctx->subs.empty())
        return;
    try {
        pvxs::Value v = snapshot(*ctx, /*withMeta=*/false);
        for (auto &sc : ctx->subs)
            if (sc->ctrl)
                sc->ctrl->post(v);
    } catch (std::exception &e) {
        log_exc_printf(tlog, "onProcess: %s\n", e.what());
    }
}

TableSource::List TableSource::onList()
{
    return List(names_);
}

} /* namespace table */
