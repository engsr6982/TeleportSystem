#include "HomeCommand.h"
#include "fmt/color.h"
#include "fmt/ranges.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/Overload.h"
#include "ll/api/event/EventBus.h"
#include "ltps/TeleportSystem.h"
#include "ltps/database/PermissionStorage.h"
#include "ltps/database/StorageManager.h"
#include "ltps/modules/home/HomeStorage.h"
#include "ltps/modules/home/event/HomeEvents.h"
#include "ltps/modules/home/gui/HomeGUI.h"
#include "ltps/modules/home/gui/HomeOperatorGUI.h"
#include "ltps/utils/McUtils.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/level/dimension/VanillaDimensions.h"
#include <numeric>


namespace ltps::home {

struct HomeListParam {
    std::string name;
};

struct HomeActionParam {
    enum class Action { Add, Remove, Go };
    Action      action;
    std::string name;
};

struct HomeUpdateParam {
    PlayerRequestEditHomeEvent::Type type;
    std::string                      name;
    std::string                      newName;
};


void HomeCommand::setup() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand("home", "TeleportSystem - Home");

    // home
    cmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getOriginType() != CommandOriginType::Player) {
            mc_utils::sendText<mc_utils::Error>(output, "This command can only be run by a player"_tr());
            return;
        }

        auto& player = *static_cast<Player*>(origin.getEntity());
        HomeGUI::sendMainMenu(player);
    });

    // home list [home]
    cmd.overload<HomeListParam>().text("list").optional("name").execute(
        [](CommandOrigin const& origin, CommandOutput& output, HomeListParam const& param) {
            if (origin.getOriginType() != CommandOriginType::Player) {
                mc_utils::sendText<mc_utils::Error>(output, "This command can only be run by a player"_tr());
                return;
            }

            auto& player     = *static_cast<Player*>(origin.getEntity());
            auto  localeCode = player.getLocaleCode();
            auto  storage    = TeleportSystem::getInstance().getStorageManager().getStorage<HomeStorage>();

            if (param.name.empty()) {
                auto homes = storage->getHomes(player.getUuid());
                std::string text = "You have {} home(s):"_trl(localeCode, homes.size());
                for (const auto& home : homes) {
                    text += fmt::format(" ,{}", home.name);
                }
                mc_utils::sendText<mc_utils::Info>(output, text);
                return;
            }

            auto home = storage->getHome(player.getUuid(), param.name);
            if (!home) {
                mc_utils::sendText<mc_utils::Error>(output, "Home not found"_trl(localeCode));
                return;
            }

            mc_utils::sendText(
                output,
                "Name: {} Pos: {},{},{} Dim: {} Created: {} Modified: {}"_trl(
                    localeCode,
                    home->name,
                    home->x,
                    home->y,
                    home->z,
                    VanillaDimensions::toString(home->dimid),
                    home->createdTime,
                    home->modifiedTime
                )
            );
        }
    );

    // home <add|remove|go> <name>
    cmd.overload<HomeActionParam>().required("action").required("name").execute(
        [](CommandOrigin const& origin, CommandOutput& output, HomeActionParam const& param) {
            if (origin.getOriginType() != CommandOriginType::Player) {
                mc_utils::sendText<mc_utils::Error>(output, "This command can only be run by a player"_tr());
                return;
            }

            auto& player = *static_cast<Player*>(origin.getEntity());

            switch (param.action) {
            case HomeActionParam::Action::Add: {
                ll::event::EventBus::getInstance().publish(PlayerRequestAddHomeEvent{player, param.name});
                break;
            }
            case HomeActionParam::Action::Remove: {
                ll::event::EventBus::getInstance().publish(PlayerRequestRemoveHomeEvent{player, param.name});
                break;
            }
            case HomeActionParam::Action::Go: {
                ll::event::EventBus::getInstance().publish(PlayerRequestGoHomeEvent{player, param.name});
                break;
            }
            }
        }
    );

    // home update <name|pos> <name> [newName]
    cmd.overload<HomeUpdateParam>().text("update").required("type").required("name").optional("newName").execute(
        [](CommandOrigin const& origin, CommandOutput& output, HomeUpdateParam const& param) {
            if (origin.getOriginType() != CommandOriginType::Player) {
                mc_utils::sendText<mc_utils::Error>(output, "This command can only be run by a player"_tr());
                return;
            }

            auto& player = *static_cast<Player*>(origin.getEntity());

            switch (param.type) {
            case PlayerRequestEditHomeEvent::Type::Name: {
                if (param.newName.empty()) {
                    mc_utils::sendText<mc_utils::Error>(output, "New name cannot be empty!"_trl(player.getLocaleCode()));
                    return;
                }
                ll::event::EventBus::getInstance().publish(
                    PlayerRequestEditHomeEvent{player, param.name, param.type, std::nullopt, param.newName}
                );
                break;
            }
            case PlayerRequestEditHomeEvent::Type::Position: {
                ll::event::EventBus::getInstance().publish(
                    PlayerRequestEditHomeEvent{player, param.name, param.type, player.getPosition(), std::nullopt}
                );
                break;
            }
            }
        }
    );


    // home mgr
    cmd.overload().text("mgr").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getOriginType() != CommandOriginType::Player) {
            mc_utils::sendText<mc_utils::Error>(output, "This command can only be run by a player"_tr());
            return;
        }

        auto& player = *static_cast<Player*>(origin.getEntity());

        auto st = TeleportSystem::getInstance().getStorageManager().getStorage<PermissionStorage>();

        if (!st->hasPermission(player.getUuid(), PermissionStorage::Permission::ManagerPanel)) {
            mc_utils::sendText<mc_utils::Error>(
                output,
                "You do not have permission to use this command"_trl(player.getLocaleCode())
            );
            return;
        }

        HomeOperatorGUI::sendMainGUI(player);
    });
}


} // namespace ltps::home