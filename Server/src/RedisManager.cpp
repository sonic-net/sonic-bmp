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
RedisManager::RedisManager() : stateDb_(BMP_DB_NAME, 0, true) {
    swss::SonicDBConfig::initialize();
    swss::SonicDBConfig::initializeGlobalConfig();
    separator_ = swss::SonicDBConfig::getSeparator(BMP_DB_NAME);
    exit_ = false;
}

/*********************************************************************//**
 * Constructor for class
 ***********************************************************************/
RedisManager::~RedisManager() {
}


/*********************************************************************
 * Setup logger for this class
 *
 * \param [in] logPtr     logger pointer
 ***********************************************************************/
void RedisManager::Setup(Logger *logPtr) {
    logger = logPtr;
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

    swss::Table stateBMPTable(&stateDb_, table);
    std::string fullKey;
    for (const auto& key : keys) {
        fullKey += key;
        fullKey += separator_;
    }
    fullKey.erase(fullKey.size() - 1);

    LOG_INFO("RedisManager WriteBMPTable key = %s", fullKey.c_str());

    stateBMPTable.set(fullKey, fieldValues);
    return true;
}


/**
 * RemoveEntityFromBMPTable
 *
 * \param [in] keys             Reference to various keys
 */
bool RedisManager::RemoveEntityFromBMPTable(const std::vector<std::string>& keys) {

    stateDb_.del(keys);
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
    auto connector = swss::ConfigDBConnector_Native();
    connector.connect(false);
    auto items = connector.get_entry(BMP_CFG_DB_NAME, "table");
    for (const auto& item : items) {
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
    swss::Table stateBMPTable(&stateDb_, table);
    std::vector<std::string> keys;
    stateBMPTable.getKeys(keys);
    stateDb_.del(keys);

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