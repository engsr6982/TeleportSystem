#include "ltps/modules/tpa/TpaCommand.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/Overload.h"
#include "ll/api/event/EventBus.h"
#include "ltps/modules/tpa/TpaRequest.h"
#include "ltps/modules/tpa/event/TpaEvents.h"
#include "ltps/modules/tpa/gui/TpaGUI.h"
#include "ltps/utils/McUtils.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/world/actor/player/Player.h"


namespace ltps::tpa {

struct IAcceptDenyParam {
    using Type = PlayerExecuteTpaCommandEvent::Action;
    Type type;
};

struct ICreateTpaRequestParam {
    TpaRequest::Type        type;
    CommandSelector<Player> target;
};

void TpaCommand::setup() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand("tpa", "TeleportSystem - Tpa");

    // tpa
    cmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (origin.getOriginType() != CommandOriginType::Player) {
            mc_utils::sendText<mc_utils::Error>(output, "此命令只能由玩家执行"_tr());
            return;
        }

        auto& player = *static_cast<Player*>(origin.getEntity());
        TpaGUI::sendMainMenu(player);
    });

    // tpa <accept|deny|cancel>
    cmd.overload<IAcceptDenyParam>().required("type").execute(
        [](CommandOrigin const& origin, CommandOutput& output, IAcceptDenyParam const& param) {
            if (origin.getOriginType() != CommandOriginType::Player) {
                mc_utils::sendText<mc_utils::Error>(output, "此命令只能由玩家执行"_tr());
                return;
            }
            auto& receiver = *static_cast<Player*>(origin.getEntity());
            ll::event::EventBus::getInstance().publish(PlayerExecuteTpaCommandEvent{receiver, param.type});
        }
    );

    // tpa <to|here> <player: target>
    cmd.overload<ICreateTpaRequestParam>().required("type").required("target").execute(
        [](CommandOrigin const& origin, CommandOutput& output, ICreateTpaRequestParam const& param) {
            if (origin.getOriginType() != CommandOriginType::Player) {
                mc_utils::sendText<mc_utils::Error>(output, "此命令只能由玩家执行"_tr());
                return;
            }

            auto& player     = *static_cast<Player*>(origin.getEntity());
            auto  localeCode = player.getLocaleCode();

            auto targets = param.target.results(origin);
            if (targets.empty()) {
                mc_utils::sendText<mc_utils::Error>(output, "找不到目标玩家"_trl(localeCode));
                return;
            }

            if (targets.size() > 1) {
                mc_utils::sendText<mc_utils::Error>(output, "目标玩家过多"_trl(localeCode));
                return;
            }

            // clang-format off
            auto& tar = *(*targets.data)[0];
            ll::event::EventBus::getInstance().publish(CreateTpaRequestEvent{
                player,
                tar,
                param.type,
                [](std::shared_ptr<TpaRequest> request) {
                    if (request) {
                        request->sendFormToReceiver();
                    }
                }
            });
            // clang-format on
        }
    );
}


} // namespace ltps::tpa