#ifndef MSGBUSIMPL_REDIS_H_
#define MSGBUSIMPL_REDIS_H_

#define HASH_SIZE 16

#include "MsgBusInterface.hpp"
#include "RedisManager.h"
#include "BMPListener.h"

#include "Logger.h"
#include <string>
#include <map>
#include <vector>
#include <ctime>



#include "Config.h"

/**
 * \class   MsgBusImpl_redis
 *
 * \brief   Kafka message bus implementation
  */
class MsgBusImpl_redis: public MsgBusInterface {
public:

    /******************************************************************//**
     * \brief This function will initialize and connect to Kafka.
     *
     * \details It is expected that this class will start off with a new connection.
     *
     *  \param [in] logPtr      Pointer to Logger instance
     *  \param [in] cfg         Pointer to the config instance
     ********************************************************************/
    MsgBusImpl_redis(Logger *logPtr, Config *cfg, BMPListener::ClientInfo *client);
    ~MsgBusImpl_redis();

    /*
     * abstract methods implemented
     * See MsgBusInterface.hpp for method details
     */
    void update_Collector(struct obj_collector &c_obj, collector_action_code action_code);
    void update_Router(struct obj_router &r_entry, router_action_code code);
    void update_Peer(obj_bgp_peer &peer, obj_peer_up_event *up, obj_peer_down_event *down, peer_action_code code);
    void update_baseAttribute(obj_bgp_peer &peer, obj_path_attr &attr, base_attr_action_code code);
    void update_unicastPrefix(obj_bgp_peer &peer, std::vector<obj_rib> &rib, obj_path_attr *attr, unicast_prefix_action_code code);
    void add_StatReport(obj_bgp_peer &peer, obj_stats_report &stats);

    void update_LsNode(obj_bgp_peer &peer, obj_path_attr &attr, std::list<MsgBusInterface::obj_ls_node> &nodes,
                     ls_action_code code);
    void update_LsLink(obj_bgp_peer &peer, obj_path_attr &attr, std::list<MsgBusInterface::obj_ls_link> &links,
                     ls_action_code code);
    void update_LsPrefix(obj_bgp_peer &peer, obj_path_attr &attr, std::list<MsgBusInterface::obj_ls_prefix> &prefixes,
                      ls_action_code code);
    
    void update_L3Vpn(obj_bgp_peer &peer, std::vector<obj_vpn> &vpn, obj_path_attr *attr, vpn_action_code code);

    void update_eVPN(obj_bgp_peer &peer, std::vector<obj_evpn> &vpn, obj_path_attr *attr, vpn_action_code code);

    void send_bmp_raw(u_char *r_hash, obj_bgp_peer &peer, u_char *data, size_t data_len);

private:
    Logger          *logger;                    ///< Logging class pointer
    Config          *cfg;                       ///< Pointer to config instance
    RedisManager    redisMgr_;
};

#endif /* MSGBUSIMPL_REDIS_H_ */
