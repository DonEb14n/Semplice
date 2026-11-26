#include "platform/WindowsCodepageConverter.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace semplice::platform {

#ifdef Q_OS_WIN
namespace {

UINT windowsCodePage(CodePage page)
{
    switch (page) {
    case CodePage::Utf8: return CP_UTF8;
    case CodePage::Cp1251: return 1251;
    case CodePage::Cp936: return 936;
    case CodePage::Cp932: return 932;
    case CodePage::Cp949: return 949;
    case CodePage::Cp950: return 950;
    }
    return CP_ACP;
}

}  // namespace

std::optional<QString> WindowsCodepageConverter::decode(const QByteArray& bytes, CodePage page) const
{
    if (bytes.isEmpty())
        return QString();
    const UINT cp = windowsCodePage(page);
    // MB_ERR_INVALID_CHARS: bytes that do not belong to the code page make the call fail instead of
    // being silently replaced.
    const int size = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, bytes.constData(), static_cast<int>(bytes.size()), nullptr, 0);
    if (size <= 0)
        return std::nullopt;
    QString out(size, QChar());
    if (MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, bytes.constData(), static_cast<int>(bytes.size()),
                            reinterpret_cast<LPWSTR>(out.data()), size) != size)
        return std::nullopt;
    return out;
}

std::optional<QByteArray> WindowsCodepageConverter::encode(const QString& text, CodePage page) const
{
    if (text.isEmpty())
        return QByteArray();
    const UINT cp = windowsCodePage(page);
    const bool utf8 = page == CodePage::Utf8;
    BOOL usedDefault = FALSE;
    const DWORD flags = utf8 ? 0 : WC_NO_BEST_FIT_CHARS;
    const auto* wide = reinterpret_cast<LPCWCH>(text.utf16());
    const int size = WideCharToMultiByte(cp, flags, wide, static_cast<int>(text.size()), nullptr, 0, nullptr, utf8 ? nullptr : &usedDefault);
    if (size <= 0 || usedDefault)
        return std::nullopt;
    QByteArray out(size, '\0');
    usedDefault = FALSE;
    if (WideCharToMultiByte(cp, flags, wide, static_cast<int>(text.size()), out.data(), size, nullptr, utf8 ? nullptr : &usedDefault) != size ||
        usedDefault)
        return std::nullopt;
    return out;
}
#else
std::optional<QString> WindowsCodepageConverter::decode(const QByteArray&, CodePage) const { return std::nullopt; }
std::optional<QByteArray> WindowsCodepageConverter::encode(const QString&, CodePage) const { return std::nullopt; }
#endif

}  // namespace semplice::platform
