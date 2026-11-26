#include "core/tags/EncodingFixer.h"

#include <QStringDecoder>

#include <algorithm>

namespace semplice {

namespace {

// ---- character classes ---------------------------------------------------------------------------------

bool isCyrillic(char32_t c) { return (c >= 0x0400 && c <= 0x052F); }
bool isHan(char32_t c) { return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0xF900 && c <= 0xFAFF); }
bool isKana(char32_t c) { return c >= 0x3040 && c <= 0x30FF; }  // hiragana + katakana (not half-width)
bool isHangul(char32_t c) { return (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F); }
bool isHalfwidthKatakana(char32_t c) { return c >= 0xFF61 && c <= 0xFF9F; }
bool isAsciiLetter(char32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

struct Counts {
    int cyrillic = 0, han = 0, kana = 0, hangul = 0, asciiLetters = 0, otherLetters = 0, bad = 0;
    bool cyrillicMixesWithLatinInAWord = false;
};

Counts classify(const QString& text)
{
    Counts n;
    bool wordHasCyr = false;
    bool wordHasLatin = false;
    auto endWord = [&] {
        if (wordHasCyr && wordHasLatin)
            n.cyrillicMixesWithLatinInAWord = true;
        wordHasCyr = wordHasLatin = false;
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        char32_t c = text.at(i).unicode();
        if (QChar::isHighSurrogate(c)) {
            if (i + 1 >= text.size() || !text.at(i + 1).isLowSurrogate()) {
                ++n.bad;
                continue;
            }
            c = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            ++i;
        } else if (QChar::isLowSurrogate(c)) {
            ++n.bad;
            continue;
        }
        if (c == 0xFFFD || (c < 0x20 && c != '\t') || (c >= 0x7F && c < 0xA0) || (c >= 0xE000 && c <= 0xF8FF)) {
            ++n.bad;
            endWord();
            continue;
        }
        if (isCyrillic(c)) {
            ++n.cyrillic;
            wordHasCyr = true;
        } else if (isHan(c)) {
            ++n.han;
        } else if (isKana(c)) {
            ++n.kana;
        } else if (isHangul(c)) {
            ++n.hangul;
        } else if (isAsciiLetter(c)) {
            ++n.asciiLetters;
            wordHasLatin = true;
        } else if (isHalfwidthKatakana(c)) {
            ++n.otherLetters;  // what Shift-JIS makes of Chinese bytes: never accept
        } else if (c >= 0x80 && QChar::isLetter(c)) {
            ++n.otherLetters;
            endWord();
        } else {
            endWord();  // digits, spaces, punctuation, symbols: neutral
        }
    }
    endWord();
    return n;
}

// How "broken" does the Latin-1 reading look? Characters >= U+0080 against ASCII letters: genuine
// mojibake is mostly high bytes (Big5/Shift-JIS leave ASCII-range trail bytes in between), real
// Western text is mostly ASCII with a few accents.
bool looksBroken(const QString& text)
{
    int asciiLetters = 0;
    int nonAscii = 0;
    for (const QChar c : text) {
        if (c.unicode() >= 0x80)
            ++nonAscii;
        else if (c.isLetter())
            ++asciiLetters;
    }
    return nonAscii >= 2 && nonAscii * 10 >= (nonAscii + asciiLetters) * 6;
}

// Structural check of the byte pairs for the double-byte pages: only the common subsets
// (GB2312, KS X 1001, Big5, JIS X 0208) are accepted, which keeps rare/odd byte soups out.
bool structurallyValid(const QByteArray& bytes, CodePage page)
{
    const qsizetype n = bytes.size();
    for (qsizetype i = 0; i < n; ++i) {
        const quint8 b = static_cast<quint8>(bytes[i]);
        if (b < 0x80)
            continue;
        if (page == CodePage::Cp1251)
            continue;  // single-byte page
        if (page == CodePage::Cp932 && b >= 0xA1 && b <= 0xDF)
            continue;  // half-width katakana: one byte (the plausibility check rejects them later)
        if (i + 1 >= n)
            return false;  // truncated pair (ID3v1 cuts at 30 bytes)
        const quint8 t = static_cast<quint8>(bytes[i + 1]);
        bool ok = false;
        switch (page) {
        case CodePage::Cp936:  // GB2312
            ok = b >= 0xA1 && b <= 0xF7 && t >= 0xA1 && t <= 0xFE;
            break;
        case CodePage::Cp949:  // KS X 1001
            ok = b >= 0xA1 && b <= 0xFD && t >= 0xA1 && t <= 0xFE;
            break;
        case CodePage::Cp950:  // Big5
            ok = b >= 0xA1 && b <= 0xF9 && ((t >= 0x40 && t <= 0x7E) || (t >= 0xA1 && t <= 0xFE));
            break;
        case CodePage::Cp932:  // Shift-JIS (JIS X 0208 pairs)
            ok = ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) && ((t >= 0x40 && t <= 0x7E) || (t >= 0x80 && t <= 0xFC));
            break;
        default:
            ok = true;
            break;
        }
        if (!ok)
            return false;
        ++i;
    }
    return true;
}

// Russian mojibake uses (almost) only the letter bytes C0-FF; Chinese/Korean byte pairs spread over
// A1-FE. Without this check any high-byte string would "decode" to some Cyrillic gibberish.
bool mostlyCyrillicLetterBytes(const QByteArray& bytes)
{
    int high = 0;
    int letters = 0;
    for (const char c : bytes) {
        const quint8 b = static_cast<quint8>(c);
        if (b < 0x80)
            continue;
        ++high;
        const bool isLetterByte = b >= 0xC0 || b == 0xA8 || b == 0xB8 || b == 0xAA || b == 0xBA || b == 0xAF || b == 0xBF ||
                                  b == 0xB2 || b == 0xB3 || b == 0xA5 || b == 0xB4 || b == 0xA1 || b == 0xA2;
        const bool isTypography = b == 0x91 || b == 0x92 || b == 0x93 || b == 0x94 || b == 0x96 || b == 0x97 || b == 0xAB ||
                                  b == 0xBB || b == 0xA0 || b == 0xB9;  // quotes, dashes, guillemets, no. sign
        if (isLetterByte)
            ++letters;
        else if (!isTypography)
            return false;
    }
    return high > 0 && letters * 10 >= high * 7;  // a clear majority are letters
}

std::optional<QString> decodeUtf8Strict(const QByteArray& bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString text = decoder.decode(bytes);
    if (decoder.hasError() || text.contains(QChar::ReplacementCharacter))
        return std::nullopt;
    return text;
}

// 0 = not plausible.
double plausibility(const QString& decoded, const QString& original, CodePage page)
{
    if (decoded.isEmpty() || decoded == original)
        return 0;
    const Counts n = classify(decoded);
    if (n.bad > 0 || (n.otherLetters > 0 && page != CodePage::Utf8))
        return 0;

    switch (page) {
    case CodePage::Utf8: {
        // Valid UTF-8 inside a "Latin-1" field is a certain sign of double encoding. Just refuse controls.
        return 2.0;
    }
    case CodePage::Cp1251:
        if (n.cyrillic < 2 || n.cyrillicMixesWithLatinInAWord || n.han + n.kana + n.hangul > 0)
            return 0;
        return 1.0;
    case CodePage::Cp936:
    case CodePage::Cp950:
        if (n.han < 2 || n.hangul > 0 || n.cyrillic > 0)
            return 0;
        return 1.0 + (n.kana == 0 ? 0.0 : -0.5);
    case CodePage::Cp932:
        if (n.han + n.kana < 2 || n.hangul > 0 || n.cyrillic > 0)
            return 0;
        return n.kana > 0 ? 1.5 : 1.0;  // kana is the clearest sign of Japanese
    case CodePage::Cp949:
        if (n.hangul + n.han < 2 || n.kana > 0 || n.cyrillic > 0)
            return 0;
        return n.hangul > 0 ? 1.0 : 0.9;
    }
    return 0;
}

}  // namespace

QList<CodePage> EncodingFixer::defaultOrder()
{
    return {CodePage::Utf8, CodePage::Cp1251, CodePage::Cp936, CodePage::Cp932, CodePage::Cp949, CodePage::Cp950};
}

EncodingFixer::EncodingFixer(const ICodepageConverter& converter, QList<CodePage> order)
    : m_converter(converter), m_order(std::move(order))
{
}

FixResult EncodingFixer::fixText(const QString& text) const
{
    FixResult result;
    result.text = text;

    // Only text that could really be bytes read as Latin-1 and that is not plain ASCII.
    bool nonAscii = false;
    for (const QChar c : text) {
        if (c.unicode() >= 0x100)
            return result;
        nonAscii = nonAscii || c.unicode() >= 0x80;
    }
    if (!nonAscii)
        return result;

    const QByteArray bytes = text.toLatin1();
    const bool broken = looksBroken(text);

    QList<FixCandidate> candidates;
    for (int rank = 0; rank < m_order.size(); ++rank) {
        const CodePage page = m_order.at(rank);
        std::optional<QString> decoded;
        if (page == CodePage::Utf8) {
            decoded = decodeUtf8Strict(bytes);
        } else {
            if (!broken || !structurallyValid(bytes, page) || (page == CodePage::Cp1251 && !mostlyCyrillicLetterBytes(bytes)))
                continue;
            decoded = m_converter.decode(bytes, page);
        }
        if (!decoded)
            continue;
        const double score = plausibility(*decoded, text, page);
        if (score <= 0)
            continue;
        candidates.append({page, *decoded, score - rank * 1e-3});  // earlier in `order` wins ties
    }
    if (candidates.isEmpty())
        return result;

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const FixCandidate& a, const FixCandidate& b) { return a.score > b.score; });
    result.text = candidates.first().text;
    result.changed = true;
    result.page = candidates.first().page;
    result.alternatives = candidates.mid(1);
    return result;
}

EncodingFixer::Report EncodingFixer::fix(TrackTags& tags) const
{
    Report report;
    auto repairOne = [&](QString& value, const QString& field) {
        if (!tags.legacy8bit.contains(field))
            return false;
        const FixResult r = fixText(value);
        if (!r.changed)
            return false;
        value = r.text;
        return true;
    };
    auto repairList = [&](QStringList& values, const QString& field) {
        bool any = false;
        for (QString& v : values)
            any = repairOne(v, field) || any;
        return any;
    };
    if (repairOne(tags.title, tagfield::Title))
        report.repairedFields << tagfield::Title;
    if (repairList(tags.artists, tagfield::Artists))
        report.repairedFields << tagfield::Artists;
    if (repairOne(tags.album, tagfield::Album))
        report.repairedFields << tagfield::Album;
    if (repairList(tags.albumArtists, tagfield::AlbumArtists))
        report.repairedFields << tagfield::AlbumArtists;
    if (repairList(tags.genres, tagfield::Genres))
        report.repairedFields << tagfield::Genres;
    return report;
}

}  // namespace semplice
