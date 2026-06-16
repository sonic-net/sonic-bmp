#include <cstdlib>
#include <cstring>
#include <iostream>

#include <cinttypes>

#include <netdb.h>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <arpa/inet.h>

#include "MsgBusImpl_redis.h"
#include "RedisManager.h"

using namespace std;

/******************************************************************//**
 * \brief This function will initialize and connect to Kafka.
 *
 * \details It is expected that this class will start off with a new connection.
 *
 *  \param [in] logPtr      Pointer to Logger instance
 *  \param [in] cfg         Pointer to the config instance
 ********************************************************************/
MsgBusImpl_redis::MsgBusImpl_redis(Logger *logPtr, Config *cfg, BMPListener::ClientInfo *client) {
    logger = logPtr;
    this->cfg = cfg;
    redisMgr_.Setup(logPtr);
    redisMgr_.InitBMPConfig();
}

/**
 * Destructor
 */
MsgBusImpl_redis::~MsgBusImpl_redis() {
    redisMgr_.ExitRedisManager();
}

/**
 * Reset all Tables once FRR reconnects to BMP, this will not disable table population
 *
 * \param [in] N/A
 */
void MsgBusImpl_redis::ResetAllTables() {
    redisMgr_.ResetAllTables();
}

/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_Peer(obj_bgp_peer &peer, obj_peer_up_event *up, obj_peer_down_event *down, peer_action_code code) {

    // Below attributes will be populated if exists, and no matter bgp neighbor is up or down
    vector<swss::FieldValueTuple> fieldValues;
    fieldValues.reserve(MAX_ATTRIBUTES_COUNT);
    vector<string> keys;
    keys.emplace_back(peer.peer_addr);

    fieldValues.emplace_back(make_pair("peer_addr", peer.peer_addr));
    stringstream peer_as;
    peer_as << peer.peer_as;
    fieldValues.emplace_back(make_pair("peer_asn", peer_as.str()));
    fieldValues.emplace_back(make_pair("peer_rd", peer.peer_rd));
    if (up != NULL) {
        stringstream remote_port;
        remote_port << up->remote_port;
        fieldValues.emplace_back(make_pair("remote_port", remote_port.str()));
        stringstream local_asn;
        local_asn << up->local_asn;
        fieldValues.emplace_back(make_pair("local_asn", local_asn.str()));
        fieldValues.emplace_back(make_pair("local_ip", up->local_ip));
        stringstream local_port;
        local_port << up->local_port;
        fieldValues.emplace_back(make_pair("local_port", local_port.str()));
        fieldValues.emplace_back(make_pair("sent_cap", up->sent_cap));
        fieldValues.emplace_back(make_pair("recv_cap", up->recv_cap));
    }

    switch (code) {
        case PEER_ACTION_DOWN:
        {
            if (down != NULL) {
                // PEER DOWN only
                fieldValues.emplace_back(make_pair("bgp_err_code", to_string(down->bgp_err_code)));
                fieldValues.emplace_back(make_pair("bgp_err_subcode", to_string(down->bgp_err_subcode)));
                fieldValues.emplace_back(make_pair("error_text", down->error_text));
            }
        }
        break;
    }
    for (const auto& fieldValue : fieldValues) {
        const std::string& field = std::get<0>(fieldValue);
        const std::string& value = std::get<1>(fieldValue);
        DEBUG("MsgBusImpl_redis update_Peer field = %s, value = %s", field.c_str(), value.c_str());
    }

    redisMgr_.WriteBMPTable(BMP_TABLE_NEI, keys, fieldValues);
}


/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_unicastPrefix(obj_bgp_peer &peer,
                                            std::vector<obj_rib> &rib,
                                            obj_path_attr *attr,
                                            unicast_prefix_action_code code)
{
    // Correlate concurrent invocations in logs
    static std::atomic<uint64_t> call_seq{0};
    const uint64_t call_id = ++call_seq;

    // Only enforce attr for ADD; DEL should proceed without it.
    if (code == UNICAST_PREFIX_ACTION_ADD && attr == nullptr) {
        LOG_INFO("MsgBusImpl_redis[%llu] update_unicastPrefix: ADD requested but attr==NULL (peer=%s) — ignoring",
                 (unsigned long long)call_id, peer.peer_addr);
        return;
    }

    const char* actionStr = (code == UNICAST_PREFIX_ACTION_ADD) ? "add" : "del";

    // Timestamp + hashes (to match Kafka line contents)
    std::string ts, path_hash_str, peer_hash_str, router_hash_str;
    getTimestamp(peer.timestamp_secs, peer.timestamp_us, ts);
    if (attr)        hash_toStr(attr->hash_id,        path_hash_str);
    hash_toStr(peer.hash_id,        peer_hash_str);
    hash_toStr(peer.router_hash_id, router_hash_str);

    LOG_INFO("MsgBusImpl_redis[%llu] update_unicastPrefix[start] action=%s peer=%s as=%u ts=%s rib_size=%zu isAdjIn=%d isPrePolicy=%d",
             (unsigned long long)call_id, actionStr, peer.peer_addr, peer.peer_as, ts.c_str(), rib.size(),
             (int)peer.isAdjIn, (int)peer.isPrePolicy);

    std::vector<std::string> del_keys;
    del_keys.reserve(rib.size());

    size_t add_count = 0, del_count = 0;

    // Small helper to keep previews short in logs
    auto cut = [](const std::string& s)->std::string {
        return s.size() > 160 ? (s.substr(0,160) + "...") : s;
    };

    for (size_t i = 0; i < rib.size(); ++i) {
        // Compose "prefix/len"
        std::string pfx_len = std::string(rib[i].prefix) + "/" + std::to_string(rib[i].prefix_len);

        // Redis key parts (prefix/len, neighbor)
        std::vector<std::string> keys;
        keys.reserve(2);
        keys.emplace_back(pfx_len);
        keys.emplace_back(peer.peer_addr);

        // RIB hash (already computed by producer of obj_rib)
        std::string rib_hash_str; hash_toStr(rib[i].hash_id, rib_hash_str);

        LOG_INFO("MsgBusImpl_redis[%llu] RIB[%zu] prefix=%s/%d isIPv4=%d path_id=%u labels=%s rib_hash=%s",
                 (unsigned long long)call_id, i, rib[i].prefix, rib[i].prefix_len, (int)rib[i].isIPv4,
                 (unsigned)rib[i].path_id, rib[i].labels, rib_hash_str.c_str());

        if (code == UNICAST_PREFIX_ACTION_ADD) {
            // Convert all fields to std::string safely (works for char[] or std::string sources)
            const std::string origin          = attr->origin;
            const std::string as_path         = attr->as_path;
            const uint32_t    as_path_count   = attr->as_path_count;
            const uint32_t    origin_as       = attr->origin_as;
            const std::string next_hop        = attr->next_hop;
            const uint32_t    med             = attr->med;
            const uint32_t    local_pref      = attr->local_pref;
            const std::string aggregator      = attr->aggregator;
            const std::string community_list  = attr->community_list;
            const std::string ext_comm_list   = attr->ext_community_list;
            const std::string cluster_list    = attr->cluster_list;
            const int         atomic_agg      = (int)attr->atomic_agg;
            const int         nexthop_isIPv4  = (int)attr->nexthop_isIPv4;
            const std::string originator_id   = attr->originator_id;
            const uint32_t    path_id         = rib[i].path_id;
            const std::string labels          = rib[i].labels;
            const int         isPrePolicy     = (int)peer.isPrePolicy;
            const int         isAdjIn         = (int)peer.isAdjIn;
            const std::string large_comm_list = attr->large_community_list;

            // Kafka-parity RAW_LINE (router_ip removed; peer_addr included after peer_hash)
            char raw[4096];
            int n = std::snprintf(
                raw, sizeof(raw),
                // action, seq, rib_hash, router_hash, path_hash, peer_hash, peer_addr, peer_as, ts,
                // prefix, prefix_len, isIPv4, origin, as_path, as_path_count, origin_as, next_hop,
                // med, local_pref, aggregator, community_list, ext_comm_list, cluster_list,
                // atomic_agg, nexthop_isIPv4, originator_id, path_id, labels, isPrePolicy, isAdjIn, large_comm_list
                "%s\t%llu\t%s\t%s\t%s\t%s\t%s\t%u\t%s\t%s\t%d\t%d\t%s\t%s\t%u\t%u\t%s\t%u\t%u\t%s\t%s\t%s\t%s\t%d\t%d\t%s\t%u\t%s\t%d\t%d\t%s",
                actionStr,
                (unsigned long long)0,           /* seq if you track one */
                rib_hash_str.c_str(),
                router_hash_str.c_str(),
                path_hash_str.c_str(),
                peer_hash_str.c_str(),
                peer.peer_addr,
                (unsigned)peer.peer_as,
                ts.c_str(),
                rib[i].prefix,
                rib[i].prefix_len,
                (int)rib[i].isIPv4,
                origin.c_str(),
                as_path.c_str(),
                (unsigned)as_path_count,
                (unsigned)origin_as,
                next_hop.c_str(),
                (unsigned)med,
                (unsigned)local_pref,
                aggregator.c_str(),
                community_list.c_str(),
                ext_comm_list.c_str(),
                cluster_list.c_str(),
                atomic_agg,
                nexthop_isIPv4,
                originator_id.c_str(),
                (unsigned)path_id,
                labels.c_str(),
                isPrePolicy,
                isAdjIn,
                large_comm_list.c_str());

            if (n < 0) {
                LOG_INFO("MsgBusImpl_redis[%llu] RAW_LINE format error", (unsigned long long)call_id);
            } else if ((size_t)n >= sizeof(raw)) {
                LOG_INFO("MsgBusImpl_redis[%llu] RAW_LINE truncated (len=%d cap=%zu)",
                         (unsigned long long)call_id, n, sizeof(raw));
            }
            LOG_INFO("MsgBusImpl_redis[%llu] RAW_LINE[%zu]=%s", (unsigned long long)call_id, i, raw);

            // Compact preview (resilient to syslog truncation)
            LOG_INFO("MsgBusImpl_redis[%llu] ADD preview origin='%s' as_path.len=%zu next_hop='%s' comm.len=%zu ext_comm.len=%zu cluster.len=%zu "
                     "labels.len=%zu large_comm.len=%zu",
                     (unsigned long long)call_id,
                     cut(origin).c_str(), as_path.size(), cut(next_hop).c_str(),
                     community_list.size(), ext_comm_list.size(), cluster_list.size(),
                     labels.size(), large_comm_list.size());

            // Prepare Redis fields (parity with logged values)
            std::vector<swss::FieldValueTuple> fvs;
            fvs.reserve(32);
            fvs.emplace_back(std::make_pair("origin",               origin));
            fvs.emplace_back(std::make_pair("as_path",              as_path));
            fvs.emplace_back(std::make_pair("as_path_count",        std::to_string(as_path_count)));
            fvs.emplace_back(std::make_pair("origin_as",            std::to_string(origin_as)));
            fvs.emplace_back(std::make_pair("next_hop",             next_hop));
            fvs.emplace_back(std::make_pair("med",                  std::to_string(med)));
            fvs.emplace_back(std::make_pair("local_pref",           std::to_string(local_pref)));
            fvs.emplace_back(std::make_pair("aggregator",           aggregator));
            fvs.emplace_back(std::make_pair("community_list",       community_list));
            fvs.emplace_back(std::make_pair("ext_community_list",   ext_comm_list));
            fvs.emplace_back(std::make_pair("cluster_list",         cluster_list));
            fvs.emplace_back(std::make_pair("atomic_agg",           std::to_string(atomic_agg)));
            fvs.emplace_back(std::make_pair("nexthop_isIPv4",       std::to_string(nexthop_isIPv4)));
            fvs.emplace_back(std::make_pair("originator_id",        originator_id));
            fvs.emplace_back(std::make_pair("large_community_list", large_comm_list));
            fvs.emplace_back(std::make_pair("peer_addr",            peer.peer_addr));

            // RIB/peer context + hashes + timestamp
            fvs.emplace_back(std::make_pair("path_id",              std::to_string(path_id)));
            fvs.emplace_back(std::make_pair("labels",               labels));
            fvs.emplace_back(std::make_pair("isIPv4",               std::to_string((int)rib[i].isIPv4)));
            fvs.emplace_back(std::make_pair("peer_as",              std::to_string(peer.peer_as)));
            fvs.emplace_back(std::make_pair("isPrePolicy",          std::to_string(isPrePolicy)));
            fvs.emplace_back(std::make_pair("isAdjIn",              std::to_string(isAdjIn)));
            fvs.emplace_back(std::make_pair("rib_hash",             rib_hash_str));
            fvs.emplace_back(std::make_pair("path_hash",            path_hash_str));
            fvs.emplace_back(std::make_pair("peer_hash",            peer_hash_str));
            fvs.emplace_back(std::make_pair("router_hash",          router_hash_str));
            fvs.emplace_back(std::make_pair("ts",                   ts));

            const char* table = peer.isAdjIn ? BMP_TABLE_RIB_IN : BMP_TABLE_RIB_OUT;
            LOG_INFO("MsgBusImpl_redis[%llu] ADD -> table=%s key=%s|%s fields_count=%zu",
                     (unsigned long long)call_id, table, pfx_len.c_str(), peer.peer_addr, fvs.size());
            LOG_INFO("MsgBusImpl_redis[%llu] ADD key_prefix=%s neighbor=%s rib_hash=%s path_hash=%s peer_hash=%s router_hash=%s",
                     (unsigned long long)call_id, pfx_len.c_str(), peer.peer_addr,
                     rib_hash_str.c_str(), path_hash_str.c_str(), peer_hash_str.c_str(), router_hash_str.c_str());

            if (peer.isAdjIn)
                redisMgr_.WriteBMPTable(BMP_TABLE_RIB_IN,  keys, fvs);
            else
                redisMgr_.WriteBMPTable(BMP_TABLE_RIB_OUT, keys, fvs);

            ++add_count;
        } else {
            std::string com_key = peer.isAdjIn ? BMP_TABLE_RIB_IN : BMP_TABLE_RIB_OUT;
            com_key += redisMgr_.GetKeySeparator() + pfx_len;
            com_key += redisMgr_.GetKeySeparator() + peer.peer_addr;
            LOG_INFO("MsgBusImpl_redis[%llu] DEL -> key=%s", (unsigned long long)call_id, com_key.c_str());
            del_keys.push_back(com_key);
            ++del_count;
        }
    }

    if (!del_keys.empty()) {
        LOG_INFO("MsgBusImpl_redis[%llu] RemoveEntityFromBMPTable count=%zu",
                 (unsigned long long)call_id, del_keys.size());
        redisMgr_.RemoveEntityFromBMPTable(del_keys);
    }

    LOG_INFO("MsgBusImpl_redis[%llu] update_unicastPrefix[done] action=%s adds=%zu dels=%zu",
             (unsigned long long)call_id, actionStr, add_count, del_count);
}





/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_Collector(obj_collector &c_object, collector_action_code action_code) {
}

/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_Router(obj_router &r_object, router_action_code code) {
}


/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_baseAttribute(obj_bgp_peer &peer, obj_path_attr &attr, base_attr_action_code code) {

}

/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_L3Vpn(obj_bgp_peer &peer, vector<obj_vpn> &vpn,
                                obj_path_attr *attr, vpn_action_code code) {

}


/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_eVPN(obj_bgp_peer &peer, vector<obj_evpn> &vpn,
                              obj_path_attr *attr, vpn_action_code code) {
}


/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::add_StatReport(obj_bgp_peer &peer, obj_stats_report &stats) {
}

/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_LsNode(obj_bgp_peer &peer, obj_path_attr &attr, std::list<MsgBusInterface::obj_ls_node> &nodes,
                                  ls_action_code code) {
}

/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_LsLink(obj_bgp_peer &peer, obj_path_attr &attr, std::list<MsgBusInterface::obj_ls_link> &links,
                                 ls_action_code code) {
}

/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_LsPrefix(obj_bgp_peer &peer, obj_path_attr &attr, std::list<MsgBusInterface::obj_ls_prefix> &prefixes,
                                   ls_action_code code) {
}

/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 *
 * TODO: Consolidate this to single produce method
 */
void MsgBusImpl_redis::send_bmp_raw(u_char *r_hash, obj_bgp_peer &peer, u_char *data, size_t data_len) {
}