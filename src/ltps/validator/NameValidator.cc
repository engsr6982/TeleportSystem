#include "ltps/validator/NameValidator.h"
#include "ltps/utils/StringUtils.h"


namespace ltps {

class NameValidator::Impl {
public:
    ValidatorOptions options;

    explicit Impl(ValidatorOptions opts) : options(opts) {}
};

NameValidator::NameValidator(ValidatorOptions options) : mImpl(std::make_unique<Impl>(options)) {}

NameValidator::~NameValidator() = default;

NameValidation NameValidator::validate(std::string_view name) const {
    if (name.empty()) {
        return mImpl->options.allowEmpty ? NameValidation::Ok : NameValidation::Empty;
    }
    if (mImpl->options.maxLength != 0 && string_utils::length(std::string{name}) > mImpl->options.maxLength) {
        return NameValidation::TooLong;
    }
    if (!mImpl->options.forbiddenChars.empty()
        && name.find_first_of(mImpl->options.forbiddenChars) != std::string_view::npos) {
        return NameValidation::IllegalChar;
    }
    return NameValidation::Ok;
}


} // namespace ltps