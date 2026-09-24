#include "DeathCommand.h"

#include "DeathStorage.h"
#include "event/DeathEvents.h"
#include "gui/DeathGUI.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/utils/McUtils.h"

#include <ll/api/command/CommandHandle.h>
#include <ll/api/event/EventBus.h>

namespace ltps ::death {

struct BackParam {
    int index = 0;
};


void DeathCommand::setup() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand("death", "TeleportSystem - Death");

    // death
    cmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getOriginType() != CommandOriginType::Player) {
            mc_utils::sendText<mc_utils::Error>(output, "This command can only be run by a player"_tr());
            return;
        }
        auto& player = *static_cast<Player*>(origin.getEntity());
        DeathGUI::sendMainMenu(player);
    });

    // death list
    cmd.overload().text("list").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getOriginType() != CommandOriginType::Player) {
            mc_utils::sendText<mc_utils::Error>(output, "This command can only be run by a player"_tr());
            return;
        }
        auto& player     = *static_cast<Player*>(origin.getEntity());
        auto  localeCode = player.getLocaleCode();

        auto deaths =
            TeleportSystem::getInstance().getStorageManager().getStorage<DeathStorage>()->getDeathInfos(player.getUuid());
        if (deaths.empty()) {
            mc_utils::sendText<mc_utils::Error>(output, "You have no death records"_trl(localeCode));
            return;
        }

        mc_utils::sendText(output, "Your recent death records:"_trl(localeCode));
        mc_utils::sendText(output, " * {}"_tr(deaths.front().toString()));

        bool skipFirst = false;
        for (auto const& death : deaths) {
            if (!skipFirst) {
                skipFirst = true;
                continue;
            }
            mc_utils::sendText(output, "   {}"_tr(death.toString()));
        }
        mc_utils::sendText(output, "{} death record(s) in total"_trl(localeCode, deaths.size()));
    });

    // death back [index]
    cmd.overload<BackParam>().text("back").optional("index").execute(
        [](CommandOrigin const& origin, CommandOutput& output, BackParam const& param) {
            if (origin.getOriginType() != CommandOriginType::Player) {
                mc_utils::sendText<mc_utils::Error>(output, "This command can only be run by a player"_tr());
                return;
            }
            auto& player = *static_cast<Player*>(origin.getEntity());
            ll::event::EventBus::getInstance().publish(PlayerRequestBackDeathPointEvent{player, param.index});
        }
    );

    // back (别名)
    if (getConfig().modules.death.registerBackCommand) {
        ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("back", "TeleportSystem - Death")
            .overload()
            .execute([](CommandOrigin const& origin, CommandOutput& output) {
                if (origin.getOriginType() != CommandOriginType::Player) {
                    mc_utils::sendText<mc_utils::Error>(output, "This command can only be run by a player"_tr());
                    return;
                }
                auto& player = *static_cast<Player*>(origin.getEntity());
                ll::event::EventBus::getInstance().publish(PlayerRequestBackDeathPointEvent{player});
            });
    }
}


} // namespace ltps::death