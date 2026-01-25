#pragma once
#include "fmt/core.h"
#include "ll/api/base/StdInt.h"
#include "ltps/Global.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include <string>
#include <string_view>
#include <unordered_map>


class Vec2;
class Block;
class Actor;
class Player;
class BlockPos;
class ChunkPos;
class Dimension;
class ChunkSource;
class BlockSource;
class ItemStackBase;
class DimensionHeightRange;

namespace ltps::mc_utils {

TPSNDAPI BlockPos face2Pos(BlockPos const& sour, uchar face);

TPSNDAPI DimensionHeightRange& getDimensionHeight(Dimension& dim);

TPSNDAPI bool isSneaking(Player& player);

TPSNDAPI Vec2 getRotation(Actor& actor);

TPSNDAPI bool canDestroyBlock(ItemStackBase const& item, Block const& block);

TPSNDAPI bool canDestroySpecial(ItemStackBase const& item, Block const& block);


// Player::sendMessage、CommandOutput::success/error wrapper
// Template function sendText, usage: sendText() or sendText<LogLevel::Error>().
enum MessageLevel {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
    Fatal = 4,
};

namespace internals {
inline std::unordered_map<MessageLevel, std::string> MessageColorMapping{
    {MessageLevel::Debug, "§7"},
    { MessageLevel::Info, "§r"},
    { MessageLevel::Warn, "§e"},
    {MessageLevel::Error, "§c"},
    {MessageLevel::Fatal, "§4"},
};

inline auto formatWithModNameAndLevel(MessageLevel level, std::string_view fmt, auto&&... args) {
#ifndef MOD_NAME
#define MOD_NAME "Unknown"
#endif
    auto content = fmt::vformat(fmt, fmt::make_format_args(args...));
    return fmt::format("{}[{}]: {}", MessageColorMapping[level], MOD_NAME, content);
}
} // namespace internals


template <MessageLevel level = Info, typename... Args>
inline void sendText(Player& player, std::string_view fmt, Args&&... args) {
    player.sendMessage(internals::formatWithModNameAndLevel(level, fmt, args...));
}

template <MessageLevel level = Info, typename... Args>
inline void sendText(CommandOutput& output, std::string_view fmt, Args&&... args) {
    if constexpr (level == Error || level == Fatal) {
        output.error(internals::formatWithModNameAndLevel(level, fmt, args...));
    } else {
        output.success(internals::formatWithModNameAndLevel(level, fmt, args...));
    }
}

} // namespace ltps::mc_utils
