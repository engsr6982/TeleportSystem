#include "ltps/modules/tpa/gui/TpaGUI.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/form/FormBase.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"
#include "ltps/modules/tpa/TpaRequest.h"
#include "ltps/modules/tpa/event/TpaEvents.h"
#include "ltps/utils/McUtils.h"
#include "mc/world/level/Level.h"
#include <memory>


namespace ltps::tpa {


void TpaGUI::sendMainMenu(Player& player) {
    sendChooseTpaTypeMenu(player, [](Player& self, TpaRequest::Type type) { sendChooseTpaPlayerMenu(self, type); });
}

void TpaGUI::sendChooseTpaTypeMenu(Player& player, ChooseTpaTypeCallback callback) {
    auto const localeCode = player.getLocaleCode();

    ll::form::SimpleForm{"TPA Menu"_trl(localeCode), "How do you want to teleport?"_trl(localeCode)}
        .appendButton("Teleport to another player"_trl(localeCode))
        .appendButton("Ask another player to teleport here"_trl(localeCode))
        .sendTo(player, [fn = std::move(callback)](Player& self, int index, ll::form::FormCancelReason) {
            if (index == -1) {
                return;
            }

            fn(self, static_cast<TpaRequest::Type>(index));
        });
}

void TpaGUI::sendChooseTpaPlayerMenu(Player& player, TpaRequest::Type type) {
    auto level = ll::service::getLevel();
    if (!level) {
        return;
    }

    auto const localeCode = player.getLocaleCode();

    auto fm = ll::form::SimpleForm{"TPA - Send a request"_trl(localeCode), "Choose a player"_trl(localeCode)};

    level->forEachPlayer([&fm, level, type](Player& target) {
        auto targetUuid = target.getUuid();

        fm.appendButton(target.getRealName(), [level, targetUuid, type](Player& self) {
            auto receiver = level->getPlayer(targetUuid);
            if (!receiver) {
                mc_utils::sendText<mc_utils::Error>(self, "That player is offline"_trl(self.getLocaleCode()));
                return;
            }

            ll::event::EventBus::getInstance().publish(
                CreateTpaRequestEvent{
                    self,
                    *receiver,
                    type,
                    [](std::shared_ptr<TpaRequest> request) {
                        if (request) {
                            request->sendFormToReceiver();
                        }
                    }
                }
            );
        });
        return true;
    });

    fm.sendTo(player);
}


} // namespace ltps::tpa