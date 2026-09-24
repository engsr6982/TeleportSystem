#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ll::data {
class KeyValueDB;
}

namespace ltps {

// key 段转义: 保证 `\` 与 `:` 不破坏分段且映射单射
namespace key_utils {

[[nodiscard]] std::string escapeSegment(std::string_view segment);

} // namespace key_utils


namespace db_utils {

/**
 * @brief 遍历以 prefix 开头的所有键值对
 * @note key/value 视图仅在回调期间有效, 需要保留请立即拷贝;
 *       当前实现为全库扫描 + 前缀过滤 (LL 未暴露 Seek), 仅用于低频路径
 */
void forEachPrefix(
    ll::data::KeyValueDB const&                                       db,
    std::string_view                                                  prefix,
    std::function<void(std::string_view key, std::string_view value)> const& fn
);

// 索引记录: JSON 字符串数组
[[nodiscard]] std::string              toJsonArray(std::vector<std::string> const& items);
[[nodiscard]] std::vector<std::string> fromJsonArray(std::string_view raw);

// 取 key 中第 n 段 (以 ':' 分隔, 段内已转义), 越界返回空
[[nodiscard]] std::string_view segmentOf(std::string_view key, std::size_t index, std::string_view prefix);

} // namespace db_utils


} // namespace ltps