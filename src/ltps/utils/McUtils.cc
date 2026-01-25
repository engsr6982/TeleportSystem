#include "ltps/utils/McUtils.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/utility/MCRESULT.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/server/ServerLevel.h"
#include "mc/server/commands/CommandBlockNameResult.h"
#include "mc/server/commands/CommandContext.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandVersion.h"
#include "mc/server/commands/MinecraftCommands.h"
#include "mc/server/commands/PlayerCommandOrigin.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/Minecraft.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/chunk/ChunkSource.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionHeightRange.h"
#include <algorithm>
#include <ll/api/service/Bedrock.h>
#include <ll/api/service/ServerInfo.h>
#include <ll/api/service/Service.h>
#include <ll/api/service/ServiceManager.h>
#include <mc/deps/core/utility/optional_ref.h>
#include <mc/server/commands/Command.h>
#include <mc/server/commands/CommandContext.h>
#include <mc/server/commands/MinecraftCommands.h>
#include <mc/server/commands/PlayerCommandOrigin.h>
#include <mc/world/Minecraft.h>
#include <mc/world/actor/player/Player.h>
#include <memory>
#include <string>


namespace ltps::mc_utils {

BlockPos face2Pos(BlockPos const& sour, uchar face) {
    BlockPos dest = sour;
    switch (face) {
    case 0:
        --dest.y; // 下
        break;
    case 1:
        ++dest.y; // 上
        break;
    case 2:
        --dest.z; // 北
        break;
    case 3:
        ++dest.z; // 南
        break;
    case 4:
        --dest.x; // 西
        break;
    case 5:
        ++dest.x; // 东
        break;
    default:
        // Unknown face
        break;
    }
    return dest;
}

DimensionHeightRange& getDimensionHeight(Dimension& dim) { return dim.mHeightRange.get(); }

bool isSneaking(Player& player) {
    return SynchedActorDataAccess::getActorFlag(player.getEntityContext(), ActorFlags::Sneaking);
}

Vec2 getRotation(Actor& actor) { return actor.mBuiltInComponents->mActorRotationComponent->mRotationDegree; }

bool canDestroyBlock(ItemStackBase const& item, Block const& block) {
    auto legacy = &block.getBlockType();
    return std::find(item.mCanDestroy.begin(), item.mCanDestroy.end(), legacy) != item.mCanDestroy.end();
}

bool canDestroySpecial(ItemStackBase const& item, Block const& block) {
    auto it = item.mItem.get();
    if (!it) {
        return false;
    }
    return it->canDestroySpecial(block);
}


} // namespace ltps::mc_utils