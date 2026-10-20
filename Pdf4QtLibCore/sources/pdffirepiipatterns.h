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

#ifndef PDFFIREPIIPATTERNS_H
#define PDFFIREPIIPATTERNS_H

#include "pdfglobal.h"

#include <QString>

#include <vector>

namespace pdf
{

/// PDF Fire: patterns of personal information to be found and redacted - phone
/// numbers, e-mail addresses, credit card numbers (with the Luhn checksum), US Social
/// Security numbers and dates. The matchers work on the characters of a text and
/// return the ranges of the matches. They are not regular expressions: a match must
/// stand alone (the tail of a longer number is not a phone number, a card number must
/// have a valid checksum...).
///
/// Ported from PdfCraft crates/redact/src/patterns.rs (MIT OR Apache-2.0).
class PDF4QTLIBCORESHARED_EXPORT PDFFirePiiPatterns
{
public:
    enum class Pattern
    {
        SocialSecurityNumber,
        PhoneNumber,
        EmailAddress,
        CreditCardNumber,
        Date
    };

    /// A match - the index of its first UTF-16 code unit in the text, and its length
    struct Match
    {
        int start = 0;
        int length = 0;

        bool operator==(const Match&) const = default;
    };

    /// All the patterns, in the order they are offered to the user
    static std::vector<Pattern> getPatterns();

    /// Returns the name of the pattern, as it is shown to the user
    static QString getName(Pattern pattern);

    /// Finds all matches of the pattern in the text
    static std::vector<Match> find(Pattern pattern, const QString& text);

    /// Finds all matches of the pattern in the text, returns the matched texts
    static QStringList findTexts(Pattern pattern, const QString& text);

    /// Checks the Luhn checksum of the digits of the text (other characters are skipped)
    static bool isLuhnValid(const QString& digits);
};

}   // namespace pdf

#endif // PDFFIREPIIPATTERNS_H
