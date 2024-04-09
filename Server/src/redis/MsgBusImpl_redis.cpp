#include <cstdlib>
#include <cstring>
#include <iostream>

#include <cinttypes>

#include <netdb.h>
#include <unistd.h>

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
    redisMgr_.Setup(logPtr, client);
    std::vector<std::string> tables;
    tables.emplace_back(BMP_CFG_TABLE_NEI);
    tables.emplace_back(BMP_CFG_TABLE_RIB_IN);
    tables.emplace_back(BMP_CFG_TABLE_RIB_OUT);

    redisMgr_.ReadBMPTable(tables);
}

/**
 * Destructor
 */
MsgBusImpl_redis::~MsgBusImpl_redis() {
    redisMgr_.ExitRedisManager();
}


/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_Peer(obj_bgp_peer &peer, obj_peer_up_event *up, obj_peer_down_event *down, peer_action_code code) {

    // Below attributes will be populated if exists, and no matter bgp neighbor is up or down
    std::vector<swss::FieldValueTuple> fieldValues;
    fieldValues.reserve(30);
    std::vector<std::string> keys;
    keys.emplace_back(peer.peer_addr);

    fieldValues.emplace_back(std::make_pair("peer_addr", peer.peer_addr));
    fieldValues.emplace_back(std::make_pair("peer_asn", std::to_string(peer.peer_as)));
    fieldValues.emplace_back(std::make_pair("peer_rd", peer.peer_rd));
    fieldValues.emplace_back(std::make_pair("remote_port", std::to_string(up->remote_port)));
    fieldValues.emplace_back(std::make_pair("local_asn", std::to_string(up->local_asn)));
    fieldValues.emplace_back(std::make_pair("local_ip", up->local_ip));
    fieldValues.emplace_back(std::make_pair("local_port", std::to_string(up->local_port)));
    fieldValues.emplace_back(std::make_pair("sent_cap", up->sent_cap));
    fieldValues.emplace_back(std::make_pair("recv_cap", up->recv_cap));

    switch (code) {
        case PEER_ACTION_UP :
        {
        }
        break;

        case PEER_ACTION_DOWN:
        {
            // PEER DOWN only
            fieldValues.emplace_back(std::make_pair("bgp_err_code", std::to_string(down->bgp_err_code)));
            fieldValues.emplace_back(std::make_pair("bgp_err_subcode", std::to_string(down->bgp_err_subcode)));
            fieldValues.emplace_back(std::make_pair("error_text", down->error_text));

        }
        break;
    }
    redisMgr_.WriteBMPTable(BMP_TABLE_NEI, keys, fieldValues);
}


/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_unicastPrefix(obj_bgp_peer &peer, std::vector<obj_rib> &rib,
                                        obj_path_attr *attr, unicast_prefix_action_code code) {
    if (attr == NULL)
        return;

    // Loop through the vector array of rib entries
    std::vector<swss::FieldValueTuple> addFieldValues;
    addFieldValues.reserve(30);
    std::vector<std::string> del_keys;
    string neigh = peer.peer_addr;

    for (size_t i = 0; i < rib.size(); i++) {
        std::vector<std::string> keys;
        std::string redisMgr_pfx = rib[i].prefix;
        redisMgr_pfx += "/";
        redisMgr_pfx += std::to_string(rib[i].prefix_len);
        keys.reserve(10);
        keys.emplace_back(redisMgr_pfx);

        switch (code) {

            case UNICAST_PREFIX_ACTION_ADD:
            {
                addFieldValues.emplace_back(std::make_pair("origin", attr->origin));
                addFieldValues.emplace_back(std::make_pair("as_path", attr->as_path));
                addFieldValues.emplace_back(std::make_pair("as_path_count", std::to_string(attr->as_path_count)));
                addFieldValues.emplace_back(std::make_pair("origin_as", std::to_string(attr->origin_as)));
                addFieldValues.emplace_back(std::make_pair("next_hop", attr->next_hop));
                addFieldValues.emplace_back(std::make_pair("local_pref", std::to_string(attr->local_pref)));
                addFieldValues.emplace_back(std::make_pair("community_list", attr->community_list));
                addFieldValues.emplace_back(std::make_pair("ext_community_list", attr->ext_community_list));
                addFieldValues.emplace_back(std::make_pair("large_community_list", attr->large_community_list));
                addFieldValues.emplace_back(std::make_pair("originator_id", attr->originator_id));

                keys.emplace_back(BMP_TABLE_NEI_PREFIX);
                keys.emplace_back(peer.peer_addr);

                if(peer.isAdjIn)
                {
                    redisMgr_.WriteBMPTable(BMP_TABLE_RIB_IN, keys, addFieldValues);
                }
                else
                {
                    redisMgr_.WriteBMPTable(BMP_TABLE_RIB_OUT, keys, addFieldValues);
                }
            }
                break;

            case UNICAST_PREFIX_ACTION_DEL:
            {
                std::string com_key;
                if(peer.isAdjIn)
                {
                    com_key = BMP_TABLE_RIB_IN;
                }
                else
                {
                    com_key = BMP_TABLE_RIB_OUT;
                }
                com_key += redisMgr_.GetKeySeparator();
                com_key += redisMgr_pfx;
                com_key += redisMgr_.GetKeySeparator();
                com_key += BMP_TABLE_NEI_PREFIX;
                com_key += neigh;
                del_keys.push_back(com_key);
            }
                break;
        }
    }

    if (!del_keys.empty()) {
        redisMgr_.RemoveBMPTable(del_keys);
    }
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
    if (code == ROUTER_ACTION_INIT) {
        redisMgr_.ResetAllTables();
    }
}


/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_baseAttribute(obj_bgp_peer &peer, obj_path_attr &attr, base_attr_action_code code) {

}

/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_L3Vpn(obj_bgp_peer &peer, std::vector<obj_vpn> &vpn,
                                obj_path_attr *attr, vpn_action_code code) {

}


/**
 * Abstract method Implementation - See MsgBusInterface.hpp for details
 */
void MsgBusImpl_redis::update_eVPN(obj_bgp_peer &peer, std::vector<obj_evpn> &vpn,
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