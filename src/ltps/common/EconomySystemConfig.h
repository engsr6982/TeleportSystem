#pragma once
#include <string>

namespace ltps {


enum class EconomyKit { LegacyMoney, ScoreBoard };

struct EconomySystemConfig {
    bool        enabled        = false;
    EconomyKit  kit            = EconomyKit::LegacyMoney;
    std::string scoreboardName = "Scoreboard";
    std::string economyName    = "Coin";
};

} // namespace ltps