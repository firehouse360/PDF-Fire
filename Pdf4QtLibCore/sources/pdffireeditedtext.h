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

#ifndef PDFFIREEDITEDTEXT_H
#define PDFFIREEDITEDTEXT_H

#include "pdfglobal.h"

#include <QString>

#include <vector>

namespace pdf
{

/// PDF Fire: a text of the page is edited as a markup (see the class
/// PDFEditedPageContentElementText) - the characters, mixed with the commands, which
/// set the font, the color, the position of the following text, the kerning... The
/// markup is precise, but it is not something a user should have to read. This class
/// shows the markup as a plain text, and it writes a changed plain text back into the
/// markup: only the changed part of the markup is replaced, everything before and after
/// it stays exactly as it was - so the text, which was not edited, does not move.
class PDF4QTLIBCORESHARED_EXPORT PDFEditedTextMarkup
{
public:
    explicit PDFEditedTextMarkup(const QString& markup);

    /// A character of the plain text standing for a glyph, which has no unicode
    /// value (so it cannot be displayed or typed, but it is kept, if it is not deleted)
    static constexpr QChar UNKNOWN_CHARACTER = QChar(0xFFFC);

    /// Kerning larger than this (in the thousandths of the font size) is a gap between words
    static constexpr PDFReal WORD_GAP_ADVANCE = 200.0;

    /// Returns the markup this object was created from
    const QString& getMarkup() const { return m_markup; }

    /// Returns the text as the user reads it. The lines are separated by line feeds.
    QString getPlainText() const;

    /// Returns true, if the text contains glyphs with no unicode value
    bool hasUnknownCharacters() const;

    /// Returns the markup, in which the plain text is changed to the given one. The
    /// part of the markup, which belongs to the unchanged start and to the unchanged
    /// end of the text, is kept as it is.
    QString getMarkupWithPlainText(const QString& plainText) const;

    /// Returns the size of the font set by the first font command of the markup,
    /// or zero, if there is no font command (then the font of the element is used).
    PDFReal getFirstFontSize() const;

    /// Returns the markup with all font sizes multiplied by the factor
    QString getMarkupWithScaledFontSize(PDFReal factor) const;

private:
    struct Token
    {
        QString raw;            ///< The token, as it is written in the markup
        QString name;           ///< Name of the command (empty for a character)
        QString plainText;      ///< What the token adds to the plain text (nothing, a character, or the text of a ligature)

        bool isCommand() const { return !name.isEmpty(); }

        /// A command, which changes the state of the text, or its position - it
        /// is kept even if the text around it is replaced.
        bool isStateCommand() const { return isCommand() && name != "space" && name != "character"; }
    };

    static QString getAttribute(const QString& command, const QString& attribute);

    QString m_markup;
    std::vector<Token> m_tokens;
};

}   // namespace pdf

#endif // PDFFIREEDITEDTEXT_H
