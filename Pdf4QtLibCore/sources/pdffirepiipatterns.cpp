// MIT License
//
// Copyright (c) 2026 PDF Fire contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Ported from PdfCraft crates/redact/src/patterns.rs (MIT OR Apache-2.0).
// The matchers are translated one to one; the text is a sequence of Unicode
// code points (as in Rust's &[char]), the results are mapped back to UTF-16.

#include "pdffirepiipatterns.h"

#include <QCoreApplication>
#include <QStringList>

#include <array>
#include <optional>

#include "pdfdbgheap.h"

namespace pdf
{

namespace
{

using Text = std::u32string;
using Index = std::ptrdiff_t;
using Length = std::optional<Index>;

/// The character at the index, or 0, if the index is out of the text
char32_t at(const Text& s, Index i)
{
    return (i >= 0 && i < Index(s.size())) ? s[size_t(i)] : U'\0';
}

bool isDigit(char32_t c)
{
    return c >= U'0' && c <= U'9';
}

bool digit(const Text& s, Index i)
{
    return isDigit(at(s, i));
}

bool word(const Text& s, Index i)
{
    const char32_t c = at(s, i);
    return c != U'\0' && QChar::isLetterOrNumber(c);
}

bool contains(std::u32string_view set, char32_t c)
{
    return c != U'\0' && set.find(c) != std::u32string_view::npos;
}

/// Digits and the separators allowed between them, from start: (end, digit count)
std::pair<Index, Index> digitRun(const Text& s, Index start, std::u32string_view separators, Index maximalLength)
{
    Index i = start;
    Index n = 0;
    while (i < Index(s.size()) && i - start < maximalLength)
    {
        const char32_t c = s[size_t(i)];
        if (isDigit(c))
        {
            ++n;
        }
        else if (!((contains(separators, c) && (digit(s, i + 1) || at(s, i + 1) == U'(')) || ((c == U'(' || c == U')') && contains(separators, c))))
        {
            break;
        }
        ++i;
    }

    // Never end on a separator
    while (i > start && !isDigit(s[size_t(i - 1)]))
    {
        --i;
    }
    return { i, n };
}

/// Whether the separated digit run goes on past k in the direction step (1 or -1):
/// the next digit after k is reached through separators only
bool runContinues(const Text& s, Index k, Index step, std::u32string_view separators)
{
    Index j = k;
    while (true)
    {
        j += step;
        if (j < 0 || j >= Index(s.size()))
        {
            return false;
        }
        const char32_t c = s[size_t(j)];
        if (isDigit(c))
        {
            return true;
        }
        if (!contains(separators, c))
        {
            return false;
        }
    }
}

Length date(const Text& s, Index i);

Length phone(const Text& s, Index i)
{
    static constexpr std::u32string_view separators = U" -.()";
    const char32_t c = at(s, i);
    const bool startOk = (c == U'+' && digit(s, i + 1)) || (c == U'(' && digit(s, i + 1)) || isDigit(c);
    if (!startOk || word(s, i - 1) || at(s, i - 1) == U'+')
    {
        return std::nullopt;
    }

    // A phone number is a whole run of digit groups. The tail of a longer one (an IBAN,
    // a company or social security number) is not a phone number, however it is grouped
    // (PdfCraft #1884).
    if (runContinues(s, i, -1, separators))
    {
        return std::nullopt;
    }

    const bool plus = c == U'+';
    const Index first = plus ? i + 1 : i;
    const auto [end, n] = digitRun(s, first, separators, 28);
    Index separatorCount = 0;
    for (Index k = first; k < end; ++k)
    {
        if (!isDigit(s[size_t(k)]))
        {
            ++separatorCount;
        }
    }

    // Always grouped by at least one separator, so plain numbers never match; nor
    // does the head of a run too long to be a phone number.
    if (separatorCount == 0 || digit(s, end) || runContinues(s, end - 1, 1, separators))
    {
        return std::nullopt;
    }

    bool ok = false;
    if (plus)
    {
        // E.164: a + country code and up to 15 digits in all ("+49 2151 123456")
        ok = n >= 8 && n <= 15;
    }
    else
    {
        // North American: 10 digits, 11 with a country code ("(555) 123-4567")
        const bool nanp = n >= 10 && n <= 11;

        // National with a trunk 0 and 6-14 more digits ("0211 123456"). The first group
        // needs two digits, so decimals ("0.1234567") don't match, and day-first dates
        // ("01.10.2026") are left to the date pattern.
        Index k = first;
        while (k < end && !isDigit(s[size_t(k)]))
        {
            ++k;
        }
        const bool trunkZero = k < end && s[size_t(k)] == U'0';
        Index lead = 0;
        while (k < end && isDigit(s[size_t(k)]))
        {
            ++lead;
            ++k;
        }
        const bool trunk = trunkZero && n >= 7 && n <= 15 && lead >= 2 && !date(s, i).has_value();
        ok = nanp || trunk;
    }

    return ok ? Length(end - i) : std::nullopt;
}

/// Scripts written without spaces between words (Chinese, Japanese, Korean, Thai...):
/// text in them often runs straight into an address, so their letters never count
/// as part of one
bool isSpaceless(char32_t c)
{
    const uint32_t u = uint32_t(c);
    return (u >= 0x0E00 && u <= 0x0EFF) || (u >= 0x1000 && u <= 0x109F) || (u >= 0x1100 && u <= 0x11FF) ||
           (u >= 0x1780 && u <= 0x17FF) || (u >= 0x2E80 && u <= 0x9FFF) || (u >= 0xA960 && u <= 0xA97F) ||
           (u >= 0xAC00 && u <= 0xD7FF) || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0xFF00 && u <= 0xFFEF) ||
           (u >= 0x20000 && u <= 0x3FFFF);
}

bool isAsciiAlphanumeric(char32_t c)
{
    return isDigit(c) || (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}

/// A letter or digit of an address: ASCII, or of any script written with spaces
bool isAddressCharacter(char32_t c)
{
    return isAsciiAlphanumeric(c) || (c != U'\0' && QChar::isLetterOrNumber(c) && !isSpaceless(c));
}

Length email(const Text& s, Index i)
{
    auto isLocal = [](char32_t c) { return isAddressCharacter(c) || contains(U"._%+-", c); };

    // A match starts at the first character of a run of local characters, which may
    // be punctuation ("_admin@host.com"), but the run needs a letter or digit
    if (!isLocal(at(s, i)) || (i > 0 && isLocal(at(s, i - 1))))
    {
        return std::nullopt;
    }

    Index j = i;
    bool hasAddressCharacter = false;
    while (j < Index(s.size()) && isLocal(s[size_t(j)]))
    {
        hasAddressCharacter = hasAddressCharacter || isAddressCharacter(s[size_t(j)]);
        ++j;
    }
    if (at(s, j) != U'@' || !hasAddressCharacter)
    {
        return std::nullopt;
    }

    const Index domainStart = j + 1;
    Index k = domainStart;
    while (k < Index(s.size()) && (isAddressCharacter(s[size_t(k)]) || s[size_t(k)] == U'-' || (s[size_t(k)] == U'.' && isAddressCharacter(at(s, k + 1)))))
    {
        ++k;
    }

    const Text domain = s.substr(size_t(domainStart), size_t(k - domainStart));
    const size_t lastDot = domain.rfind(U'.');
    const Text topLevelDomain = lastDot == Text::npos ? domain : domain.substr(lastDot + 1);
    bool isAlphabetic = !topLevelDomain.empty();
    for (char32_t c : topLevelDomain)
    {
        isAlphabetic = isAlphabetic && QChar::isLetter(c);
    }

    if (lastDot != Text::npos && topLevelDomain.size() >= 2 && isAlphabetic)
    {
        return k - i;
    }
    return std::nullopt;
}

bool luhn(const std::vector<int>& digits)
{
    int sum = 0;
    int k = 0;
    for (auto it = digits.crbegin(); it != digits.crend(); ++it, ++k)
    {
        int d = *it;
        if (k % 2 == 1)
        {
            d *= 2;
            if (d > 9)
            {
                d -= 9;
            }
        }
        sum += d;
    }
    return sum % 10 == 0;
}

Length creditCard(const Text& s, Index i)
{
    static constexpr std::u32string_view separators = U" -";
    if (!digit(s, i) || digit(s, i - 1))
    {
        return std::nullopt;
    }

    // Room for the longest card (19 digits in groups, 23 characters) and a tail of up to four digits
    const auto [end, n] = digitRun(s, i, separators, 29);

    // A run longer than that is some other, longer number
    if (end == i || runContinues(s, end - 1, 1, separators))
    {
        return std::nullopt;
    }

    // The longest run may carry a trailing number (an expiry, a year, a CVV), so it is
    // tried, and then each shorter run ending at a group boundary, longest first. The
    // dropped tail is at most four digits, so a longer number (the digits of an IBAN)
    // doesn't match on a prefix.
    Index count = n;
    for (Index e = end; e >= i + 1; --e)
    {
        if (n - count > 4)
        {
            break;
        }
        if (!digit(s, e - 1))
        {
            continue;
        }

        const bool boundary = (e == end) ? !digit(s, end) : contains(separators, at(s, e));
        if (boundary && count >= 13 && count <= 19)
        {
            std::vector<int> digits;
            for (Index k = i; k < e; ++k)
            {
                if (isDigit(s[size_t(k)]))
                {
                    digits.push_back(int(s[size_t(k)] - U'0'));
                }
            }
            if (luhn(digits))
            {
                return e - i;
            }
        }
        count = std::max<Index>(count - 1, 0);
    }
    return std::nullopt;
}

Length socialSecurityNumber(const Text& s, Index i)
{
    if (digit(s, i - 1))
    {
        return std::nullopt;
    }

    auto group = [&s](Index from, Index n)
    {
        for (Index k = from; k < from + n; ++k)
        {
            if (!digit(s, k))
            {
                return false;
            }
        }
        return true;
    };
    auto separator = [&s](Index k) { return at(s, k) == U'-' || at(s, k) == U' '; };

    if (!(group(i, 3) && separator(i + 3) && group(i + 4, 2) && separator(i + 6) && group(i + 7, 4)) || digit(s, i + 11))
    {
        return std::nullopt;
    }

    // The area number 000, 666 and 900-999 is never assigned
    const Text area = s.substr(size_t(i), 3);
    if (area == U"000" || area == U"666" || area[0] == U'9')
    {
        return std::nullopt;
    }
    return 11;
}

/// Month names the date pattern knows: English, then the languages whose dates are
/// written "5 Ekim 2024" / "5. Oktober 2024" (day, month name, year)
const std::array<std::array<std::u32string_view, 12>, 5> monthNames = {{
    { U"january", U"february", U"march", U"april", U"may", U"june", U"july", U"august", U"september", U"october", U"november", U"december" },
    { U"ocak", U"şubat", U"mart", U"nisan", U"mayıs", U"haziran", U"temmuz", U"ağustos", U"eylül", U"ekim", U"kasım", U"aralık" },
    { U"januar", U"februar", U"märz", U"april", U"mai", U"juni", U"juli", U"august", U"september", U"oktober", U"november", U"dezember" },
    { U"janvier", U"février", U"mars", U"avril", U"mai", U"juin", U"juillet", U"août", U"septembre", U"octobre", U"novembre", U"décembre" },
    { U"gennaio", U"febbraio", U"marzo", U"aprile", U"maggio", U"giugno", U"luglio", U"agosto", U"settembre", U"ottobre", U"novembre", U"dicembre" },
}};

/// Abbreviations that may end in a full stop: English and Turkish
const std::array<std::array<std::u32string_view, 12>, 2> monthAbbreviations = {{
    { U"jan", U"feb", U"mar", U"apr", U"may", U"jun", U"jul", U"aug", U"sep", U"oct", U"nov", U"dec" },
    { U"oca", U"şub", U"mar", U"nis", U"may", U"haz", U"tem", U"ağu", U"eyl", U"eki", U"kas", U"ara" },
}};

/// One lowercase character, with the Turkish dotted and dotless i the same as i
char32_t fold(char32_t c)
{
    if (c == U'I' || c == U'İ' || c == U'ı')
    {
        return U'i';
    }
    return QChar::toLower(c);
}

/// Characters the name covers at i, when it stands there as a whole word (any case)
Length wordAt(const Text& s, Index i, std::u32string_view name)
{
    Index n = 0;
    for (char32_t c : name)
    {
        const char32_t textCharacter = at(s, i + n);
        if (textCharacter == U'\0' || fold(textCharacter) != fold(c))
        {
            return std::nullopt;
        }
        ++n;
    }

    const char32_t next = at(s, i + n);
    if (next != U'\0' && QChar::isLetter(next))
    {
        return std::nullopt;
    }
    return n;
}

/// A month name or its abbreviation (optionally with a full stop) at i
Length month(const Text& s, Index i)
{
    if (word(s, i - 1))
    {
        return std::nullopt;
    }

    Length longest;
    for (const auto& names : monthNames)
    {
        for (std::u32string_view name : names)
        {
            if (Length n = wordAt(s, i, name); n && (!longest || *n > *longest))
            {
                longest = n;
            }
        }
    }
    if (longest)
    {
        return longest;
    }

    for (const auto& names : monthAbbreviations)
    {
        for (std::u32string_view name : names)
        {
            if (Length n = wordAt(s, i, name))
            {
                return at(s, i + *n) == U'.' ? *n + 1 : *n;
            }
        }
    }
    return std::nullopt;
}

Length number(const Text& s, Index i, Index minimum, Index maximum)
{
    Index n = 0;
    while (n <= maximum && digit(s, i + n))
    {
        ++n;
    }
    return (n >= minimum && n <= maximum) ? Length(n) : std::nullopt;
}

Index spaces(const Text& s, Index k)
{
    Index n = 0;
    while (at(s, k + n) == U' ')
    {
        ++n;
    }
    return n;
}

Length date(const Text& s, Index i)
{
    if (word(s, i - 1))
    {
        return std::nullopt;
    }

    // Numeric: m/d/yy(yy), m-d-yyyy, d.m.yyyy, yyyy-mm-dd
    if (const Length a = number(s, i, 1, 4))
    {
        const char32_t separator = at(s, i + *a);
        if (separator == U'/' || separator == U'-' || separator == U'.')
        {
            if (const Length b = number(s, i + *a + 1, 1, 2); b && at(s, i + *a + 1 + *b) == separator)
            {
                const Length c = number(s, i + *a + *b + 2, *a == 4 ? 1 : 2, *a == 4 ? 2 : 4);
                if (c && (*a <= 2 || *a == 4) && !digit(s, i + *a + *b + 2 + *c))
                {
                    return *a + *b + *c + 2;
                }
            }
        }

        // "5 January 2024", "5 Ekim 2024", "5. Oktober 2024"
        const Index day = i + *a + (at(s, i + *a) == U'.' ? 1 : 0);
        if (*a <= 2)
        {
            const Index k = day + spaces(s, day);
            if (k > day)
            {
                if (const Length m = month(s, k))
                {
                    const Index k2 = k + *m + spaces(s, k + *m);
                    if (const Length y = number(s, k2, 4, 4))
                    {
                        return k2 + *y - i;
                    }
                }
            }
        }
        return std::nullopt;
    }

    // "January 5, 2024" / "Jan. 5 2024"
    const Length m = month(s, i);
    if (!m)
    {
        return std::nullopt;
    }
    const Index k = i + *m + spaces(s, i + *m);
    const Length d = number(s, k, 1, 2);
    if (!d)
    {
        return std::nullopt;
    }
    Index k2 = k + *d;
    if (at(s, k2) == U',')
    {
        ++k2;
    }
    k2 += spaces(s, k2);
    const Length y = number(s, k2, 4, 4);
    if (!y)
    {
        return std::nullopt;
    }
    return k2 + *y - i;
}

}   // namespace

std::vector<PDFFirePiiPatterns::Pattern> PDFFirePiiPatterns::getPatterns()
{
    return { Pattern::SocialSecurityNumber, Pattern::PhoneNumber, Pattern::EmailAddress, Pattern::CreditCardNumber, Pattern::Date };
}

QString PDFFirePiiPatterns::getName(Pattern pattern)
{
    switch (pattern)
    {
        case Pattern::SocialSecurityNumber:
            return QCoreApplication::translate("pdf::PDFFirePiiPatterns", "Social Security Numbers");
        case Pattern::PhoneNumber:
            return QCoreApplication::translate("pdf::PDFFirePiiPatterns", "Phone Numbers");
        case Pattern::EmailAddress:
            return QCoreApplication::translate("pdf::PDFFirePiiPatterns", "E-mail Addresses");
        case Pattern::CreditCardNumber:
            return QCoreApplication::translate("pdf::PDFFirePiiPatterns", "Credit Card Numbers");
        case Pattern::Date:
            return QCoreApplication::translate("pdf::PDFFirePiiPatterns", "Dates");
    }

    Q_ASSERT(false);
    return QString();
}

std::vector<PDFFirePiiPatterns::Match> PDFFirePiiPatterns::find(Pattern pattern, const QString& text)
{
    // The text as code points, and the UTF-16 index of each of them
    Text s;
    std::vector<int> offsets;
    s.reserve(size_t(text.size()));
    offsets.reserve(size_t(text.size()) + 1);
    for (int i = 0; i < text.size(); ++i)
    {
        offsets.push_back(i);
        const QChar c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate())
        {
            s.push_back(QChar::surrogateToUcs4(c, text.at(i + 1)));
            ++i;
        }
        else
        {
            s.push_back(c.unicode());
        }
    }
    offsets.push_back(int(text.size()));

    Length (*matcher)(const Text&, Index) = nullptr;
    switch (pattern)
    {
        case Pattern::SocialSecurityNumber:
            matcher = &socialSecurityNumber;
            break;
        case Pattern::PhoneNumber:
            matcher = &phone;
            break;
        case Pattern::EmailAddress:
            matcher = &email;
            break;
        case Pattern::CreditCardNumber:
            matcher = &creditCard;
            break;
        case Pattern::Date:
            matcher = &date;
            break;
    }

    std::vector<Match> matches;
    Index i = 0;
    while (matcher && i < Index(s.size()))
    {
        const Length n = matcher(s, i);
        if (n && *n > 0)
        {
            const int start = offsets[size_t(i)];
            matches.push_back({ start, offsets[size_t(i + *n)] - start });
            i += *n;
        }
        else
        {
            ++i;
        }
    }
    return matches;
}

QStringList PDFFirePiiPatterns::findTexts(Pattern pattern, const QString& text)
{
    QStringList result;
    for (const Match& match : find(pattern, text))
    {
        result << text.mid(match.start, match.length);
    }
    return result;
}

bool PDFFirePiiPatterns::isLuhnValid(const QString& digits)
{
    std::vector<int> values;
    for (QChar c : digits)
    {
        if (c >= QChar('0') && c <= QChar('9'))
        {
            values.push_back(c.unicode() - '0');
        }
    }
    return !values.empty() && luhn(values);
}

}   // namespace pdf
