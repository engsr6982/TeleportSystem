#pragma once
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"


#include "ll/api/base/StdInt.h"
#include "ll/api/i18n/I18n.h"

#include "mc/world/actor/player/Player.h"

#include <string>

#include "econbridge/IEconomy.h"

class Player;
namespace mce {
class UUID;
}

namespace ltps ::economy_helper {

inline std::string getCostMessage(Player& player, llong amount, std::string const& localeCode) {
    using ll::i18n_literals::operator""_trl;

    auto&                    config = getConfig().economySystem;
    static const std::string prefix = "\n[Tip] ";

    auto& economy = TeleportSystem::getInstance().getEconomy();

    if (config.enabled) {
        llong currentMoney = economy.get(player.getUuid());
        bool  isEnough     = currentMoney >= amount;

        return "\n[Tip] 本次操作需要: {0} {1} | 当前余额: {2} | 剩余余额: {3} | {4}"_trl(
            localeCode,
            amount,
            config.economyName,
            currentMoney,
            currentMoney - amount,
            isEnough ? "余额充足"_trl(localeCode) : "余额不足"_trl(localeCode)
        );
    }
    return "\n[Tip] 经济系统未启用，本次操作不消耗 {}"_trl(localeCode, config.economyName);
}

inline void sendNotEnoughMoneyMessage(Player& player, llong amount, std::string const& localeCode) {
    auto& config = getConfig().economySystem;

    player.sendMessage(
        "§c[EconomySystem] 操作失败，需要 {0} {1}，当前余额 {2}"_trl(
            localeCode,
            amount,
            config.economyName,
            TeleportSystem::getInstance().getEconomy().get(player.getUuid())
        )
    );
}


} // namespace ltps::economy_helper