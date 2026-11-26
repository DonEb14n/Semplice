#pragma once

#include "core/tags/EncodingFixer.h"

#include <QHash>
#include <QMap>

namespace semplice::testing {

// Deterministic stand-in for the Windows code pages. CP1251 is the real table (Russian/Ukrainian
// letters, the common typography). For the double-byte pages a test registers its own small table:
// the codes are invented but follow the structural rules of the real page, which is all the fixer
// looks at. The REAL tables are checked by the Windows-only test of WindowsCodepageConverter.
class FakeCodepageConverter final : public ICodepageConverter {
public:
    // Assigns every character of `chars` a two-byte code: lead = leadStart + k / perLead,
    // trail = trailStart + k % perLead.
    void addTable(CodePage page, const QString& chars, quint8 leadStart, quint8 trailStart, int perLead = 94)
    {
        auto& toChar = m_decode[page];
        auto& toBytes = m_encode[page];
        for (int k = 0; k < chars.size(); ++k) {
            const QByteArray code = QByteArray(1, static_cast<char>(leadStart + k / perLead)) +
                                    QByteArray(1, static_cast<char>(trailStart + k % perLead));
            toChar.insert(code, chars.at(k));
            toBytes.insert(chars.at(k), code);
        }
    }

    std::optional<QString> decode(const QByteArray& bytes, CodePage page) const override
    {
        if (page == CodePage::Utf8) {
            QStringDecoder d(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
            const QString s = d.decode(bytes);
            return d.hasError() ? std::nullopt : std::optional<QString>(s);
        }
        QString out;
        for (qsizetype i = 0; i < bytes.size(); ++i) {
            const quint8 b = static_cast<quint8>(bytes[i]);
            if (b < 0x80) {
                out.append(QChar(b));
                continue;
            }
            if (page == CodePage::Cp1251) {
                const QChar c = cp1251ToChar(b);
                if (c.isNull())
                    return std::nullopt;
                out.append(c);
                continue;
            }
            if (i + 1 >= bytes.size())
                return std::nullopt;
            const auto table = m_decode.constFind(page);
            if (table == m_decode.constEnd())
                return std::nullopt;
            const auto hit = table->constFind(bytes.mid(i, 2));
            if (hit == table->constEnd())
                return std::nullopt;
            out.append(hit.value());
            ++i;
        }
        return out;
    }

    std::optional<QByteArray> encode(const QString& text, CodePage page) const override
    {
        if (page == CodePage::Utf8)
            return text.toUtf8();
        QByteArray out;
        for (const QChar c : text) {
            if (c.unicode() < 0x80) {
                out.append(static_cast<char>(c.unicode()));
            } else if (page == CodePage::Cp1251) {
                const int b = charToCp1251(c);
                if (b < 0)
                    return std::nullopt;
                out.append(static_cast<char>(b));
            } else {
                const auto table = m_encode.constFind(page);
                if (table == m_encode.constEnd() || !table->contains(c))
                    return std::nullopt;
                out.append(table->value(c));
            }
        }
        return out;
    }

    // The text as an ISO-8859-1 field would show it: every byte becomes one character.
    QString mojibake(const QString& good, CodePage page) const
    {
        const auto bytes = encode(good, page);
        return bytes ? QString::fromLatin1(*bytes) : QString();
    }

private:
    static QChar cp1251ToChar(quint8 b)
    {
        if (b >= 0xC0)
            return QChar(static_cast<ushort>(0x0410 + (b - 0xC0)));
        switch (b) {
        case 0xA8: return QChar(0x0401);
        case 0xB8: return QChar(0x0451);
        case 0xAA: return QChar(0x0404);
        case 0xBA: return QChar(0x0454);
        case 0xAF: return QChar(0x0407);
        case 0xBF: return QChar(0x0457);
        case 0xB2: return QChar(0x0406);
        case 0xB3: return QChar(0x0456);
        case 0xA5: return QChar(0x0490);
        case 0xB4: return QChar(0x0491);
        case 0xA1: return QChar(0x040E);
        case 0xA2: return QChar(0x045E);
        case 0x91: return QChar(0x2018);
        case 0x92: return QChar(0x2019);
        case 0x93: return QChar(0x201C);
        case 0x94: return QChar(0x201D);
        case 0x96: return QChar(0x2013);
        case 0x97: return QChar(0x2014);
        case 0xA0: return QChar(0x00A0);
        case 0xAB: return QChar(0x00AB);
        case 0xBB: return QChar(0x00BB);
        case 0xB9: return QChar(0x2116);
        default: return QChar();
        }
    }
    static int charToCp1251(QChar c)
    {
        for (int b = 0x80; b <= 0xFF; ++b) {
            if (cp1251ToChar(static_cast<quint8>(b)) == c)
                return b;
        }
        return -1;
    }

    QMap<CodePage, QHash<QByteArray, QChar>> m_decode;
    QMap<CodePage, QHash<QChar, QByteArray>> m_encode;
};

}  // namespace semplice::testing
