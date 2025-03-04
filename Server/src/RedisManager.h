/*
 * Copyright (c) 2024 Microsoft, Inc. and others.  All rights reserved.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License v1.0 which accompanies this distribution,
 * and is available at http://www.eclipse.org/legal/epl-v10.html
 *
 */

#ifndef REDISMANAGER_H_
#define REDISMANAGER_H_

#include <swss/dbconnector.h>
#include <swss/table.h>
#include <swss/configdb.h>

#include <string>
#include <list>
#include <map>
#include <mutex>
#include <unordered_set>
#include <functional>
#include <vector>
#include <sstream>
#include "Logger.h"
#include "Config.h"


/**
 * BMP_TABLE_* defines the default table name prefix
 */
#define BMP_DB_NAME                "BMP_STATE_DB"
#define BMP_TABLE_NEI              "BGP_NEIGHBOR_TABLE"
#define BMP_TABLE_RIB_IN           "BGP_RIB_IN_TABLE"
#define BMP_TABLE_RIB_OUT          "BGP_RIB_OUT_TABLE"
#define BMP_TABLE_NEI_PREFIX       "BGP_NEIGHBOR"


/**
 * BMP_CFG_TABLE_* defines config db tables.
 */
#define BMP_CFG_TABLE_NAME         "BMP"
#define BMP_CFG_TABLE_KEY          "table"
#define BMP_CFG_TABLE_NEI          "bgp_neighbor_table"
#define BMP_CFG_TABLE_RIB_IN       "bgp_rib_in_table"
#define BMP_CFG_TABLE_RIB_OUT      "bgp_rib_out_table"

/**
 * \class   RedisManager
 *
 * \brief   RedisManager class for openbmpd
 * \details
 *      Encapsulate redis operation in this class instance.
 */
class RedisManager {

public:
    /***********************************************************************
     * Constructor for class
     ***********************************************************************/
    RedisManager();

    /*********************************************************************//**
     * Destructor for class
     ***********************************************************************/
    ~RedisManager();

    /***********************************************************************
     * Setup logger for this class
     *
     * \param [in] logPtr     logger pointer
     */
    void Setup(Logger *logPtr);


    /**
    * ExitRedisManager
    *
    * \param [in] N/A
    */
    void ExitRedisManager();

    /**
     * Reset all Tables once FRR reconnects to BMP, this will not disable table population
     *
     * \param [in] N/A
     */
    void ResetAllTables();

    /**
     * Reset ResetBMPTable, this will flush redis
     *
     * \param [in] table    Reference to table name BGP_NEIGHBOR_TABLE/BGP_RIB_OUT_TABLE/BGP_RIB_IN_TABLE
     */
    void ResetBMPTable(const std::string & table);

    /**
     * Reset ResetBMPTable, this will flush redis
     *
     * \param [in] table            Reference to table name BGP_NEIGHBOR_TABLE/BGP_RIB_OUT_TABLE/BGP_RIB_IN_TABLE
     * \param [in] peer_addr        Reference to peer address
     */
    void ResetBMPTableByPeer(const std::string & table, const std::string& peer_addr);

    /**
     * WriteBMPTable
     *
     * \param [in] table            Reference to table name
     * \param [in] key              Reference to various keys list
     * \param [in] fieldValues      Reference to field-value pairs
     */
    bool WriteBMPTable(const std::string& table, const std::vector<std::string>& keys, const std::vector<swss::FieldValueTuple> fieldValues);

    /**
     * InitBMPConfig, read config_db for table enablement setting.
     *
     * \param [in] N/A
     */
    bool InitBMPConfig();

    /**
     * RemoveEntityFromBMPTable
     *
     * \param [in] table            Reference to table name
     * \param [in] args             Reference to various keys
     */
    bool RemoveEntityFromBMPTable(const std::vector<std::string>& keys);

    /**
     * RemoveBGPPeerFromBMPTable
     *
     * \param [in] peer_addr        Reference to peer address
     */
    bool RemoveBGPPeerFromBMPTable(const std::string& peer_addr);

    /**
     * Get Key separator for deletion
     *
     * \param [in] N/A
     */
    std::string GetKeySeparator();

private:
    std::shared_ptr<swss::DBConnector> stateDb_;
    std::string separator_;
    Logger *logger;
    std::unordered_set<std::string> enabledTables_;
    bool exit_;
};


#endif /* RedisManager_H_ */
