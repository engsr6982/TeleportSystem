#include "ll/api/data/KeyValueDB.h"
#include "PermissionStorage.h"
#include "ll/api/io/FileUtils.h"
#include "ll/api/service/PlayerInfo.h"
#include "ll/api/utils/StringUtils.h"
#include "ltps/TeleportSystem.h"
#include "ltps/database/DbUtils.h"
#include "ltps/database/StorageKeys.h"
#include "ltps/utils/StringUtils.h"
#include "magic_enum/magic_enum.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <optional>


namespace ltps {

namespace {

std::string serializeMask(int mask) { return nlohmann::json{{"perms", mask}}.dump(); }

int parseMask(std::optional<std::string> const& raw) {
    if (!raw.has_value()) {
        return 0;
    }
    auto json = nlohmann::json::parse(raw.value(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return 0;
    }
    return json.value("perms", 0);
}

PermissionStorage::Permissions maskToPermissions(int mask) {
    PermissionStorage::Permissions result;
    for (auto const& p : magic_enum::enum_values<PermissionStorage::Permission>()) {
        if (p == PermissionStorage::Permission::None) {
            continue;
        }
        if ((mask & static_cast<int>(p)) != 0) {
            result.push_back(p);
        }
    }
    return result;
}

} // namespace


PermissionStorage::PermissionStorage(ll::data::KeyValueDB& db) : IStorage(db) {}

std::string_view PermissionStorage::getBusinessGroup() const { return "permission"; }

void PermissionStorage::expandLegacyData(nlohmann::json const& json, ll::data::KeyValueDB& v2) {
    if (!json.is_object()) {
        return;
    }
    v2.set(keys::kDataPermissionDefault, serializeMask(json.value("mDefaultPerms", 0)));

    auto playerPerms = json.find("mPlayerPerms");
    if (playerPerms == json.end() || !playerPerms->is_object()) {
        return;
    }
    for (auto& [name, mask] : playerPerms->items()) {
        if (!mask.is_number_integer()) {
            continue;
        }
        auto text = serializeMask(mask.get<int>());
        if (auto info = ll::service::PlayerInfo::getInstance().fromName(name)) {
            v2.set(fmt::format(keys::kDataPermission, info->uuid.asString()), text);
        } else {
            v2.set(fmt::format(keys::kLegacyPermission, key_utils::escapeSegment(name)), text);
        }
    }
}

void PermissionStorage::migrateFromV1(ll::data::KeyValueDB& v1, ll::data::KeyValueDB& v2) {
    auto raw = v1.get(LEGACY_BIG_KEY);
    if (!raw.has_value()) {
        return;
    }
    expandLegacyData(nlohmann::json::parse(raw.value(), nullptr, false), v2);
}

void PermissionStorage::migrateLegacyFiles(ll::data::KeyValueDB& v2) {
    if (!_hasLegacyPermissionFile()) {
        return;
    }
    auto path    = TeleportSystem::getInstance().getSelf().getDataDir() / LEGACY_FILE_NAME;
    auto content = ll::file_utils::readFile(path);
    if (!content.has_value()) {
        throw std::runtime_error("Failed to read legacy permission file");
    }
    expandLegacyData(nlohmann::json::parse(content.value(), nullptr, false), v2);
    _renameLegacyPermissionFile();
    TeleportSystem::getInstance().getSelf().getLogger().trace("Migrated legacy permission file");
}

void PermissionStorage::migrateUser(
    ll::data::KeyValueDB::WriteBatch&            batch,
    mce::UUID const&                 uuid,
    RealName const& /*name*/,
    std::vector<LegacyRecord> const& legacyRecords
) {
    for (auto& [key, value] : legacyRecords) {
        batch.set(fmt::format(keys::kDataPermission, uuid.asString()), value);
        batch.del(key);
    }
}


bool PermissionStorage::_hasLegacyPermissionFile() const {
    auto path = TeleportSystem::getInstance().getSelf().getDataDir() / LEGACY_FILE_NAME;
    return std::filesystem::exists(path);
}

void PermissionStorage::_renameLegacyPermissionFile() const {
    if (!_hasLegacyPermissionFile()) {
        return;
    }
    auto path = TeleportSystem::getInstance().getSelf().getDataDir() / LEGACY_FILE_NAME;
    std::filesystem::rename(path, path.replace_extension(".old"));
}


bool PermissionStorage::hasDefaultPermission(Permission permission) const {
    return (parseMask(getDatabase().get(keys::kDataPermissionDefault)) & static_cast<int>(permission)) != 0;
}

bool PermissionStorage::hasPermission(mce::UUID const& uuid, Permission permission, bool includeDefault) const {
    if (includeDefault && hasDefaultPermission(permission)) {
        return true;
    }
    auto mask = parseMask(getDatabase().get(fmt::format(keys::kDataPermission, uuid.asString())));
    return (mask & static_cast<int>(permission)) != 0;
}

Result<void> PermissionStorage::grantPermission(mce::UUID const& uuid, Permission permission) {
    if (hasPermission(uuid, permission, false)) {
        return ll::makeI18nStringError<"Permission already granted">();
    }
    auto key  = fmt::format(keys::kDataPermission, uuid.asString());
    auto mask = parseMask(getDatabase().get(key)) | static_cast<int>(permission);
    getDatabase().set(key, serializeMask(mask));
    return {};
}

Result<void> PermissionStorage::revokePermission(mce::UUID const& uuid, Permission permission) {
    if (!hasPermission(uuid, permission, false)) {
        return ll::makeI18nStringError<"Permission not granted">();
    }
    auto key  = fmt::format(keys::kDataPermission, uuid.asString());
    auto mask = parseMask(getDatabase().get(key)) & ~static_cast<int>(permission);
    getDatabase().set(key, serializeMask(mask));
    return {};
}

PermissionStorage::Permissions PermissionStorage::getPermissions(mce::UUID const& uuid) const {
    return maskToPermissions(parseMask(getDatabase().get(fmt::format(keys::kDataPermission, uuid.asString()))));
}


Result<void> PermissionStorage::grantDefaultPermission(Permission permission) {
    if (hasDefaultPermission(permission)) {
        return ll::makeI18nStringError<"Permission already granted">();
    }
    auto mask = parseMask(getDatabase().get(keys::kDataPermissionDefault)) | static_cast<int>(permission);
    getDatabase().set(keys::kDataPermissionDefault, serializeMask(mask));
    return {};
}

Result<void> PermissionStorage::revokeDefaultPermission(Permission permission) {
    if (!hasDefaultPermission(permission)) {
        return ll::makeI18nStringError<"Permission not granted">();
    }
    auto mask = parseMask(getDatabase().get(keys::kDataPermissionDefault)) & ~static_cast<int>(permission);
    getDatabase().set(keys::kDataPermissionDefault, serializeMask(mask));
    return {};
}

PermissionStorage::Permissions PermissionStorage::getDefaultPermissions() const {
    return maskToPermissions(parseMask(getDatabase().get(keys::kDataPermissionDefault)));
}

Result<std::pair<PermissionStorage::Permissions, PermissionStorage::Permissions>>
PermissionStorage::tracePermissions(mce::UUID const& uuid) const {
    return std::make_pair(getDefaultPermissions(), getPermissions(uuid));
}


Result<void> PermissionStorage::grantPermissionByName(RealName const& realName, Permission permission) {
    if (auto uuid = TeleportSystem::getInstance().getStorageManager().resolveUuid(realName)) {
        return grantPermission(uuid.value(), permission);
    }
    // 解析不到 uuid (玩家未在 v2 进过服): 写 legacy 组, 等其进服后由懒迁移收编
    auto key  = fmt::format(keys::kLegacyPermission, key_utils::escapeSegment(realName));
    auto mask = parseMask(getDatabase().get(key));
    if ((mask & static_cast<int>(permission)) != 0) {
        return ll::makeI18nStringError<"Permission already granted">();
    }
    getDatabase().set(key, serializeMask(mask | static_cast<int>(permission)));
    return {};
}

Result<void> PermissionStorage::revokePermissionByName(RealName const& realName, Permission permission) {
    if (auto uuid = TeleportSystem::getInstance().getStorageManager().resolveUuid(realName)) {
        return revokePermission(uuid.value(), permission);
    }
    auto key  = fmt::format(keys::kLegacyPermission, key_utils::escapeSegment(realName));
    auto mask = parseMask(getDatabase().get(key));
    if ((mask & static_cast<int>(permission)) == 0) {
        return ll::makeI18nStringError<"Permission not granted">();
    }
    getDatabase().set(key, serializeMask(mask & ~static_cast<int>(permission)));
    return {};
}

Result<std::pair<PermissionStorage::Permissions, PermissionStorage::Permissions>>
PermissionStorage::tracePermissionsByName(RealName const& realName) const {
    if (auto uuid = TeleportSystem::getInstance().getStorageManager().resolveUuid(realName)) {
        return tracePermissions(uuid.value());
    }
    auto key = fmt::format(keys::kLegacyPermission, key_utils::escapeSegment(realName));
    auto raw = getDatabase().get(key);
    if (!raw.has_value()) {
        return ll::makeI18nStringError<"Player {} not found">(realName);
    }
    return std::make_pair(getDefaultPermissions(), maskToPermissions(parseMask(raw)));
}


std::string PermissionStorage::toString(Permission permission) {
    return ll::string_utils::toSnakeCase(std::string(magic_enum::enum_name(permission)));
}

std::optional<PermissionStorage::Permission> PermissionStorage::fromString(std::string const& str) {
    return magic_enum::enum_cast<Permission>(string_utils::snake_to_pascal(str));
}

std::vector<PermissionStorage::Permission> PermissionStorage::getPermissions() {
    auto vals = magic_enum::enum_values<PermissionStorage::Permission>();
    return {vals.begin(), vals.end()};
}

Result<std::vector<PermissionStorage::Permission>> PermissionStorage::resolve(std::string const& permissions) {
    if (permissions.empty()) {
        return ll::makeI18nStringError<"Please provide permissions">();
    }
    auto&                                      logger = TeleportSystem::getInstance().getSelf().getLogger();
    std::vector<PermissionStorage::Permission> result;
    std::stringstream                          ss(permissions);
    std::string                                token;
    while (std::getline(ss, token, '|')) {
        if (token.empty()) {
            logger.trace("Empty token, skipping");
            continue;
        }
        auto perm = fromString(token);
        if (!perm.has_value()) {
            return ll::makeI18nStringError<"Parsing failed, invalid permissions: {}">(token);
        }
        logger.trace("Parsed permission: {} -> {}", token, toString(perm.value()));
        result.push_back(perm.value());
    }
    return result;
}


} // namespace ltps