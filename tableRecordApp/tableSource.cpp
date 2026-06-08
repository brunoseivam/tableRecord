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
            arr[r] = (const char *)buf + r * MAX_STRING_SIZE;
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
    const char   *name;
    const char   *label;
    epicsEnum16   type;
    void         *val;
    epicsUInt32   numrows;
    epicsUInt8    chgd;
};

static void getCols(tableRecord *prec, Col cols[16])
{
    cols[ 0] = {prec->c00name, prec->c00label, prec->c00type, prec->c00val, prec->c00nrows, prec->c00chgd};
    cols[ 1] = {prec->c01name, prec->c01label, prec->c01type, prec->c01val, prec->c01nrows, prec->c01chgd};
    cols[ 2] = {prec->c02name, prec->c02label, prec->c02type, prec->c02val, prec->c02nrows, prec->c02chgd};
    cols[ 3] = {prec->c03name, prec->c03label, prec->c03type, prec->c03val, prec->c03nrows, prec->c03chgd};
    cols[ 4] = {prec->c04name, prec->c04label, prec->c04type, prec->c04val, prec->c04nrows, prec->c04chgd};
    cols[ 5] = {prec->c05name, prec->c05label, prec->c05type, prec->c05val, prec->c05nrows, prec->c05chgd};
    cols[ 6] = {prec->c06name, prec->c06label, prec->c06type, prec->c06val, prec->c06nrows, prec->c06chgd};
    cols[ 7] = {prec->c07name, prec->c07label, prec->c07type, prec->c07val, prec->c07nrows, prec->c07chgd};
    cols[ 8] = {prec->c08name, prec->c08label, prec->c08type, prec->c08val, prec->c08nrows, prec->c08chgd};
    cols[ 9] = {prec->c09name, prec->c09label, prec->c09type, prec->c09val, prec->c09nrows, prec->c09chgd};
    cols[10] = {prec->c0aname, prec->c0alabel, prec->c0atype, prec->c0aval, prec->c0anrows, prec->c0achgd};
    cols[11] = {prec->c0bname, prec->c0blabel, prec->c0btype, prec->c0bval, prec->c0bnrows, prec->c0bchgd};
    cols[12] = {prec->c0cname, prec->c0clabel, prec->c0ctype, prec->c0cval, prec->c0cnrows, prec->c0cchgd};
    cols[13] = {prec->c0dname, prec->c0dlabel, prec->c0dtype, prec->c0dval, prec->c0dnrows, prec->c0dchgd};
    cols[14] = {prec->c0ename, prec->c0elabel, prec->c0etype, prec->c0eval, prec->c0enrows, prec->c0echgd};
    cols[15] = {prec->c0fname, prec->c0flabel, prec->c0ftype, prec->c0fval, prec->c0fnrows, prec->c0fchgd};
}

struct OptCol {
    const char   *name;
    epicsEnum16   type;
    void         *val;
    epicsUInt32   numrows;
    epicsUInt8    chgd;
};

static void getOptCols(tableRecord *prec, OptCol cols[16])
{
    cols[ 0] = {prec->co00name, prec->co00type, prec->co00val, prec->co00nrows, prec->co00chgd};
    cols[ 1] = {prec->co01name, prec->co01type, prec->co01val, prec->co01nrows, prec->co01chgd};
    cols[ 2] = {prec->co02name, prec->co02type, prec->co02val, prec->co02nrows, prec->co02chgd};
    cols[ 3] = {prec->co03name, prec->co03type, prec->co03val, prec->co03nrows, prec->co03chgd};
    cols[ 4] = {prec->co04name, prec->co04type, prec->co04val, prec->co04nrows, prec->co04chgd};
    cols[ 5] = {prec->co05name, prec->co05type, prec->co05val, prec->co05nrows, prec->co05chgd};
    cols[ 6] = {prec->co06name, prec->co06type, prec->co06val, prec->co06nrows, prec->co06chgd};
    cols[ 7] = {prec->co07name, prec->co07type, prec->co07val, prec->co07nrows, prec->co07chgd};
    cols[ 8] = {prec->co08name, prec->co08type, prec->co08val, prec->co08nrows, prec->co08chgd};
    cols[ 9] = {prec->co09name, prec->co09type, prec->co09val, prec->co09nrows, prec->co09chgd};
    cols[10] = {prec->co0aname, prec->co0atype, prec->co0aval, prec->co0anrows, prec->co0achgd};
    cols[11] = {prec->co0bname, prec->co0btype, prec->co0bval, prec->co0bnrows, prec->co0bchgd};
    cols[12] = {prec->co0cname, prec->co0ctype, prec->co0cval, prec->co0cnrows, prec->co0cchgd};
    cols[13] = {prec->co0dname, prec->co0dtype, prec->co0dval, prec->co0dnrows, prec->co0dchgd};
    cols[14] = {prec->co0ename, prec->co0etype, prec->co0eval, prec->co0enrows, prec->co0echgd};
    cols[15] = {prec->co0fname, prec->co0ftype, prec->co0fval, prec->co0fnrows, prec->co0fchgd};
}

/* Snapshot a table record into a Value clone (caller holds lock).
   partial=true: only serialize (mark) columns whose chgd flag is set, so a
   monitor update carries only the columns that changed this process cycle.
   partial=false: serialize all active columns (GET, initial monitor snapshot).
   Each serialized column is padded to the table-wide max row count so the
   NTTable stays internally consistent. */
static void snapshotTable(tableRecord *prec, pvxs::Value &v, bool partial)
{
    Col cols[16];
    getCols(prec, cols);

    /* NTTable requires uniform column length: use the maximum across all data
       columns (not just the changed ones) so a partial update's columns stay
       consistent with the unchanged columns the client already holds. */
    epicsUInt32 nrow = 0;
    for (epicsUInt32 i = 0; i < prec->numcols; i++) {
        if (cols[i].numrows > nrow)
            nrow = cols[i].numrows;
    }

    for (epicsUInt32 i = 0; i < prec->numcols; i++) {
        if (partial && !cols[i].chgd) continue;
        auto col = v["value"][cols[i].name];
        fillCol(col, cols[i].type, cols[i].val, cols[i].numrows, nrow);
    }

    OptCol optcols[16];
    getOptCols(prec, optcols);
    for (epicsUInt32 i = 0; i < prec->numoptcols; i++) {
        if (!optcols[i].name || !optcols[i].name[0]) continue;
        if (partial && !optcols[i].chgd) continue;
        /* pvxs uses '.' as path separator, so "meta.field" accesses v["meta"]["field"] */
        auto col = v[optcols[i].name];
        fillCol(col, optcols[i].type, optcols[i].val, optcols[i].numrows, nrow);
    }
}

/* Build a snapshot Value from the current record state.
   withMeta=true: include static metadata (labels, descriptor) that only needs
   to be sent once per client — on the initial monitor post and every GET. */
static pvxs::Value snapshot(dbCommon *prec, const pvxs::Value &proto,
                             bool withMeta = false)
{
    pvxs::Value v = withMeta ? proto.clone() : proto.cloneEmpty();
    /* prec->time is in the EPICS epoch (1990); PVA timestamps are POSIX (1970). */
    v["timeStamp.secondsPastEpoch"] =
        (int64_t)prec->time.secPastEpoch + POSIX_TIME_AT_EPICS_EPOCH;
    v["timeStamp.nanoseconds"]      = (int32_t)prec->time.nsec;
    if (withMeta)
        v["descriptor"] = std::string(prec->desc);
    snapshotTable((tableRecord *)prec, v, /*partial=*/ !withMeta);
    return v;
}

struct ColPut {
    const char   *name;
    epicsEnum16   type;
    void        **val;
    epicsUInt8   *chgd;
    epicsUInt32  *numrows;
};

static void getColsPut(tableRecord *prec, ColPut cols[16])
{
    cols[ 0] = {prec->c00name, prec->c00type, &prec->c00val, &prec->c00chgd, &prec->c00nrows};
    cols[ 1] = {prec->c01name, prec->c01type, &prec->c01val, &prec->c01chgd, &prec->c01nrows};
    cols[ 2] = {prec->c02name, prec->c02type, &prec->c02val, &prec->c02chgd, &prec->c02nrows};
    cols[ 3] = {prec->c03name, prec->c03type, &prec->c03val, &prec->c03chgd, &prec->c03nrows};
    cols[ 4] = {prec->c04name, prec->c04type, &prec->c04val, &prec->c04chgd, &prec->c04nrows};
    cols[ 5] = {prec->c05name, prec->c05type, &prec->c05val, &prec->c05chgd, &prec->c05nrows};
    cols[ 6] = {prec->c06name, prec->c06type, &prec->c06val, &prec->c06chgd, &prec->c06nrows};
    cols[ 7] = {prec->c07name, prec->c07type, &prec->c07val, &prec->c07chgd, &prec->c07nrows};
    cols[ 8] = {prec->c08name, prec->c08type, &prec->c08val, &prec->c08chgd, &prec->c08nrows};
    cols[ 9] = {prec->c09name, prec->c09type, &prec->c09val, &prec->c09chgd, &prec->c09nrows};
    cols[10] = {prec->c0aname, prec->c0atype, &prec->c0aval, &prec->c0achgd, &prec->c0anrows};
    cols[11] = {prec->c0bname, prec->c0btype, &prec->c0bval, &prec->c0bchgd, &prec->c0bnrows};
    cols[12] = {prec->c0cname, prec->c0ctype, &prec->c0cval, &prec->c0cchgd, &prec->c0cnrows};
    cols[13] = {prec->c0dname, prec->c0dtype, &prec->c0dval, &prec->c0dchgd, &prec->c0dnrows};
    cols[14] = {prec->c0ename, prec->c0etype, &prec->c0eval, &prec->c0echgd, &prec->c0enrows};
    cols[15] = {prec->c0fname, prec->c0ftype, &prec->c0fval, &prec->c0fchgd, &prec->c0fnrows};
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
        *cols[i].numrows = nout;
        *cols[i].chgd = 1;
    }
}

/* Build NTTable prototype from record metadata (must be called under lock) */
pvxs::Value TableSource::makeProto(dbCommon *pcommon) const
{
    pvxs::nt::NTTable builder;
    char fallback[32];
    tableRecord *prec = (tableRecord *)pcommon;
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
                ctx->proto = makeProto(prec);
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

    TableRecCtx *ctx  = it->second;
    dbCommon    *prec = ctx->prec;
    pvxs::Value  proto = ctx->proto;

    /* GET / PUT */
    chan->onOp([prec, proto](std::unique_ptr<pvxs::server::ConnectOp> &&op) {
        op->connect(proto);

        op->onGet([prec, proto](std::unique_ptr<pvxs::server::ExecOp> &&get) {
            try {
                pvxs::Value v;
                {
                    RecLock lk(prec);
                    v = snapshot(prec, proto, true);
                }
                get->reply(v);
            } catch (std::exception &e) {
                get->error(e.what());
            }
        });

        op->onPut([prec](std::unique_ptr<pvxs::server::ExecOp> &&put,
                       pvxs::Value &&val) {
            try {
                {
                    RecLock lk(prec);
                    putValueTable((tableRecord *)prec, val);
                    dbProcess(prec);   /* process() drives the update via rpvt */
                }
                put->reply();
            } catch (std::exception &e) {
                put->error(e.what());
            }
        });
    });

    /* MONITOR — register the subscription in the record context; updates are
       posted from process() via onProcess(). No dbEvent involved. */
    chan->onSubscribe([ctx, proto](
                          std::unique_ptr<pvxs::server::MonitorSetupOp> &&sub) {
        try {
            auto sc   = std::make_shared<SubCtx>();
            sc->owner = ctx;
            sc->ctrl  = sub->connect(proto);

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
                        sc->ctrl->post(snapshot(ctx->prec, ctx->proto, true));
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
    dbCommon *prec = (dbCommon *)prect;
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
        pvxs::Value v = snapshot(prec, ctx->proto, /*withMeta=*/false);
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
