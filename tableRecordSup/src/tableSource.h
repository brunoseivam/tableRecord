#ifndef TABLE_SOURCE_H
#define TABLE_SOURCE_H

#include <map>
#include <memory>
#include <set>
#include <string>

#include <dbCommon.h>
#include <dbEvent.h>

#include <pvxs/source.h>
#include <pvxs/server.h>
#include <pvxs/nt.h>

namespace table {

struct RecInfo {
    const char *type;   /* "tableA" or "tableB" */
    dbCommon   *prec;
};

struct SubCtx {
    RecInfo                                   ri;
    pvxs::Value                               proto;
    std::unique_ptr<pvxs::server::MonitorControlOp> ctrl;
    dbEventSubscription                       evtSub;
    dbEventCtx                                evtCtx;

    SubCtx() : evtSub(nullptr), evtCtx(nullptr) {}
};

/**
 * Custom pvxs Source that publishes tableA and tableB records as NTTable PVs.
 *
 * Registered at priority 1, it intercepts channels for table records before
 * the default qsrvSingle source (priority 0).
 */
class TableSource final : public pvxs::server::Source {
public:
    TableSource();
    ~TableSource();

    void onSearch(Search& op) override;
    void onCreate(std::unique_ptr<pvxs::server::ChannelControl>&& op) override;
    List onList() override;

private:
    std::map<std::string, RecInfo>            records_;
    std::shared_ptr<const std::set<std::string>> names_;
    dbEventCtx                                eventCtx_;

    pvxs::Value makeProto(const RecInfo& ri) const;
};

} /* namespace table */

#endif /* TABLE_SOURCE_H */
