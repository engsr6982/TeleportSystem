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


void BaseCommand::setup() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand("ltps", MOD_NAME);

    // ltps version
    cmd.overload().text("version").execute([](CommandOrigin const& /* origin */, CommandOutput& output) {
        mc_utils::sendText(output, LTPS_VERSION_STRING);
    });

    // ltps reload
    cmd.overload().text("reload").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getOriginType() != CommandOriginType::DedicatedServer) {
            mc_utils::sendText<mc_utils::Error>(output, "此命令只能在服务器端执行"_tr());
            return;
        }

        loadConfig();
        TeleportSystem::getInstance().getModuleManager().reconfigureModules();
        EconomySystemManager::getInstance().reloadEconomySystem();
        mc_utils::sendText(output, "配置已重载"_tr());
    });

    // ltps setting
    cmd.overload().text("setting").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getOriginType() != CommandOriginType::Player) {
            mc_utils::sendText<mc_utils::Error>(output, "此命令只能由玩家执行"_tr());
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
                mc_utils::sendText<mc_utils::Error>(output, "此命令只能在服务器端执行"_tr());
                return;
            }

            switch (param.action) {
            case PermListActionParam::Action::Builtin: {
                auto perms = PermissionStorage::getPermissions();
                mc_utils::sendText(output, "内置权限: "_tr());
                for (auto& perm : perms) {
                    mc_utils::sendText(output, " - {}", PermissionStorage::toString(perm));
                }
                mc_utils::sendText(output, "共 {} 个权限"_tr(perms.size()));
                break;
            }
            case PermListActionParam::Action::Default: {
                auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();
                if (!storage) {
                    mc_utils::sendText<mc_utils::Error>(output, "权限存储不可用"_tr());
                    return;
                }
                auto perms = storage->getDefaultPermissions();

                mc_utils::sendText(output, "默认权限: "_tr());
                for (auto& perm : perms) {
                    mc_utils::sendText(output, " - {}", PermissionStorage::toString(perm));
                }
                mc_utils::sendText(output, "共 {} 个权限"_tr(perms.size()));
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
                mc_utils::sendText<mc_utils::Error>(output, "此命令只能在服务器端执行"_tr());
                return;
            }

            auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();
            if (!storage) {
                mc_utils::sendText<mc_utils::Error>(output, "权限存储不可用"_tr());
                return;
            }

            auto perms = storage->tracePermissions(param.realName);
            if (!perms.has_value()) {
                mc_utils::sendText<mc_utils::Error>(output, "玩家不存在"_tr());
                return;
            }

            // 玩家 "{}" 拥有的权限：
            //  # 默认权限
            //    - <权限>
            //  # 玩家权限
            //    - <权限>
            // 共计 {} 个权限
            mc_utils::sendText(output, "玩家 \"{}\" 拥有的权限："_tr(param.realName));
            mc_utils::sendText(output, " # 默认权限"_tr());
            for (auto& perm : perms->first) {
                mc_utils::sendText(output, "  - {}", PermissionStorage::toString(perm));
            }
            mc_utils::sendText(output, " # 玩家权限"_tr());
            for (auto& perm : perms->second) {
                mc_utils::sendText(output, "  - {}", PermissionStorage::toString(perm));
            }
            mc_utils::sendText(output, "总计 {} 个权限"_tr(perms->first.size() + perms->second.size()));
        });

    // /ltps perm <add|remove> default <permission> # [控制台] 添加或移除默认权限
    cmd.overload<PermDefaultActionParam>()
        .text("perm")
        .required("action")
        .text("default")
        .required("permission")
        .execute([](CommandOrigin const& origin, CommandOutput& output, PermDefaultActionParam const& param) {
            if (origin.getOriginType() != CommandOriginType::DedicatedServer) {
                mc_utils::sendText<mc_utils::Error>(output, "此命令只能在服务器端执行"_tr());
                return;
            }

            auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();
            if (!storage) {
                mc_utils::sendText<mc_utils::Error>(output, "权限存储不可用"_tr());
                return;
            }

            switch (param.action) {
            case PermAction::Add: {
                if (auto res = storage->grantDefaultPermission(param.permission)) {
                    mc_utils::sendText(
                        output,
                        "\"{}\" 已添加到默认权限"_tr(PermissionStorage::toString(param.permission))
                    );
                } else {
                    mc_utils::sendText<mc_utils::Error>(output, "添加默认权限失败: {}"_tr(res.error()));
                }
                break;
            }
            case PermAction::Remove: {
                if (auto res = storage->revokeDefaultPermission(param.permission)) {
                    mc_utils::sendText(
                        output,
                        "\"{}\" 已从默认权限中移除"_tr(PermissionStorage::toString(param.permission))
                    );
                } else {
                    mc_utils::sendText<mc_utils::Error>(output, "移除默认权限失败: {}"_tr(res.error()));
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
                mc_utils::sendText<mc_utils::Error>(output, "此命令只能在服务器端执行"_tr());
                return;
            }

            auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();
            if (!storage) {
                mc_utils::sendText<mc_utils::Error>(output, "权限存储不可用"_tr());
                return;
            }

            switch (param.action) {
            case PermAction::Add: {
                if (auto res = storage->grantPermission(param.realName, param.permission)) {
                    mc_utils::sendText(
                        output,
                        "\"{}\" 已授予玩家 \"{}\""_tr(PermissionStorage::toString(param.permission), param.realName)
                    );
                } else {
                    mc_utils::sendText<mc_utils::Error>(output, "授予玩家权限失败: {}"_tr(res.error()));
                }
                break;
            }
            case PermAction::Remove: {
                if (auto res = storage->revokePermission(param.realName, param.permission)) {
                    mc_utils::sendText(
                        output,
                        "\"{}\" 已从玩家 \"{}\" 中移除"_tr(
                            PermissionStorage::toString(param.permission),
                            param.realName
                        )
                    );
                } else {
                    mc_utils::sendText<mc_utils::Error>(output, "移除玩家权限失败: {}"_tr(res.error()));
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
                mc_utils::sendText<mc_utils::Error>(output, "此命令只能在服务器端执行"_tr());
                return;
            }

            auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();
            if (!storage) {
                mc_utils::sendText<mc_utils::Error>(output, "权限存储不可用"_tr());
                return;
            }

            auto perms = PermissionStorage::resolve(param.permissions);
            if (!perms.has_value()) {
                mc_utils::sendText(output, "解析权限失败: {}"_tr(perms.error()));
                return;
            }

            switch (param.action) {
            case PermAction::Add: {
                for (auto const& perm : perms.value()) {
                    if (auto res = storage->grantDefaultPermission(perm)) {
                        mc_utils::sendText(output, "\"{}\" 已添加到默认权限"_tr(PermissionStorage::toString(perm)));
                    } else {
                        mc_utils::sendText<mc_utils::Error>(output, "添加默认权限失败: {}"_tr(res.error()));
                    }
                }
                break;
            }
            case PermAction::Remove: {
                for (auto const& perm : perms.value()) {
                    if (auto res = storage->revokeDefaultPermission(perm)) {
                        mc_utils::sendText(output, "\"{}\" 已从默认权限中移除"_tr(PermissionStorage::toString(perm)));
                    } else {
                        mc_utils::sendText<mc_utils::Error>(output, "移除默认权限失败: {}"_tr(res.error()));
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
                mc_utils::sendText<mc_utils::Error>(output, "此命令只能在服务器端执行"_tr());
                return;
            }

            auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();
            if (!storage) {
                mc_utils::sendText<mc_utils::Error>(output, "权限存储不可用"_tr());
                return;
            }

            auto perms = PermissionStorage::resolve(param.permissions);
            if (!perms.has_value()) {
                mc_utils::sendText(output, "解析权限失败: {}"_tr(perms.error()));
                return;
            }

            switch (param.action) {
            case PermAction::Add: {
                for (auto const& perm : perms.value()) {
                    if (auto res = storage->grantPermission(param.realName, perm)) {
                        mc_utils::sendText(
                            output,
                            "\"{}\" 已添加到玩家 \"{}\""_tr(PermissionStorage::toString(perm), param.realName)
                        );
                    } else {
                        mc_utils::sendText<mc_utils::Error>(output, "添加玩家权限失败: {}"_tr(res.error()));
                    }
                }
                break;
            }
            case PermAction::Remove: {
                for (auto const& perm : perms.value()) {
                    if (auto res = storage->revokePermission(param.realName, perm)) {
                        mc_utils::sendText(
                            output,
                            "\"{}\" 已从玩家 \"{}\" 中移除"_tr(PermissionStorage::toString(perm), param.realName)
                        );
                    } else {
                        mc_utils::sendText<mc_utils::Error>(output, "移除玩家权限失败: {}"_tr(res.error()));
                    }
                }
                break;
            }
            }
        });
}


} // namespace ltps