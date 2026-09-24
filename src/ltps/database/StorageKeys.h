#pragma once
#include <string_view>

// 数据库 key 常量全集
// 布局: <库分组>:<业务组>:[用户唯一标识符]:[数据唯一标识符] => <数据>
// 库分组: meta(元数据) / data(uuid 业务数据) / legacy(过渡态 realName 数据) / index(索引)
// 用户段: uuid.asString(); 全局数据用 "-"; legacy 组为转义后的 realName
// 模板 key 一律 fmt::format(keys::kXxx, ...) 实例化; iterPrefix 只接受 kPrefix* 常量
namespace ltps::keys {

// ── meta ──────────────────────────────────────────────────────────────
inline constexpr std::string_view kMetaSchema = "meta:schema";

// ── data 模板 key (fmt 占位: <uuid> <数据id>) ─────────────────────────
inline constexpr std::string_view kDataHome              = "data:home:{}:{}";         // <uuid> <homeName>
inline constexpr std::string_view kDataWarp              = "data:warp:-:{}";          // <warpName>
inline constexpr std::string_view kDataDeath             = "data:death:{}:{}";        // <uuid> <ts[-n]>
inline constexpr std::string_view kDataSetting           = "data:setting:{}";         // <uuid>
inline constexpr std::string_view kDataPermission        = "data:permission:{}";      // <uuid>
inline constexpr std::string_view kDataPermissionDefault = "data:permission:-:default";
inline constexpr std::string_view kDataUserProfile       = "data:user:{}:profile";    // <uuid>

// ── legacy 模板 key (过渡态, 用户段为转义 realName) ──────────────────
inline constexpr std::string_view kLegacyHome       = "legacy:home:{}:{}";            // <realName> <homeName>
inline constexpr std::string_view kLegacyDeath      = "legacy:death:{}:{}";           // <realName> <ts[-n]>
inline constexpr std::string_view kLegacySetting    = "legacy:setting:{}";            // <realName>
inline constexpr std::string_view kLegacyPermission = "legacy:permission:{}";         // <realName>

// ── index 模板 key ───────────────────────────────────────────────────
inline constexpr std::string_view kIndexHome    = "index:home:{}";                    // <uuid> -> 家园名列表
inline constexpr std::string_view kIndexWarp    = "index:warp";                       // -> 传送点名列表
inline constexpr std::string_view kIndexDeath   = "index:death:{}";                   // <uuid> -> 完整 id 列表
inline constexpr std::string_view kIndexNameRef = "index:ref:name:{}";                // <realName> -> <uuid>

// ── iterPrefix 前缀常量 ──────────────────────────────────────────────
inline constexpr std::string_view kPrefixMeta            = "meta:";
inline constexpr std::string_view kPrefixData            = "data:";
inline constexpr std::string_view kPrefixDataHome        = "data:home:";
inline constexpr std::string_view kPrefixDataHomeUser    = "data:home:{}:";           // <uuid>
inline constexpr std::string_view kPrefixDataWarp        = "data:warp:-:";
inline constexpr std::string_view kPrefixDataDeath       = "data:death:";
inline constexpr std::string_view kPrefixDataDeathUser   = "data:death:{}:";          // <uuid>
inline constexpr std::string_view kPrefixDataSetting     = "data:setting:";
inline constexpr std::string_view kPrefixDataPermission  = "data:permission:";
inline constexpr std::string_view kPrefixDataUser        = "data:user:";
inline constexpr std::string_view kPrefixLegacy          = "legacy:";
inline constexpr std::string_view kPrefixLegacyHome      = "legacy:home:";
inline constexpr std::string_view kPrefixLegacyDeath     = "legacy:death:";
inline constexpr std::string_view kPrefixLegacySetting   = "legacy:setting:";
inline constexpr std::string_view kPrefixLegacyPermission= "legacy:permission:";
inline constexpr std::string_view kPrefixIndex           = "index:";

// ── 目录 ─────────────────────────────────────────────────────────────
inline constexpr std::string_view kDatabaseV2Dir   = "database_v2";
inline constexpr std::string_view kLegacyV1DirName = "leveldb";      // 旧库目录名 (迁移后原样保留)
inline constexpr std::string_view kLangDirName     = "lang";

// ── 保留段 ───────────────────────────────────────────────────────────
inline constexpr std::string_view kGlobalUserSegment = "-";

} // namespace ltps::keys