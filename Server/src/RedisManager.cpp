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
    if (!exit_) {
        exit_ = true;
        for (auto& threadPtr : threadList_) {
            threadPtr->join();
        }
    }
}


/*********************************************************************
 * Setup logger for this class
 *
 * \param [in] logPtr     logger pointer
 ***********************************************************************/
void RedisManager::Setup(Logger *logPtr, BMPListener::ClientInfo *client) {
    logger = logPtr;
    client_ = client;
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
 * RemoveBMPTable
 *
 * \param [in] keys             Reference to various keys
 */
bool RedisManager::RemoveBMPTable(const std::vector<std::string>& keys) {

    stateDb_.del(keys);
    return true;
}

/**
 * DisconnectBMP
 *
 * \param [in] N/A
 */
void RedisManager::DisconnectBMP() {
    LOG_INFO("RedisManager DisconnectBMP");
    close(client_->c_sock);
    client_->c_sock = 0;
}

/**
 * ExitRedisManager
 *
 * \param [in] N/A
 */
void RedisManager::ExitRedisManager() {
    exit_ = true;
    for (auto& threadPtr : threadList_) {
        threadPtr->join();
    }
}

/**
 * ReadBMPTable, there will be dedicated thread be launched inside and monitor corresponding redis table.
 *
 * \param [in] tables             table names to be subscribed.
 */
void RedisManager::SubscriberWorker(const std::string& table) {
    try {
        swss::DBConnector cfgDb("CONFIG_DB", 0, true);

        swss::SubscriberStateTable conf_table(&cfgDb, table);
        swss::Select s;
        s.addSelectable(&conf_table);

        while (!exit_) {
            swss::Selectable *sel;
            int ret;

            ret = s.select(&sel, BMP_CFG_TABLE_SELECT_TIMEOUT);
            if (ret == swss::Select::ERROR) {
                SWSS_LOG_NOTICE("Error: %s!", strerror(errno));
                continue;
            }
            if (ret == swss::Select::TIMEOUT) {
                continue;
            }

            swss::KeyOpFieldsValuesTuple kco;
            conf_table.pop(kco);

            if (std::get<0>(kco) == "SET") {
                if (std::get<1>(kco) == "true") {
                    EnableTable(table);
                }
                else {
                    DisableTable(table);
                    DisconnectBMP();
                }
            }
            else if (std::get<0>(kco) == "DEL")
            {
                LOG_ERR("Config should not be deleted");
            }
        }
    }
    catch (const exception &e) {
        LOG_ERR("Runtime error: %s", e.what());
    }
}

  

/**
 * ReadBMPTable, there will be dedicated thread be launched inside and monitor corresponding redis table.
 *
 * \param [in] tables             table names to be subscribed.
 */
bool RedisManager::ReadBMPTable(const std::vector<std::string>& tables) {
    for (const auto& table : tables) {
        std::shared_ptr<std::thread> threadPtr = std::make_shared<std::thread>(
            std::bind(&RedisManager::SubscriberWorker, this, table));
        threadList_.push_back(threadPtr);
    }
    return true;
}


/**
 * Enable specific Table
 *
 * \param [in] table    Reference to table name, like BGP_NEIGHBOR_TABLE/BGP_RIB_OUT_TABLE/BGP_RIB_IN_TABLE
 */
bool RedisManager::EnableTable(const std::string & table) {
    enabledTables_.insert(table);
    return true;
}

/**
 * Enable BGP_Neighbor* Table
 *
 * \param [in] table    Reference to table name BGP_NEIGHBOR_TABLE/BGP_RIB_OUT_TABLE/BGP_RIB_IN_TABLE
 */
bool RedisManager::DisableTable(const std::string & table) {
    enabledTables_.erase(table);
    return ResetBMPTable(table);
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