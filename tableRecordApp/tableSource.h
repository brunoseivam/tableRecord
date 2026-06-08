#ifndef TABLE_SOURCE_H
#define TABLE_SOURCE_H

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <dbCommon.h>

#include <pvxs/source.h>
#include <pvxs/server.h>
#include <pvxs/nt.h>

#include "tableRecordHook.h"
#include "tableRecordUtil.h"

namespace table {

struct TableRecCtx;

/* Per-subscription state: just the pvxs monitor control and a back-pointer to
   the owning record context (so onClose can deregister). */
struct SubCtx {
    std::unique_ptr<pvxs::server::MonitorControlOp> ctrl;
    TableRecCtx                                    *owner;

    SubCtx() : owner(nullptr) {}
};

/* Per-record context, owned by TableSource and pointed to by prec->rpvt.
   tableRecordPvt MUST be the first member so the record can recover the full
   context from prec->rpvt. The NTTable prototype is built once and cloned for
   every update and GET. */
struct TableRecCtx {
    tableRecordPvt                     hdr;    /* offset 0: notify() */
    dbCommon                          *prec;
    pvxs::Value                        proto;  /* built once, cloned per update */
    /* Column accessors, resolved once at init (names/types and the field
       pointers are fixed for the life of the record). Used for both reading
       (snapshot) and writing (put). */
    std::vector<TableRecordWrapper::DataColumn> dcols;
    std::vector<TableRecordWrapper::OptColumn>  ocols;
    std::mutex                         mu;     /* guards subs */
    std::set<std::shared_ptr<SubCtx>>  subs;
    class TableSource                 *src;

    TableRecCtx() : prec(nullptr), src(nullptr) { hdr.notify = nullptr; }
};

/**
 * Custom pvxs Source that publishes table records as NTTable PVs.
 *
 * Registered at priority -1, it intercepts channels for table records before
 * the default qsrvSingle source (priority 0).
 *
 * Updates are driven synchronously from tableRecord's process() via the RPVT
 * hook (see onProcess), so the per-column CHGD flags are read in the same locked
 * cycle that set them — no asynchronous dbEvent, no race.
 */
class TableSource final : public pvxs::server::Source {
public:
    TableSource();
    ~TableSource();

    void onSearch(Search& op) override;
    void onCreate(std::unique_ptr<pvxs::server::ChannelControl>&& op) override;
    List onList() override;

    /* Installed into each record's rpvt->notify; called from process() under
       the record lock. Recovers its TableRecCtx from prec->rpvt. */
    static void onProcess(struct tableRecord* prec);

private:
    std::vector<std::unique_ptr<TableRecCtx>>    ctxs_;
    std::map<std::string, TableRecCtx*>          records_;
    std::shared_ptr<const std::set<std::string>> names_;

    pvxs::Value makeProto(
        const std::vector<TableRecordWrapper::DataColumn>& dcols,
        const std::vector<TableRecordWrapper::OptColumn>& ocols) const;
};

} /* namespace table */

#endif /* TABLE_SOURCE_H */
