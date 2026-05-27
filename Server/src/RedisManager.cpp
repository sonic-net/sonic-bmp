/*
 * Copyright (c) 2024 Microsoft, Inc. and others.  All rights reserved.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License v1.0 which accompanies this distribution,
 * and is available at http://www.eclipse.org/legal/epl-v10.html
 *
 */

#include "RedisManager.h"

/**
 * Default size of the shared RedisPipeline buffer used for BMP state
 * writes. Set high enough that a typical BMP UPDATE worth of route
 * entries fits in a single round-trip, but bounded so that the pipeline
 * auto-flushes if a producer goes quiet without the message-level
 * flush hook ever firing.
 */
static constexpr size_t BMP_PIPELINE_SIZE = 1024;


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

    // Build a single shared pipeline backed by the same logical DB. All
    // per-table buffered Table writers below share this pipeline so that a
    // flush() sends every queued HSET in a single batch instead of one
    // round-trip per route entry.
    pipeline_ = std::make_shared<swss::RedisPipeline>(stateDb_.get(), BMP_PIPELINE_SIZE);
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
 * Lookup-or-create a buffered Table writer for the given table name.
 *
 * The Table is constructed with buffered=true and bound to pipeline_, so
 * each set() call enqueues the underlying HSET on the pipeline rather
 * than synchronously talking to redis. The actual round-trip happens on
 * FlushBMPTables() (or when the pipeline buffer fills).
 *
 * Returns nullptr if the table is not enabled via CONFIG_DB.
 */
swss::Table* RedisManager::GetOrCreateBufferedTable(const std::string& table) {
    if (enabledTables_.find(table) == enabledTables_.end()) {
        return nullptr;
    }
    auto it = bufferedTables_.find(table);
    if (it == bufferedTables_.end()) {
        auto t = std::make_unique<swss::Table>(pipeline_.get(), table, /*buffered=*/true);
        auto raw = t.get();
        bufferedTables_.emplace(table, std::move(t));
        return raw;
    }
    return it->second.get();
}


/**
 * WriteBMPTable
 *
 * \param [in] table            Reference to table name
 * \param [in] key              Reference to various keys list
 * \param [in] fieldValues      Reference to field-value pairs
 */
bool RedisManager::WriteBMPTable(const std::string& table, const std::vector<std::string>& keys, const std::vector<swss::FieldValueTuple> fieldValues) {

    swss::Table* stateBMPTable = GetOrCreateBufferedTable(table);
    if (stateBMPTable == nullptr) {
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

    stateBMPTable->set(fullKey, fieldValues);
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
    // Flush any buffered SETs first so that a later DEL on the same key
    // cannot be reordered before its preceding SET.
    FlushBMPTables();
    stateDb_->del(keys);
    return true;
}


/**
 * FlushBMPTables - flush all buffered table writers' pending operations.
 */
void RedisManager::FlushBMPTables() {
    for (auto& kv : bufferedTables_) {
        if (kv.second) {
            kv.second->flush();
        }
    }
}


/**
 * ExitRedisManager
 *
 * \param [in] N/A
 */
void RedisManager::ExitRedisManager() {
    // Best-effort: drain anything still buffered so we don't lose state
    // updates that have already been observed by openbmpd. This runs from
    // ~MsgBusImpl_redis, so we must not let a redis I/O error escape into
    // the destructor chain - mirror swss::RedisPipeline's own dtor
    // pattern and swallow exceptions here.
    try {
        FlushBMPTables();
    } catch (const std::exception& e) {
        LOG_INFO("RedisManager ExitRedisManager flush failed: %s", e.what());
    } catch (...) {
        LOG_INFO("RedisManager ExitRedisManager flush failed with unknown exception");
    }
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

    // Drain the pipeline before reading the current key set: getKeys() runs
    // on stateDb_'s connection and would otherwise miss any SETs that are
    // still buffered in pipeline_ (which uses an independent connection),
    // leaving stale entries in redis after the reset completes.
    FlushBMPTables();

    std::unique_ptr<swss::Table> stateBMPTable = std::make_unique<swss::Table>(stateDb_.get(), table);
    std::vector<std::string> keys;
    stateBMPTable->getKeys(keys);
    LOG_INFO("RedisManager ResetBMPTable data size %d", keys.size());

    stateDb_->del(keys);
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
