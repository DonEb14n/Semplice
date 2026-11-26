#pragma once

#include "core/tags/EncodingFixer.h"

namespace semplice::platform {

// ICodepageConverter on top of MultiByteToWideChar / WideCharToMultiByte (SPEC 6.3). On other systems
// every conversion reports "not possible".
class WindowsCodepageConverter final : public ICodepageConverter {
public:
    std::optional<QString> decode(const QByteArray& bytes, CodePage page) const override;
    std::optional<QByteArray> encode(const QString& text, CodePage page) const override;
};

}  // namespace semplice::platform
