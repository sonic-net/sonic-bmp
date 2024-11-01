/*
 * Copyright (c) 2024 Microsoft, Inc. and others.  All rights reserved.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License v1.0 which accompanies this distribution,
 * and is available at http://www.eclipse.org/legal/epl-v10.html
 *
 */

#include "RedisManager.h"


/*********************************************************************//**
 * Constructor for class
 ***********************************************************************/
RedisManager::RedisManager() {
    exit_ = false;
}

/*********************************************************************//**
 * Constructor for class
 ***********************************************************************/
RedisManager::~RedisManager() {
}


/*********************************************************************
 * Setup for this class
 *
 * \param [in] logPtr     logger pointer
 * \param [in] cfgPtr     config pointer
 ***********************************************************************/
void RedisManager::Setup(Logger *logPtr, Config *cfgPtr) {
    logger = logPtr;
    if (!cfgPtr->redis_multiAsic) {
        if (!swss::SonicDBConfig::isInit()) {
            swss::SonicDBConfig::initialize();
        }
    } else {
        if (!swss::SonicDBConfig::isGlobalInit()) {
            swss::SonicDBConfig::initializeGlobalConfig();
        }
    }
    stateDb_ =  std::make_shared<swss::DBConnector>(BMP_DB_NAME, 0, false);
    separator_ = swss::SonicDBConfig::getSeparator(BMP_DB_NAME);
}



/**
 * Get Key separator for deletion
 *
 * \param [in] N/A
 */
std::string RedisManager::GetKeySeparator() {
    return separator_;
}


/**
 * WriteBMPTable
 *
 * \param [in] table            Reference to table name
 * \param [in] key              Reference to various keys list
 * \param [in] fieldValues      Reference to field-value pairs
 */
bool RedisManager::WriteBMPTable(const std::string& table, const std::vector<std::string>& keys, const std::vector<swss::FieldValueTuple> fieldValues) {

    if (enabledTables_.find(table) == enabledTables_.end()) {
        LOG_INFO("RedisManager %s is disabled", table.c_str());
        return false;
    }
    std::unique_ptr<swss::Table> stateBMPTable = std::unique_ptr<Table>(new Table(stateDb_.get(), table));
    std::string fullKey;
    for (const auto& key : keys) {
        fullKey += key;
        fullKey += separator_;
    }
    fullKey.erase(fullKey.size() - 1);

    SELF_DEBUG("RedisManager WriteBMPTable key = %s", fullKey.c_str());

    stateBMPTable->set(fullKey, fieldValues);
    return true;
}


/**
 * RemoveEntityFromBMPTable
 *
 * \param [in] keys             Reference to various keys
 */
bool RedisManager::RemoveEntityFromBMPTable(const std::vector<std::string>& keys) {

    stateDb_->del(keys);
    return true;
}


/**
 * ExitRedisManager
 *
 * \param [in] N/A
 */
void RedisManager::ExitRedisManager() {
    exit_ = true;
}


/**
 * InitBMPConfig, read config_db for table enablement setting.
 *
 * \param [in] N/A
 */
bool RedisManager::InitBMPConfig() {
    std::shared_ptr<swss::DBConnector> cfgDb =
        std::make_shared<swss::DBConnector>("CONFIG_DB", 0, false);
    std::unique_ptr<swss::Table> cfgTable =
        std::unique_ptr<swss::Table>(new swss::Table(cfgDb.get(), BMP_CFG_TABLE_NAME));
    std::vector<FieldValueTuple> fvt;
    cfgTable.get(BMP_CFG_TABLE_KEY, fvt);
    for (const auto& item : fvt) {
        if (item.second == "true") {
            enabledTables_.insert(item.first);
        }
    }
    return true;
}


/**
 * Reset ResetBMPTable, this will flush redis
 *
 * \param [in] table    Reference to table name BGP_NEIGHBOR_TABLE/BGP_RIB_OUT_TABLE/BGP_RIB_IN_TABLE
 */
bool RedisManager::ResetBMPTable(const std::string & table) {

    LOG_INFO("RedisManager ResetBMPTable %s", table.c_str());
    std::unique_ptr<swss::Table> stateBMPTable = std::unique_ptr<Table>(new Table(stateDb_.get(), table));
    std::vector<std::string> keys;
    stateBMPTable->getKeys(keys);
    stateDb_->del(keys);

    return true;
}



/**
 * Reset all Tables once FRR reconnects to BMP, this will not disable table population
 *
 * \param [in] N/A
 */
void RedisManager::ResetAllTables() {
    LOG_INFO("RedisManager ResetAllTables");

    for (const auto& enabledTable : enabledTables_) {
        ResetBMPTable(enabledTable);
    }
}