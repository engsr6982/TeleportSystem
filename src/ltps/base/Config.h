#pragma once
#include "ltps/Global.h"
#include "ltps/common/EconomySystemConfig.h"

#include <filesystem>
#include <unordered_set>


namespace ltps::inline config {

namespace v4 {
struct Config {};
} // namespace v4


namespace v5 {

using DisallowedDimensions = std::unordered_set<int>;

struct Config {
    int                 version = 12;
    EconomySystemConfig economySystem{};
    bool                telemetry{true}; // 是否发送匿名统计信息
    struct {
        struct {
            bool                 enable                 = true;
            std::string          createRequestCalculate = "random_num_range(10, 60)"; // 创建请求价格
            int                  cooldownTime           = 10;                         // 发起请求冷却时间（秒）
            int                  expirationTime         = 120;                        // 请求过期时间（秒）
            DisallowedDimensions disallowedDimensions   = {};                         // 禁用此功能的维度
        } tpa;

        struct {
            bool                 enable               = true;
            std::string          createHomeCalculate  = "random_num_range(10, 188)";
            std::string          goHomeCalculate      = "random_num_range(10, 188)";
            int                  nameLength           = 20; // 名字长度
            int                  maxHome              = 20; // 最大家园数量
            int                  cooldownTime         = 10; // 传送冷却时间（秒）
            DisallowedDimensions disallowedDimensions = {};
        } home;

        struct {
            bool                 enable               = true;
            int                  cooldownTime         = 10; // 冷却时间（秒, 对于玩家）
            std::string          goWarpCalculate      = "random_num_range(10, 60)";
            DisallowedDimensions disallowedDimensions = {};
        } warp;

        struct {
            bool                 enable               = true;
            bool                 registerBackCommand  = true; // 注册 /back 命令 (/death back的别名)
            std::string          goDeathCalculate     = "random_num_range(10, 60)";
            int                  maxDeathInfos        = 5; // 显示给玩家的死亡信息数量
            DisallowedDimensions disallowedDimensions = {};
        } death;

        struct {
            bool        enable       = true;
            int         cooldownTime = 10; // 冷却时间（秒）
            std::string calculate    = "random_num_range(40, 80)";
            struct {
                int min = -1000;
                int max = 1000;
            } randomRange;

            std::unordered_set<std::string> dangerousBlocks = {
                "minecraft:water",
                "minecraft:lava",
                "minecraft:fire",
            };

            struct {
                bool enable   = false;
                bool isCircle = true; // true: Circle  false: CenteredSquare

                struct {
                    int  x                       = 0;     // 圆心或矩形的中心点
                    int  z                       = 0;     // 圆心或矩形的中心点
                    int  radius                  = 100;   // 半径或矩形的边长
                    bool usePlayerPositionCenter = false; // 使用玩家当前的位置作为中心点
                } center;

            } restrictedAreas;

            DisallowedDimensions disallowedDimensions = {};
        } tpr;

        // [[deprecated]]
        // struct {
        //     bool        enable           = true;
        //     std::string openPRCalculate  = "random_num_range(10, 60)";
        //     std::string closePRCalculate = "random_num_range(10, 60)";
        // } pr;
    } modules;
};
} // namespace v5

using Config = v5::Config;

TPSNDAPI inline Config&               getConfig();
TPSNDAPI inline std::filesystem::path getConfigPath();
TPSAPI void                           loadConfig();
TPSAPI void                           saveConfig();

} // namespace ltps::inline config