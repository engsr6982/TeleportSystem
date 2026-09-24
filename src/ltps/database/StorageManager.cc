#include "ll/api/data/KeyValueDB.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/service/PlayerInfo.h"

#include "ltps/TeleportSystem.h"
#include "ltps/Version.h"
#include "ltps/database/DbUtils.h"
#include "ltps/database/StorageKeys.h"
#include "ltps/database/StorageManager.h"
#include "ltps/utils/TimeUtils.h"

#include "mc/world/actor/player/Player.h"

#include "nlohmann/json.hpp"

#include <filesystem>
#include <stdexcept>
#include <unordered_map>


namespace ltps {

// 当前数据库结构版本 (meta:schema 的 version 字段)
inline constexpr int kSchemaVersion = 2;


class StorageManager::Impl {
public:
    std::filesystem::path mModDir;
    std::filesystem::path mV2Path; // database_v2
    std::filesystem::path mV1Path; // leveldb (旧库; 迁移后原样保留, 不重命名)

    std::unique_ptr<ll::data::KeyValueDB> mDatabase;

    bool mMigrated{false};       // meta:schema 有效
    bool mNeedMigration{false};  // 存在旧库, 待迁移

    ll::event::ListenerPtr                                         mJoinListener;
    std::unordered_map<std::type_index, std::unique_ptr<IStorage>> mStorages;
};


StorageManager::StorageManager() : mImpl(std::make_unique<Impl>()) {
    mImpl->mModDir = TeleportSystem::getInstance().getSelf().getModDir();
    mImpl->mV2Path = mImpl->mModDir / keys::kDatabaseV2Dir;
    mImpl->mV1Path = mImpl->mModDir / keys::kLegacyV1DirName;

    bool const v2Exists = std::filesystem::exists(mImpl->mV2Path);
    bool const v1Exists = std::filesystem::exists(mImpl->mV1Path);

    // 新旧库同时存在 = 状态歧义, 拒绝猜测 (迁移中断也会落到这里: 半成品 v2 + 未动过的 v1)
    if (v2Exists && v1Exists) {
        throw std::runtime_error(fmt::format(
            "both '{}' and '{}' exist; refusing to guess which one is authoritative. Keep exactly one: "
            "delete '{}' to migrate from the old database, or delete '{}' if the old one is no longer needed",
            mImpl->mV2Path.filename().string(),
            mImpl->mV1Path.filename().string(),
            mImpl->mV2Path.filename().string(),
            mImpl->mV1Path.filename().string()
        ));
    }

    mImpl->mDatabase = std::make_unique<ll::data::KeyValueDB>(mImpl->mV2Path);

    if (v1Exists) {
        // 只有旧库 → 迁移 (marker 由 initialize() 在迁移成功后写入)
        mImpl->mNeedMigration = true;
        return;
    }

    // 只有新库 (或全新创建): 校验 marker
    auto rawMarker = mImpl->mDatabase->get(keys::kMetaSchema);
    if (!rawMarker.has_value()) {
        if (!mImpl->mDatabase->empty()) {
            // 非空却无标记 = 迁移半途失败或数据损坏; 不自动清理, 交由人工处置
            throw std::runtime_error(fmt::format(
                "'{}' is not empty but has no schema marker (failed migration or corruption); "
                "delete '{}' explicitly to retry a clean migration, or restore it from backup",
                mImpl->mV2Path.filename().string(),
                mImpl->mV2Path.filename().string()
            ));
        }
        return; // 空库 = 全新安装
    }

    auto json = nlohmann::json::parse(rawMarker.value(), nullptr, false);
    if (json.is_discarded() || !json.is_object() || !json.contains("version") || !json["version"].is_number_integer()) {
        throw std::runtime_error(
            "database marker 'meta:schema' is corrupted, refusing to load (manual intervention required)"
        );
    }
    auto version = json["version"].get<int>();
    if (version > kSchemaVersion) {
        throw std::runtime_error(fmt::format(
            "database schema version {} is newer than supported version {}, please upgrade the plugin",
            version,
            kSchemaVersion
        ));
    }
    // version < kSchemaVersion: 预留链式迁移位
    mImpl->mMigrated = true;
}

StorageManager::~StorageManager() {
    if (mImpl->mJoinListener) {
        ll::event::EventBus::getInstance().removeListener(mImpl->mJoinListener);
        mImpl->mJoinListener.reset();
    }
}

void StorageManager::initialize() {
    auto& logger = TeleportSystem::getInstance().getSelf().getLogger();

    if (!mImpl->mMigrated) {
        // 迁移必须一次性成功: 中途任何异常 → 整体 abort (不自动清理半成品, 由人工处置)
        try {
            if (mImpl->mNeedMigration) {
                logger.info(
                    "Migrating database from '{}' to '{}'",
                    mImpl->mV1Path.filename().string(),
                    mImpl->mV2Path.filename().string()
                );

                // 旧库句柄仅在迁移期间存活, 出块即析构释放 (不驻留在 Impl)
                auto legacyDatabase = std::make_unique<ll::data::KeyValueDB>(mImpl->mV1Path);
                for (auto& [_, storage] : mImpl->mStorages) {
                    storage->migrateFromV1(*legacyDatabase, *mImpl->mDatabase);
                }
            }

            // 旧版独立文件迁移 (permission.json 等; 与旧库是否存在无关)
            for (auto& [_, storage] : mImpl->mStorages) {
                storage->migrateLegacyFiles(*mImpl->mDatabase);
            }

            // marker 最后写 (含 version): 缺失即视为失败, 下次启动直接 abort
            nlohmann::json marker{
                {      "version",                               kSchemaVersion},
                {   "migratedAt",           time_utils::getCurrentTimeString()},
                { "migratedFrom", mImpl->mNeedMigration ? "v1" : "none"},
                {"pluginVersion",                          LTPS_VERSION_STRING},
            };
            mImpl->mDatabase->set(keys::kMetaSchema, marker.dump());
            mImpl->mMigrated = true;
            logger.info("Database schema v{} ready", kSchemaVersion);
        } catch (std::exception const& e) {
            logger.error("Database migration FAILED: {}", e.what());
            logger.error(
                "The old database '{}' was left untouched. Delete '{}' and restart to retry the migration.",
                mImpl->mV1Path.filename().string(),
                mImpl->mV2Path.filename().string()
            );
            throw std::runtime_error("database migration aborted");
        }
    }

    // 残留 legacy 记录统计 (未回服玩家 / 改名后旧名下的数据), 提示管理员可用 attach 收编
    {
        std::size_t legacyCount = 0;
        db_utils::forEachPrefix(
            *mImpl->mDatabase,
            keys::kPrefixLegacy,
            [&](std::string_view /*key*/, std::string_view /*value*/) { ++legacyCount; }
        );
        if (legacyCount > 0) {
            logger.warn(
                "{} legacy record(s) remain (players who have not joined yet, or renamed away); "
                "they will be migrated automatically on their next join, or manually via '/ltps admin attach'",
                legacyCount
            );
        }
    }

    // 玩家进服懒迁移 (须早于各模块注册的监听器)
    mImpl->mJoinListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerJoinEvent>(
        [this](ll::event::PlayerJoinEvent& ev) {
            auto& player = ev.self();
            ensureUserMigrated(player.getUuid(), player.getRealName(), player.getXuid());
        }
    );
}

void StorageManager::rebuildIndexes() {
    for (auto& [_, storage] : mImpl->mStorages) {
        storage->rebuildIndexes(*mImpl->mDatabase);
    }
}

void StorageManager::ensureUserMigrated(mce::UUID const& uuid, RealName const& name, std::string_view xuid) {
    auto& db          = *mImpl->mDatabase;
    auto  profileKey  = fmt::format(keys::kDataUserProfile, uuid.asString());
    auto  escapedName = key_utils::escapeSegment(name);
    auto  refKey      = fmt::format(keys::kIndexNameRef, escapedName);

    if (db.has(profileKey)) {
        // 已迁移: 仅在改名时刷新档案与反查索引 (常态零写入)
        std::string oldName;
        if (auto raw = db.get(profileKey); raw.has_value()) {
            auto json = nlohmann::json::parse(raw.value(), nullptr, false);
            if (!json.is_discarded() && json.is_object()) {
                oldName = json.value("name", std::string{});
                if (oldName == name) {
                    return;
                }
            }
        }
        ll::data::KeyValueDB::WriteBatch batch;
        batch.set(
            profileKey,
            nlohmann::json{
                {      "name",                               name},
                {      "xuid",                  std::string{xuid}},
                {"migratedAt", time_utils::getCurrentTimeString()},
        }
                .dump()
        );
        batch.set(refKey, uuid.asString());
        if (!oldName.empty() && oldName != name) {
            // 旧名的反查索引必须删除: 名字被他人回收后, 按旧名操作会落到本玩家 uuid 上
            batch.del(fmt::format(keys::kIndexNameRef, key_utils::escapeSegment(oldName)));
        }
        db.write(batch);
        return;
    }

    // 单次扫描 legacy 组, 按业务组分桶后分发给各 Storage
    std::unordered_map<std::string, std::vector<LegacyRecord>> buckets;
    db_utils::forEachPrefix(db, keys::kPrefixLegacy, [&](std::string_view key, std::string_view value) {
        // legacy:<业务组>:<用户段>[:<数据id>]
        std::string_view rest   = key.substr(keys::kPrefixLegacy.size());
        auto             bizEnd = rest.find(':');
        if (bizEnd == std::string_view::npos) {
            return;
        }
        std::string_view afterBiz = rest.substr(bizEnd + 1);
        auto             userEnd  = afterBiz.find(':');
        std::string_view user     = (userEnd == std::string_view::npos) ? afterBiz : afterBiz.substr(0, userEnd);
        if (user != escapedName) {
            return;
        }
        buckets[std::string{rest.substr(0, bizEnd)}].emplace_back(std::string{key}, std::string{value});
    });

    std::size_t migratedRecords = 0;
    for (auto& [_, records] : buckets) {
        migratedRecords += records.size();
    }
    if (migratedRecords > 0) {
        TeleportSystem::getInstance().getSelf().getLogger().warn(
            "Migrating {} legacy record(s) under name '{}' to uuid {}; if this name was previously used by another "
            "player, verify with '/ltps admin attach'",
            migratedRecords,
            name,
            uuid.asString()
        );
    }

    ll::data::KeyValueDB::WriteBatch batch;
    for (auto& [_, storage] : mImpl->mStorages) {
        auto it = buckets.find(std::string{storage->getBusinessGroup()});
        storage->migrateUser(batch, uuid, name, it == buckets.end() ? std::vector<LegacyRecord>{} : it->second);
    }

    // profile 无条件写入: 它是懒迁移的负缓存, 否则每次读都要全扫 legacy
    batch.set(
        profileKey,
        nlohmann::json{
            {      "name",                               name},
            {      "xuid",                  std::string{xuid}},
            {"migratedAt", time_utils::getCurrentTimeString()},
    }
            .dump()
    );
    batch.set(refKey, uuid.asString());
    db.write(batch);
}

Result<void>
StorageManager::attachLegacyData(mce::UUID const& uuid, RealName const& legacyName, RealName const& currentName) {
    auto& db            = *mImpl->mDatabase;
    auto  escapedLegacy = key_utils::escapeSegment(legacyName);

    std::unordered_map<std::string, std::vector<LegacyRecord>> buckets;
    db_utils::forEachPrefix(db, keys::kPrefixLegacy, [&](std::string_view key, std::string_view value) {
        std::string_view rest   = key.substr(keys::kPrefixLegacy.size());
        auto             bizEnd = rest.find(':');
        if (bizEnd == std::string_view::npos) {
            return;
        }
        std::string_view afterBiz = rest.substr(bizEnd + 1);
        auto             userEnd  = afterBiz.find(':');
        std::string_view user     = (userEnd == std::string_view::npos) ? afterBiz : afterBiz.substr(0, userEnd);
        if (user != escapedLegacy) {
            return;
        }
        buckets[std::string{rest.substr(0, bizEnd)}].emplace_back(std::string{key}, std::string{value});
    });

    if (buckets.empty()) {
        return ll::makeI18nStringError<"No legacy records found for \"{}\"">(legacyName);
    }

    ll::data::KeyValueDB::WriteBatch batch;
    for (auto& [_, storage] : mImpl->mStorages) {
        auto it = buckets.find(std::string{storage->getBusinessGroup()});
        if (it == buckets.end()) {
            continue;
        }
        storage->migrateUser(batch, uuid, currentName, it->second);
    }
    // 保留既有档案的 xuid (若有), 仅刷新名字
    auto profileKey = fmt::format(keys::kDataUserProfile, uuid.asString());
    auto oldXuid    = std::string{};
    if (auto raw = db.get(profileKey); raw.has_value()) {
        auto json = nlohmann::json::parse(raw.value(), nullptr, false);
        if (!json.is_discarded() && json.is_object()) {
            oldXuid = json.value("xuid", std::string{});
        }
    }
    batch.set(
        profileKey,
        nlohmann::json{
            {      "name",                        currentName},
            {      "xuid",                            oldXuid},
            {"migratedAt", time_utils::getCurrentTimeString()},
    }
            .dump()
    );
    db.write(batch);
    return {};
}

Result<mce::UUID> StorageManager::resolveUuid(RealName const& name) const {
    auto refKey = fmt::format(keys::kIndexNameRef, key_utils::escapeSegment(name));
    if (auto raw = mImpl->mDatabase->get(refKey); raw.has_value() && !raw->empty()) {
        auto uuid = mce::UUID{raw.value()};
        // 反查索引须与档案当前名字一致才可信: 改名后残留的旧 ref (旧版本写下 / 名字被他人回收)
        // 盲信会把管理操作落到改名前的玩家 uuid 上
        auto profileRaw = mImpl->mDatabase->get(fmt::format(keys::kDataUserProfile, uuid.asString()));
        if (!profileRaw.has_value()) {
            return uuid; // 无档案 (异常状态): 以 ref 为准
        }
        auto profile = nlohmann::json::parse(profileRaw.value(), nullptr, false);
        if (!profile.is_discarded() && profile.is_object() && profile.value("name", std::string{}) == name) {
            return uuid;
        }
        // ref 陈旧: 落到 PlayerInfo, 取当前持有该名字的玩家
    }
    if (auto info = ll::service::PlayerInfo::getInstance().fromName(name)) {
        return info->uuid;
    }
    return ll::makeI18nStringError<"Player {} not found">(name);
}

void StorageManager::registerStorageImpl(std::type_index type, std::unique_ptr<IStorage> storage) {
    mImpl->mStorages[type] = std::move(storage);
}

IStorage* StorageManager::getStorageImpl(std::type_index type) const {
    auto it = mImpl->mStorages.find(type);
    if (it == mImpl->mStorages.end()) {
        return nullptr;
    }
    return it->second.get();
}

ll::data::KeyValueDB& StorageManager::getDatabase() const { return *mImpl->mDatabase; }


} // namespace ltps