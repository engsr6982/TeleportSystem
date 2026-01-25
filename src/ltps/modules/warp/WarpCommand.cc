#include "WarpCommand.h"

#include "WarpStorage.h"
#include "event/WarpEvents.h"
#include "gui/WarpGUI.h"
#include "ll/api/event/EventBus.h"
#include "ltps/TeleportSystem.h"
#include "ltps/database/PermissionStorage.h"
#include "ltps/modules/warp/gui/WarpOperatorGUI.h"
#include "ltps/utils/McUtils.h"
#include <ll/api/command/CommandHandle.h>
#include <mc/world/level/dimension/VanillaDimensions.h>


namespace ltps::warp {

struct WarpListParam {
    std::string name;
};

struct WarpActionParam {
    enum class Action { Add, Remove, Go };
    Action      action;
    std::string name;
};


void WarpCommand::setup() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand("warp", "TeleportSystem - Warp");

    // warp
    cmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getOriginType() != CommandOriginType::Player) {
            mc_utils::sendText<mc_utils::Error>(output, "此命令只能由玩家执行"_tr());
            return;
        }
        auto& player = *static_cast<Player*>(origin.getEntity());
        WarpGUI::sendMainMenu(player);
    });

    // warp list [name]
    cmd.overload<WarpListParam>().text("list").optional("name").execute(
        [](CommandOrigin const& origin, CommandOutput& output, WarpListParam const& param) {
            if (origin.getOriginType() != CommandOriginType::Player) {
                mc_utils::sendText<mc_utils::Error>(output, "此命令只能由玩家执行"_tr());
                return;
            }

            auto& player     = *static_cast<Player*>(origin.getEntity());
            auto  localeCode = player.getLocaleCode();
            auto  storage    = TeleportSystem::getInstance().getStorageManager().getStorage<WarpStorage>();

            if (param.name.empty()) {
                auto        warps = storage->getWarps(16);
                std::string text  = "当前服务器共有 {} 个公共传送点:"_trl(localeCode, warps.size());
                for (const auto& warp : warps) {
                    text += fmt::format(" ,{}", warp.name);
                }
                mc_utils::sendText<mc_utils::Info>(output, text);
                return;
            }

            auto warp = storage->getWarp(param.name);
            if (!warp) {
                mc_utils::sendText<mc_utils::Error>(output, "未找到该公共传送点"_trl(localeCode));
                return;
            }

            mc_utils::sendText(
                output,
                "名称: {} 坐标：{},{},{} 维度: {} 创建时间: {} 修改时间: {}"_trl(
                    localeCode,
                    warp->name,
                    warp->x,
                    warp->y,
                    warp->z,
                    VanillaDimensions::toString(warp->dimid),
                    warp->createdTime,
                    warp->modifiedTime
                )
            );
        }
    );

    // warp <add|remove|go> <name>
    cmd.overload<WarpActionParam>().required("action").required("name").execute(
        [](CommandOrigin const& origin, CommandOutput& output, WarpActionParam const& param) {
            if (origin.getOriginType() != CommandOriginType::Player) {
                mc_utils::sendText<mc_utils::Error>(output, "此命令只能由玩家执行"_tr());
                return;
            }
            auto& player = *static_cast<Player*>(origin.getEntity());

            switch (param.action) {
            case WarpActionParam::Action::Add: {
                ll::event::EventBus::getInstance().publish(PlayerRequestAddWarpEvent{player, param.name});
                break;
            }
            case WarpActionParam::Action::Remove: {
                ll::event::EventBus::getInstance().publish(PlayerRequestRemoveWarpEvent{player, param.name});
                break;
            }
            case WarpActionParam::Action::Go: {
                ll::event::EventBus::getInstance().publish(PlayerRequestGoWarpEvent{player, param.name});
                break;
            }
            }
        }
    );

    // warp mgr
    cmd.overload().text("mgr").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getOriginType() != CommandOriginType::Player) {
            mc_utils::sendText<mc_utils::Error>(output, "此命令只能由玩家执行"_tr());
            return;
        }

        auto& player = *static_cast<Player*>(origin.getEntity());

        auto st = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();

        if (!st->hasPermission(player.getRealName(), PermissionStorage::Permission::ManagerPanel)) {
            mc_utils::sendText<mc_utils::Error>(output, "你没有权限使用此命令"_trl(player.getLocaleCode()));
            return;
        }

        WarpOperatorGUI::sendMainGUI(player);
    });
}


} // namespace ltps::warp