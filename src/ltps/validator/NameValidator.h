#pragma once
#include "ltps/Global.h"
#include <cstddef>
#include <memory>
#include <string_view>


namespace ltps {

enum class NameValidation {
    Ok,          // 合法
    Empty,       // 空名称
    TooLong,     // 超出长度上限
    IllegalChar, // 含禁用字符
};

struct ValidatorOptions {
    std::size_t      maxLength{0};              // 长度上限 (UTF-8 码点), 0 = 不限
    bool             allowEmpty{false};         // 是否允许空名称
    std::string_view forbiddenChars{"\x00", 1}; // 禁用字符集合, 默认仅 NUL
};


class NameValidator final {
public:
    TPS_DISALLOW_COPY_AND_MOVE(NameValidator);

    TPSAPI explicit NameValidator(ValidatorOptions options = {});
    TPSAPI ~NameValidator();

    TPSNDAPI NameValidation validate(std::string_view name) const;

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};


} // namespace ltps