#include "TprCommand.h"
#include "events/TprEvents.h"
#include "ltps/utils/McUtils.h"
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/event/EventBus.h>

namespace ltps::tpr {


void TprCommand::setup() {
    ll::command::CommandRegistrar::getInstance(false)
        .getOrCreateCommand("tpr", "TeleportSystem - Tpr")
        .overload()
        .execute([](CommandOrigin const& origin, CommandOutput& output) {
            if (origin.getOriginType() != CommandOriginType::Player) {
                mc_utils::sendText<mc_utils::Error>(output, "此命令只能由玩家执行"_tr());
                return;
            }
            auto& player = *static_cast<Player*>(origin.getEntity());
            ll::event::EventBus::getInstance().publish(PlayerRequestTprEvent{player});
        });
}


} // namespace ltps::tpr