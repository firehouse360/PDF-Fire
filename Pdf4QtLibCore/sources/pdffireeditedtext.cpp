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

#include "pdffireeditedtext.h"

#include <QRegularExpression>

#include "pdfdbgheap.h"

namespace pdf
{

QString PDFEditedTextMarkup::getAttribute(const QString& command, const QString& attribute)
{
    const QString pattern = QString(" %1=\"").arg(attribute);
    const qsizetype start = command.indexOf(pattern);
    if (start == -1)
    {
        return QString();
    }

    const qsizetype valueStart = start + pattern.size();
    const qsizetype valueEnd = command.indexOf(QChar('"'), valueStart);
    return valueEnd == -1 ? QString() : command.mid(valueStart, valueEnd - valueStart);
}

PDFEditedTextMarkup::PDFEditedTextMarkup(const QString& markup) :
    m_markup(markup)
{
    // The first pass splits the markup to the tokens, the second one decides, what the
    // positioning commands mean for the plain text (a line break, a gap between words, or
    // nothing - many documents position every few characters, even every character).
    struct PositionInfo
    {
        size_t tokenIndex = 0;
        PDFReal x = 0.0;
        PDFReal y = 0.0;
    };
    std::vector<PositionInfo> positions;

    qsizetype i = 0;
    while (i < markup.size())
    {
        const QChar character = markup[i];

        if (character == QChar('<'))
        {
            qsizetype end = markup.indexOf(QChar('>'), i);
            if (end == -1)
            {
                end = markup.size() - 1;
            }

            Token token;
            token.raw = markup.mid(i, end - i + 1);

            qsizetype nameEnd = 1;
            while (nameEnd < token.raw.size() && (token.raw[nameEnd].isLetterOrNumber() || token.raw[nameEnd] == QChar('_')))
            {
                ++nameEnd;
            }
            token.name = token.raw.mid(1, nameEnd - 1);
            if (token.name.isEmpty())
            {
                // Not a command we know, it is kept as it is
                token.name = "unknown";
            }

            if (token.name == "character")
            {
                // A glyph with no single character: its text, if it has one (a ligature)
                QString text = getAttribute(token.raw, "text");
                text.replace("&quot;", "\"").replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&");
                token.plainText = text.isEmpty() ? QString(UNKNOWN_CHARACTER) : text;
            }
            else if (token.name == "tpos" || token.name == "tmatrix")
            {
                positions.push_back({ m_tokens.size(), getAttribute(token.raw, "x").toDouble(), getAttribute(token.raw, "y").toDouble() });
            }

            m_tokens.push_back(qMove(token));
            i = end + 1;
        }
        else if (character == QChar('&'))
        {
            // An escaped character
            static const std::pair<const char*, QChar> entities[] = { { "&amp;", QChar('&') }, { "&lt;", QChar('<') }, { "&gt;", QChar('>') }, { "&quot;", QChar('"') }, { "&apos;", QChar('\'') } };

            Token token;
            for (const auto& entity : entities)
            {
                const QLatin1StringView text(entity.first);
                if (QStringView(markup).mid(i).startsWith(text))
                {
                    token.raw = markup.mid(i, text.size());
                    token.plainText = QString(entity.second);
                    break;
                }
            }

            if (token.raw.isEmpty())
            {
                token.raw = QString(character);
                token.plainText = QString(character);
            }

            i += token.raw.size();
            m_tokens.push_back(qMove(token));
        }
        else
        {
            Token token;
            token.raw = QString(character);
            token.plainText = QString(character);
            m_tokens.push_back(qMove(token));
            ++i;
        }
    }

    // The characters between two positions on the same line: the typical distance per
    // character tells, which moves are just the next piece of a word, and which ones leave
    // a gap between words. (The widths of the glyphs are not known here.)
    auto countCharacters = [this](size_t from, size_t to)
    {
        int count = 0;
        for (size_t tokenIndex = from; tokenIndex < to; ++tokenIndex)
        {
            if (!m_tokens[tokenIndex].isCommand() || m_tokens[tokenIndex].name == "character")
            {
                count += qMax<int>(1, int(m_tokens[tokenIndex].plainText.size()));
            }
        }
        return count;
    };

    std::vector<PDFReal> distancesPerCharacter;
    for (size_t k = 1; k < positions.size(); ++k)
    {
        const int characterCount = countCharacters(positions[k - 1].tokenIndex, positions[k].tokenIndex);
        const PDFReal distance = positions[k].x - positions[k - 1].x;
        if (qFuzzyCompare(1.0 + positions[k].y, 1.0 + positions[k - 1].y) && characterCount > 0 && distance > 0.0)
        {
            distancesPerCharacter.push_back(distance / characterCount);
        }
    }

    PDFReal typicalDistance = 0.0;
    if (!distancesPerCharacter.empty())
    {
        std::vector<PDFReal> sorted = distancesPerCharacter;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        typicalDistance = sorted[sorted.size() / 2];
    }

    for (size_t k = 1; k < positions.size(); ++k)
    {
        const PositionInfo& previous = positions[k - 1];
        const PositionInfo& current = positions[k];
        const int characterCount = countCharacters(previous.tokenIndex, current.tokenIndex);
        if (characterCount == 0)
        {
            continue;
        }

        Token& token = m_tokens[current.tokenIndex];
        if (!qFuzzyCompare(1.0 + current.y, 1.0 + previous.y))
        {
            token.plainText = QString(QChar('\n'));
        }
        else
        {
            // A gap, which is larger than the characters of the piece need. With too few
            // samples to know the typical distance, every move on the line is taken as a
            // gap between words (the text is positioned by words then, not by characters).
            const PDFReal distance = current.x - previous.x;
            const bool hasTypicalDistance = distancesPerCharacter.size() >= 3 && typicalDistance > 0.0;
            const bool isGap = distance < 0.0 || !hasTypicalDistance || distance > (characterCount + 0.35) * typicalDistance;
            if (isGap)
            {
                token.plainText = QString(QChar(' '));
            }
        }
    }

    // The gaps between words made by kerning, and the synthetic spaces must not double
    // a space, which is written in the text
    bool isLastPlainCharacterSpace = true;
    for (Token& token : m_tokens)
    {
        if (token.name == "space")
        {
            if (getAttribute(token.raw, "advance").toDouble() <= -WORD_GAP_ADVANCE && !isLastPlainCharacterSpace)
            {
                token.plainText = QString(QChar(' '));
            }
        }
        else if (token.plainText == QString(QChar(' ')) && token.isCommand() && isLastPlainCharacterSpace)
        {
            token.plainText.clear();
        }

        if (!token.plainText.isEmpty())
        {
            isLastPlainCharacterSpace = token.plainText.back().isSpace();
        }
    }
}

QString PDFEditedTextMarkup::getPlainText() const
{
    QString text;
    for (const Token& token : m_tokens)
    {
        text += token.plainText;
    }
    return text;
}

bool PDFEditedTextMarkup::hasUnknownCharacters() const
{
    return std::any_of(m_tokens.cbegin(), m_tokens.cend(), [](const Token& token) { return token.name == "character" && token.plainText == QString(UNKNOWN_CHARACTER); });
}

QString PDFEditedTextMarkup::getMarkupWithPlainText(const QString& plainText) const
{
    const QString oldPlainText = getPlainText();
    if (plainText == oldPlainText)
    {
        return m_markup;
    }

    // The common start and the common end of the old and of the new text
    const qsizetype maximalLength = qMin(oldPlainText.size(), plainText.size());
    qsizetype prefixLength = 0;
    while (prefixLength < maximalLength && oldPlainText[prefixLength] == plainText[prefixLength])
    {
        ++prefixLength;
    }

    qsizetype suffixLength = 0;
    while (suffixLength < maximalLength - prefixLength &&
           oldPlainText[oldPlainText.size() - 1 - suffixLength] == plainText[plainText.size() - 1 - suffixLength])
    {
        ++suffixLength;
    }

    qsizetype replacedEnd = oldPlainText.size() - suffixLength;

    // A token standing for several characters (a ligature) is replaced as a whole: if
    // the change begins or ends inside of it, the change is extended to cover it
    {
        qsizetype offset = 0;
        for (const Token& token : m_tokens)
        {
            const qsizetype tokenStart = offset;
            const qsizetype tokenEnd = offset + token.plainText.size();
            offset = tokenEnd;

            if (token.plainText.size() > 1)
            {
                if (tokenStart < prefixLength && prefixLength < tokenEnd)
                {
                    prefixLength = tokenStart;
                }
                if (tokenStart < replacedEnd && replacedEnd < tokenEnd)
                {
                    replacedEnd = tokenEnd;
                }
            }
        }
        suffixLength = oldPlainText.size() - replacedEnd;
    }

    // The new text between them. A line cannot be broken by typing (the position
    // of each line is a command of the markup), so a new line feed is a space.
    QString insertedText = plainText.mid(prefixLength, plainText.size() - prefixLength - suffixLength);
    insertedText.replace(QChar('\n'), QChar(' '));
    insertedText.remove(UNKNOWN_CHARACTER);
    insertedText.remove(QChar('\r'));

    QString markup;
    markup.reserve(m_markup.size() + insertedText.size() * 2);

    bool isInserted = false;
    auto insert = [&]()
    {
        if (!isInserted)
        {
            markup += insertedText.toHtmlEscaped();
            isInserted = true;
        }
    };

    qsizetype offset = 0;
    for (const Token& token : m_tokens)
    {
        const qsizetype tokenStart = offset;
        const qsizetype tokenEnd = offset + token.plainText.size();
        offset = tokenEnd;

        if (token.plainText.isEmpty())
        {
            // A token, which is not visible in the plain text. The commands, which change the
            // state or the position, are always kept. At the very start of the text they stay
            // before the inserted text (they position and style it), at the place of the
            // insertion they follow it (the typed text continues the text before it).
            if (tokenStart < prefixLength || (tokenStart == prefixLength && prefixLength == 0))
            {
                markup += token.raw;
            }
            else if (tokenStart >= replacedEnd && (tokenStart > prefixLength || replacedEnd == prefixLength))
            {
                insert();
                markup += token.raw;
            }
            else
            {
                insert();
                if (token.isStateCommand())
                {
                    markup += token.raw;
                }
                // Kerning inside of the replaced text is dropped
            }
        }
        else if (tokenEnd <= prefixLength)
        {
            markup += token.raw;
        }
        else if (tokenStart >= replacedEnd)
        {
            insert();
            markup += token.raw;
        }
        else
        {
            // The token is a part of the replaced text. Its command is kept, if it changes the
            // state or the position (a line start, for example) - only its text is replaced.
            insert();
            if (token.isStateCommand())
            {
                markup += token.raw;
            }
        }
    }

    insert();
    return markup;
}

PDFReal PDFEditedTextMarkup::getFirstFontSize() const
{
    for (const Token& token : m_tokens)
    {
        if (token.name == "tf")
        {
            return getAttribute(token.raw, "size").toDouble();
        }
    }

    return 0.0;
}

QString PDFEditedTextMarkup::getMarkupWithScaledFontSize(PDFReal factor) const
{
    QString markup;
    markup.reserve(m_markup.size());

    for (const Token& token : m_tokens)
    {
        if (token.name == "tf")
        {
            const QString size = getAttribute(token.raw, "size");
            QString command = token.raw;
            command.replace(QString(" size=\"%1\"").arg(size), QString(" size=\"%1\"").arg(size.toDouble() * factor));
            markup += command;
        }
        else
        {
            markup += token.raw;
        }
    }

    return markup;
}

}   // namespace pdf
