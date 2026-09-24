#include "ltps/base/BaseCommand.h"
#include "ll/api/command/Command.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ltps/TeleportSystem.h"
#include "ltps/Version.h"
#include "ltps/base/Config.h"
#include "ltps/database/PermissionStorage.h"
#include "ltps/database/StorageManager.h"
#include "ltps/modules/ModuleManager.h"
#include "ltps/modules/setting/gui/SettingGUI.h"
#include "ltps/utils/McUtils.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandOutput.h"


namespace ltps {

struct PermListActionParam {
    enum class Action { Builtin, Default };
    Action action;
};

struct PermListPlayerParam {
    std::string realName;
};

enum class PermAction { Add, Remove };

struct PermDefaultActionParam {
    PermAction                    action;
    PermissionStorage::Permission permission;
};

struct PermPlayerActionParam {
    PermAction                    action;
    std::string                   realName;
    PermissionStorage::Permission permission;
};


struct PermBatchDefaultActionParam {
    PermAction  action;
    std::string permissions;
};

struct PermBatchPlayerActionParam {
    PermAction  action;
    std::string realName;
    std::string permissions;
};

struct AdminAttachParam {
    std::string legacyName; // 旧数据所在的玩家名
    std::string playerName; // 目标玩家 (须进过服)
};


void BaseCommand::setup() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand("ltps", MOD_NAME);

    // ltps version
    cmd.overload().text("version").execute([](CommandOrigin const& /* origin */, CommandOutput& output) {
        mc_utils::sendText(output, LTPS_VERSION_STRING);
    });

    // ltps reload
    cmd.overload().text("reload").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getOriginType() != CommandOriginType::DedicatedServer) {
            mc_utils::sendText<mc_utils::Error>(output, "This command can only be run from the server console"_tr());
            return;
        }

        loadConfig();
        TeleportSystem::getInstance().getModuleManager().reconfigureModules();
        EconomySystemManager::getInstance().reloadEconomySystem();
        mc_utils::sendText(output, "Configuration reloaded"_tr());
    });

    // ltps setting
    cmd.overload().text("setting").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getOriginType() != CommandOriginType::Player) {
            mc_utils::sendText<mc_utils::Error>(output, "This command can only be run by a player"_tr());
            return;
        }
        auto& player = *static_cast<Player*>(origin.getEntity());
        setting::SettingGUI::sendMainGUI(player);
    });

    // ======= 权限 =======
    // /ltps perm list <builtin|default> # [控制台] 列出 内置权限 / 默认权限
    cmd.overload<PermListActionParam>().text("perm").text("list").required("action").execute(
        [](CommandOrigin const& origin, CommandOutput& output, PermListActionParam const& param) {
            if (origin.getOriginType() != CommandOriginType::DedicatedServer) {
                mc_utils::sendText<mc_utils::Error>(
                    output,
                    "This command can only be run from the server console"_tr()
                );
                return;
            }

            switch (param.action) {
            case PermListActionParam::Action::Builtin: {
                auto perms = PermissionStorage::getPermissions();
                mc_utils::sendText(output, "Builtin permissions: "_tr());
                for (auto& perm : perms) {
                    mc_utils::sendText(output, " - {}", PermissionStorage::toString(perm));
                }
                mc_utils::sendText(output, "{} permission(s) in total"_tr(perms.size()));
                break;
            }
            case PermListActionParam::Action::Default: {
                auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();
                if (!storage) {
                    mc_utils::sendText<mc_utils::Error>(output, "Permission storage is unavailable"_tr());
                    return;
                }
                auto perms = storage->getDefaultPermissions();

                mc_utils::sendText(output, "Default permissions: "_tr());
                for (auto& perm : perms) {
                    mc_utils::sendText(output, " - {}", PermissionStorage::toString(perm));
                }
                mc_utils::sendText(output, "{} permission(s) in total"_tr(perms.size()));
                break;
            }
            }
        }
    );

    // /ltps perm list player <realName> # [控制台] 列出玩家权限
    cmd.overload<PermListPlayerParam>()
        .text("perm")
        .text("list")
        .text("player")
        .required("realName")
        .execute([](CommandOrigin const& origin, CommandOutput& output, PermListPlayerParam const& param) {
            if (origin.getOriginType() != CommandOriginType::DedicatedServer) {
                mc_utils::sendText<mc_utils::Error>(
                    output,
                    "This command can only be run from the server console"_tr()
                );
                return;
            }

            auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();
            if (!storage) {
                mc_utils::sendText<mc_utils::Error>(output, "Permission storage is unavailable"_tr());
                return;
            }

            auto perms = storage->tracePermissionsByName(param.realName);
            if (!perms.has_value()) {
                mc_utils::sendText<mc_utils::Error>(output, perms.error().message());
                return;
            }

            // 玩家 "{}" 拥有的权限：
            //  # 默认权限
            //    - <权限>
            //  # 玩家权限
            //    - <权限>
            // 共计 {} 个权限
            mc_utils::sendText(output, "Permissions of player \"{}\":"_tr(param.realName));
            mc_utils::sendText(output, " # Default permissions"_tr());
            for (auto& perm : perms->first) {
                mc_utils::sendText(output, "  - {}", PermissionStorage::toString(perm));
            }
            mc_utils::sendText(output, " # Player permissions"_tr());
            for (auto& perm : perms->second) {
                mc_utils::sendText(output, "  - {}", PermissionStorage::toString(perm));
            }
            mc_utils::sendText(output, "{} permission(s) in total"_tr(perms->first.size() + perms->second.size()));
        });

    // /ltps perm <add|remove> default <permission> # [控制台] 添加或移除默认权限
    cmd.overload<PermDefaultActionParam>()
        .text("perm")
        .required("action")
        .text("default")
        .required("permission")
        .execute([](CommandOrigin const& origin, CommandOutput& output, PermDefaultActionParam const& param) {
            if (origin.getOriginType() != CommandOriginType::DedicatedServer) {
                mc_utils::sendText<mc_utils::Error>(
                    output,
                    "This command can only be run from the server console"_tr()
                );
                return;
            }

            auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();
            if (!storage) {
                mc_utils::sendText<mc_utils::Error>(output, "Permission storage is unavailable"_tr());
                return;
            }

            switch (param.action) {
            case PermAction::Add: {
                if (auto res = storage->grantDefaultPermission(param.permission)) {
                    mc_utils::sendText(
                        output,
                        "\"{}\" added to default permissions"_tr(PermissionStorage::toString(param.permission))
                    );
                } else {
                    mc_utils::sendText<mc_utils::Error>(
                        output,
                        "Failed to add default permission: {}"_tr(res.error().message())
                    );
                }
                break;
            }
            case PermAction::Remove: {
                if (auto res = storage->revokeDefaultPermission(param.permission)) {
                    mc_utils::sendText(
                        output,
                        "\"{}\" removed from default permissions"_tr(PermissionStorage::toString(param.permission))
                    );
                } else {
                    mc_utils::sendText<mc_utils::Error>(
                        output,
                        "Failed to remove default permission: {}"_tr(res.error().message())
                    );
                }
                break;
            }
            }
        });

    // /ltps perm <add|remove> player <realName> <permission> # [控制台] 添加或移除玩家权限
    cmd.overload<PermPlayerActionParam>()
        .text("perm")
        .required("action")
        .text("player")
        .required("realName")
        .required("permission")
        .execute([](CommandOrigin const& origin, CommandOutput& output, PermPlayerActionParam const& param) {
            if (origin.getOriginType() != CommandOriginType::DedicatedServer) {
                mc_utils::sendText<mc_utils::Error>(
                    output,
                    "This command can only be run from the server console"_tr()
                );
                return;
            }

            auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();
            if (!storage) {
                mc_utils::sendText<mc_utils::Error>(output, "Permission storage is unavailable"_tr());
                return;
            }

            switch (param.action) {
            case PermAction::Add: {
                if (auto res = storage->grantPermissionByName(param.realName, param.permission)) {
                    mc_utils::sendText(
                        output,
                        "\"{}\" granted to player \"{}\""_tr(PermissionStorage::toString(param.permission), param.realName)
                    );
                } else {
                    mc_utils::sendText<mc_utils::Error>(
                        output,
                        "Failed to grant player permission: {}"_tr(res.error().message())
                    );
                }
                break;
            }
            case PermAction::Remove: {
                if (auto res = storage->revokePermissionByName(param.realName, param.permission)) {
                    mc_utils::sendText(
                        output,
                        "\"{}\" removed from player \"{}\""_tr(
                            PermissionStorage::toString(param.permission),
                            param.realName
                        )
                    );
                } else {
                    mc_utils::sendText<mc_utils::Error>(
                        output,
                        "Failed to revoke player permission: {}"_tr(res.error().message())
                    );
                }
                break;
            }
            }
        });

    // /ltps perm batch <add|remove> default <permissions> # [控制台] 批量添加或移除默认权限 (用'|'分隔)
    cmd.overload<PermBatchDefaultActionParam>()
        .text("perm")
        .text("batch")
        .required("action")
        .text("default")
        .required("permissions")
        .execute([](CommandOrigin const& origin, CommandOutput& output, PermBatchDefaultActionParam const& param) {
            if (origin.getOriginType() != CommandOriginType::DedicatedServer) {
                mc_utils::sendText<mc_utils::Error>(
                    output,
                    "This command can only be run from the server console"_tr()
                );
                return;
            }

            auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();
            if (!storage) {
                mc_utils::sendText<mc_utils::Error>(output, "Permission storage is unavailable"_tr());
                return;
            }

            auto perms = PermissionStorage::resolve(param.permissions);
            if (!perms.has_value()) {
                mc_utils::sendText<mc_utils::Error>(
                    output,
                    "Failed to parse permissions: {}"_tr(perms.error().message())
                );
                return;
            }

            switch (param.action) {
            case PermAction::Add: {
                for (auto const& perm : perms.value()) {
                    if (auto res = storage->grantDefaultPermission(perm)) {
                        mc_utils::sendText(
                            output,
                            "\"{}\" added to default permissions"_tr(PermissionStorage::toString(perm))
                        );
                    } else {
                        mc_utils::sendText<mc_utils::Error>(
                            output,
                            "Failed to add default permission: {}"_tr(res.error().message())
                        );
                    }
                }
                break;
            }
            case PermAction::Remove: {
                for (auto const& perm : perms.value()) {
                    if (auto res = storage->revokeDefaultPermission(perm)) {
                        mc_utils::sendText(
                            output,
                            "\"{}\" removed from default permissions"_tr(PermissionStorage::toString(perm))
                        );
                    } else {
                        mc_utils::sendText<mc_utils::Error>(
                            output,
                            "Failed to remove default permission: {}"_tr(res.error().message())
                        );
                    }
                }
                break;
            }
            }
        });

    // /ltps perm batch <add|remove> player <realName> <permissions> # [控制台] 批量添加或移除玩家权限 (用'|'分隔)
    cmd.overload<PermBatchPlayerActionParam>()
        .text("perm")
        .text("batch")
        .required("action")
        .text("player")
        .required("realName")
        .required("permissions")
        .execute([](CommandOrigin const& origin, CommandOutput& output, PermBatchPlayerActionParam const& param) {
            if (origin.getOriginType() != CommandOriginType::DedicatedServer) {
                mc_utils::sendText<mc_utils::Error>(
                    output,
                    "This command can only be run from the server console"_tr()
                );
                return;
            }

            auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();
            if (!storage) {
                mc_utils::sendText<mc_utils::Error>(output, "Permission storage is unavailable"_tr());
                return;
            }

            auto perms = PermissionStorage::resolve(param.permissions);
            if (!perms.has_value()) {
                mc_utils::sendText<mc_utils::Error>(
                    output,
                    "Failed to parse permissions: {}"_tr(perms.error().message())
                );
                return;
            }

            switch (param.action) {
            case PermAction::Add: {
                for (auto const& perm : perms.value()) {
                    if (auto res = storage->grantPermissionByName(param.realName, perm)) {
                        mc_utils::sendText(
                            output,
                            "\"{}\" added to player \"{}\""_tr(PermissionStorage::toString(perm), param.realName)
                        );
                    } else {
                        mc_utils::sendText<mc_utils::Error>(
                            output,
                            "Failed to grant player permission: {}"_tr(res.error().message())
                        );
                    }
                }
                break;
            }
            case PermAction::Remove: {
                for (auto const& perm : perms.value()) {
                    if (auto res = storage->revokePermissionByName(param.realName, perm)) {
                        mc_utils::sendText(
                            output,
                            "\"{}\" removed from player \"{}\""_tr(PermissionStorage::toString(perm), param.realName)
                        );
                    } else {
                        mc_utils::sendText<mc_utils::Error>(
                            output,
                            "Failed to revoke player permission: {}"_tr(res.error().message())
                        );
                    }
                }
                break;
            }
            }
        });

    // ======= 管理 =======
    // /ltps admin rebuild-index # [控制台] 全量重建索引记录
    cmd.overload().text("admin").text("rebuild-index").execute(
        [](CommandOrigin const& origin, CommandOutput& output) {
            if (origin.getOriginType() != CommandOriginType::DedicatedServer) {
                mc_utils::sendText<mc_utils::Error>(
                    output,
                    "This command can only be run from the server console"_tr()
                );
                return;
            }

            TeleportSystem::getInstance().getStorageManager().rebuildIndexes();
            mc_utils::sendText(output, "Indexes rebuilt"_tr());
        }
    );

    // /ltps admin attach <legacyName> <playerName> # [控制台] 将旧名字下的数据收编到该玩家
    cmd.overload<AdminAttachParam>()
        .text("admin")
        .text("attach")
        .required("legacyName")
        .required("playerName")
        .execute([](CommandOrigin const& origin, CommandOutput& output, AdminAttachParam const& param) {
            if (origin.getOriginType() != CommandOriginType::DedicatedServer) {
                mc_utils::sendText<mc_utils::Error>(
                    output,
                    "This command can only be run from the server console"_tr()
                );
                return;
            }

            auto& storageManager = TeleportSystem::getInstance().getStorageManager();

            auto uuid = storageManager.resolveUuid(param.playerName);
            if (!uuid) {
                mc_utils::sendText<mc_utils::Error>(
                    output,
                    "Failed to resolve player {}: they must join the server once first"_tr(param.playerName)
                );
                return;
            }

            if (auto res = storageManager.attachLegacyData(uuid.value(), param.legacyName, param.playerName)) {
                mc_utils::sendText(
                    output,
                    "Legacy data of \"{}\" attached to player \"{}\""_tr(param.legacyName, param.playerName)
                );
            } else {
                mc_utils::sendText<mc_utils::Error>(output, res.error().message());
            }
        }
    );
}


} // namespace ltps