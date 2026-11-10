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
 ***********************************************************************/
void RedisManager::Setup(Logger *logPtr) {
    logger = logPtr;
    if (!swss::SonicDBConfig::isInit()) {
        swss::SonicDBConfig::initialize();
    }

    stateDb_ =  std::make_shared<swss::DBConnector>(BMP_DB_NAME, 0, false);
    separator_ = swss::SonicDBConfig::getSeparator(BMP_DB_NAME);
}


/*********************************************************************//**
 * Return the current BMP_STATE_DB connection handle (thread-safe)
 ***********************************************************************/
std::shared_ptr<swss::DBConnector> RedisManager::GetStateDb() {
    std::lock_guard<std::mutex> lock(stateDbMutex_);
    return stateDb_;
}


/*********************************************************************//**
 * Log a redis failure and re-establish stateDb_ when it is still the failed
 * connection, so subsequent operations recover instead of failing permanently.
 ***********************************************************************/
void RedisManager::HandleRedisError(const std::shared_ptr<swss::DBConnector>& usedDb,
                                    const char* op, const std::exception& e) {
    LOG_ERR("RedisManager %s failed on %s: %s; reconnecting", op, BMP_DB_NAME, e.what());

    std::lock_guard<std::mutex> lock(stateDbMutex_);
    // Only the first thread to observe the broken connection reconnects; other
    // threads holding the same handle pick up the new one on their next call.
    if (stateDb_ == usedDb) {
        try {
            stateDb_ = std::make_shared<swss::DBConnector>(BMP_DB_NAME, 0, false);
        } catch (const std::exception& reconnectErr) {
            LOG_ERR("RedisManager failed to reconnect to %s: %s", BMP_DB_NAME, reconnectErr.what());
        }
    }
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
        DEBUG("RedisManager %s is disabled", table.c_str());
        return false;
    }
    std::ostringstream oss;
    for (const auto& key : keys) {
        oss << key << separator_;
    }
    std::string fullKey = oss.str();
    fullKey.pop_back();

    DEBUG("RedisManager WriteBMPTable key = %s", fullKey.c_str());

    std::shared_ptr<swss::DBConnector> db = GetStateDb();
    try {
        std::unique_ptr<swss::Table> stateBMPTable = std::make_unique<swss::Table>(db.get(), table);
        stateBMPTable->set(fullKey, fieldValues);
    } catch (const std::exception& e) {
        HandleRedisError(db, "WriteBMPTable", e);
        return false;
    }
    return true;
}


/**
 * RemoveEntityFromBMPTable
 *
 * \param [in] keys             Reference to various keys
 */
bool RedisManager::RemoveEntityFromBMPTable(const std::vector<std::string>& keys) {

    for (const auto& key : keys) {
        DEBUG("RedisManager RemoveEntityFromBMPTable key = %s", key.c_str());
    }
    std::shared_ptr<swss::DBConnector> db = GetStateDb();
    try {
        db->del(keys);
    } catch (const std::exception& e) {
        HandleRedisError(db, "RemoveEntityFromBMPTable", e);
        return false;
    }
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
    std::unique_ptr<swss::Table> cfgTable = std::make_unique<swss::Table>(cfgDb.get(), BMP_CFG_TABLE_NAME);

    std::vector<swss::FieldValueTuple> fvt;
    cfgTable->get(BMP_CFG_TABLE_KEY, fvt);
    for (const auto& item : fvt) {
        const std::string& field = std::get<0>(item);
        const std::string& value = std::get<1>(item);

        if (field == BMP_CFG_TABLE_NEI && value == "true") {
            enabledTables_.insert(BMP_TABLE_NEI);
        }
        if (field == BMP_CFG_TABLE_RIB_IN && value == "true") {
            enabledTables_.insert(BMP_TABLE_RIB_IN);
        }
        if (field == BMP_CFG_TABLE_RIB_OUT && value == "true") {
            enabledTables_.insert(BMP_TABLE_RIB_OUT);
        }
    }
    return true;
}


/**
 * Reset ResetBMPTable, this will flush redis
 *
 * \param [in] table    Reference to table name BGP_NEIGHBOR_TABLE/BGP_RIB_OUT_TABLE/BGP_RIB_IN_TABLE
 */
void RedisManager::ResetBMPTable(const std::string & table) {

    std::shared_ptr<swss::DBConnector> db = GetStateDb();
    try {
        std::unique_ptr<swss::Table> stateBMPTable = std::make_unique<swss::Table>(db.get(), table);
        std::vector<std::string> keys;
        stateBMPTable->getKeys(keys);
        LOG_INFO("RedisManager ResetBMPTable data size %d", keys.size());

        db->del(keys);
    } catch (const std::exception& e) {
        HandleRedisError(db, "ResetBMPTable", e);
    }
}



/**
 * Reset all Tables once FRR reconnects to BMP, this will not disable table population
 *
 * \param [in] N/A
 */
void RedisManager::ResetAllTables() {
    for (const auto& enabledTable : enabledTables_) {
        ResetBMPTable(enabledTable);
    }
}
