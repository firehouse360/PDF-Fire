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

#include "odtlayout.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <set>

namespace pdfplugin
{

namespace
{

constexpr double DEFAULT_FONT_SIZE = 11.0;

/// Ascent and descent of a typical text font, in portions of the font size - used to
/// estimate the box of a line from its baseline (the glyph outlines are not reliable,
/// a line of "aaa" is lower than a line of "Ålg")
constexpr double ASCENT = 0.93;
constexpr double DESCENT = 0.28;

double safeSize(double size)
{
    return size > 0.1 ? size : DEFAULT_FONT_SIZE;
}

double roundToHalf(double value)
{
    return std::round(value * 2.0) / 2.0;
}

template<typename T>
T percentile(std::vector<T> values, double portion)
{
    if (values.empty())
    {
        return T();
    }
    std::sort(values.begin(), values.end());
    const size_t index = std::min(values.size() - 1, size_t(portion * double(values.size() - 1) + 0.5));
    return values[index];
}

bool isNearBlack(const QColor& color)
{
    return !color.isValid() || (color.red() < 40 && color.green() < 40 && color.blue() < 40);
}

bool isHyphen(QChar character)
{
    return character == QChar('-') || character == QChar(0x2010) || character == QChar(QChar::SoftHyphen);
}

/// Everything known about one line, computed once
struct LineInfo
{
    const OdtLayoutLine* line = nullptr;
    OdtLayoutBuilder::LineText text;
    double left = 0.0;
    double right = 0.0;
    double baseline = 0.0;
    double size = DEFAULT_FONT_SIZE;
    double textLeft = 0.0;          ///< Left of the text after a bullet
    double firstWordWidth = 0.0;
    int block = 0;
    bool rotated = false;
    int dominantStyle = 0;
    double boldFraction = 0.0;
    QChar bullet;

    double top() const { return baseline - ASCENT * size; }
    double bottom() const { return baseline + DESCENT * size; }
};

LineInfo createLineInfo(const OdtLayoutLine& line, const std::vector<OdtCharacterStyle>& styles)
{
    LineInfo info;
    info.line = &line;
    info.text = OdtLayoutBuilder::getLineText(line);
    info.left = line.left();
    info.right = line.right();
    info.baseline = line.baseline();
    info.size = safeSize(line.fontSize());
    info.block = line.block;
    info.rotated = line.rotated;

    std::map<int, int> styleCounts;
    int boldCount = 0;
    int count = 0;
    for (const OdtLayoutCharacter& character : line.characters)
    {
        if (character.character.isSpace())
        {
            continue;
        }
        ++count;
        ++styleCounts[character.style];
        if (character.style >= 0 && character.style < int(styles.size()) && styles[character.style].bold)
        {
            ++boldCount;
        }
    }
    info.boldFraction = count > 0 ? double(boldCount) / count : 0.0;
    if (!styleCounts.empty())
    {
        info.dominantStyle = std::max_element(styleCounts.begin(), styleCounts.end(), [](const auto& l, const auto& r) { return l.second < r.second; })->first;
    }

    // Width of the first word - would it have fitted at the end of the previous line?
    // (the same gap, which makes a space in the text, ends the word)
    const QString& text = info.text.text;
    if (!line.characters.empty())
    {
        double wordRight = line.characters.front().right;
        for (size_t i = 1; i < line.characters.size(); ++i)
        {
            const OdtLayoutCharacter& previous = line.characters[i - 1];
            const OdtLayoutCharacter& character = line.characters[i];
            if (character.character.isSpace() || character.x - previous.right > 0.13 * safeSize(qMin(previous.fontSize, character.fontSize)))
            {
                break;
            }
            wordRight = qMax(wordRight, character.right);
        }
        info.firstWordWidth = wordRight - info.left;
    }

    info.textLeft = info.left;
    if (OdtLayoutBuilder::startsWithBullet(text, &info.bullet))
    {
        // The text starts with the first character after the bullet and the gap
        for (size_t i = 1; i < line.characters.size(); ++i)
        {
            if (!line.characters[i].character.isSpace())
            {
                info.textLeft = line.characters[i].x;
                break;
            }
        }
    }

    return info;
}

/// Paragraph being built - indices into the lines of the page
struct ParagraphLines
{
    std::vector<const LineInfo*> lines;
};

/// Statistics of the whole document
struct DocumentMetrics
{
    double bodySize = DEFAULT_FONT_SIZE;
    std::map<int, double> pitches;      ///< Font size (half points) -> usual distance of baselines
    double bodyLeft = 72.0;
    double bodyRight = 540.0;
    double bodyTop = 72.0;
    double bodyBottom = 720.0;

    double getPitch(double size) const
    {
        auto it = pitches.find(qRound(size * 2.0));
        if (it != pitches.end() && it->second >= 0.9 * size && it->second <= 2.5 * size)
        {
            return it->second;
        }
        return 1.2 * size;
    }
};

OdtAlignment getAlignment(const std::vector<const LineInfo*>& lines, const DocumentMetrics& metrics)
{
    if (lines.empty() || lines.front()->rotated)
    {
        return OdtAlignment::Left;
    }

    const double width = metrics.bodyRight - metrics.bodyLeft;
    const double size = lines.front()->size;
    const double tolerance = qMax(3.0, 0.02 * width);

    bool centered = true;
    bool rightAligned = true;
    for (const LineInfo* line : lines)
    {
        const double leftGap = line->left - metrics.bodyLeft;
        const double rightGap = metrics.bodyRight - line->right;
        if (qAbs(leftGap - rightGap) > tolerance || leftGap < 1.5 * size)
        {
            centered = false;
        }
        if (rightGap > 0.6 * size || leftGap < 0.25 * width)
        {
            rightAligned = false;
        }
    }

    if (centered)
    {
        return OdtAlignment::Center;
    }
    if (rightAligned)
    {
        return OdtAlignment::Right;
    }

    // Justified: all lines but the last end at the same place, the last one ends earlier
    if (lines.size() >= 3)
    {
        double maxRight = 0.0;
        for (const LineInfo* line : lines)
        {
            maxRight = qMax(maxRight, line->right);
        }

        bool justified = true;
        for (size_t i = 0; i + 1 < lines.size(); ++i)
        {
            if (maxRight - lines[i]->right > 1.0)
            {
                justified = false;
                break;
            }
        }
        if (justified && maxRight - lines.back()->right > 2.0 * size)
        {
            return OdtAlignment::Justify;
        }
    }

    return OdtAlignment::Left;
}

/// Decides, whether \p current starts a new paragraph after \p previous
bool isParagraphBreak(const LineInfo& previous,
                      const LineInfo& current,
                      size_t linesInParagraph,
                      double blockRight,
                      const DocumentMetrics& metrics)
{
    if (previous.rotated || current.rotated || previous.block != current.block)
    {
        return true;
    }

    // Different font size - a heading and its text
    const double maxSize = qMax(previous.size, current.size);
    if (qAbs(previous.size - current.size) > 0.12 * maxSize)
    {
        return true;
    }

    // Vertical gap larger than the usual line distance (space between paragraphs)
    const double distance = current.baseline - previous.baseline;
    if (distance <= 0.3 * current.size)
    {
        return true;
    }
    if (distance > metrics.getPitch(current.size) * 1.3 + 0.5)
    {
        return true;
    }

    // A bold line followed by a regular one: a run-in heading ("SECTION 1. Duties")
    if (previous.boldFraction > 0.85 && current.boldFraction < 0.5)
    {
        return true;
    }

    // A bullet always starts a new item
    if (!current.bullet.isNull())
    {
        return true;
    }

    const double indentTolerance = 0.8 * current.size;
    const bool previousEndsWithHyphen = !previous.text.text.isEmpty() && isHyphen(previous.text.text.back());

    // The previous line ends short - the first word of this line would have fitted there,
    // so the writer ended the paragraph on purpose (works for ragged and justified text)
    const double available = blockRight - previous.right;
    const bool previousEndsShort = !previousEndsWithHyphen && current.firstWordWidth + 0.3 * current.size < available - 0.5;

    if (current.left > previous.left + indentTolerance)
    {
        // Indented more than the previous line. A continuation of a list item (hanging
        // indentation) or of a paragraph with a hanging first line - or a new paragraph.
        if (!previous.bullet.isNull() && qAbs(current.left - previous.textLeft) < indentTolerance)
        {
            return previousEndsShort;
        }
        if (linesInParagraph == 1 && !previousEndsShort)
        {
            return false;
        }
        return true;
    }

    if (current.left < previous.left - indentTolerance)
    {
        // Less indented: the continuation of a paragraph with an indented first line,
        // otherwise a new paragraph
        if (linesInParagraph == 1 && previous.bullet.isNull())
        {
            return previousEndsShort;
        }
        return true;
    }

    return previousEndsShort;
}

OdtTextStyle createTextStyle(int styleIndex, double size, const std::vector<OdtCharacterStyle>& styles)
{
    OdtTextStyle style;
    if (styleIndex >= 0 && styleIndex < int(styles.size()))
    {
        const OdtCharacterStyle& characterStyle = styles[styleIndex];
        style.fontFamily = characterStyle.fontFamily;
        style.bold = characterStyle.bold;
        style.italic = characterStyle.italic;
        if (!isNearBlack(characterStyle.color))
        {
            style.color = characterStyle.color;
        }
    }
    style.fontSize = roundToHalf(safeSize(size));
    return style;
}

/// Words of the document (lower case) - used to decide about a hyphen at a line end
using WordSet = std::set<QString>;

QString getTrailingLetters(const QString& text)
{
    qsizetype start = text.size();
    while (start > 0 && text[start - 1].isLetter())
    {
        --start;
    }
    return text.mid(start).toLower();
}

QString getLeadingLetters(const QString& text)
{
    qsizetype end = 0;
    while (end < text.size() && text[end].isLetter())
    {
        ++end;
    }
    return text.left(end).toLower();
}

/// Text of a paragraph: the lines joined by spaces, hyphenation at line ends removed
/// where it is safe (a hyphen after a letter, the next line starts with a small letter).
/// A hyphen of a compound word ("machine-\ngenerated") must stay - the words of the
/// document itself decide: "element" is used elsewhere -> join; "machine" and "generated"
/// are words, "machinegenerated" is not -> keep the hyphen.
void createParagraphText(const std::vector<const LineInfo*>& lines,
                         bool stripBullet,
                         QString& text,
                         std::vector<int>& characterStyles,
                         std::vector<double>& characterSizes,
                         const WordSet* words = nullptr)
{
    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        const LineInfo* line = lines[lineIndex];
        QString lineText = line->text.text;
        std::vector<int> lineStyles = line->text.styles;
        std::vector<double> lineSizes = line->text.sizes;

        if (lineIndex == 0 && stripBullet && !line->bullet.isNull())
        {
            // Drop the bullet and the white space after it - the list style draws it
            qsizetype start = 1;
            while (start < lineText.size() && lineText[start].isSpace())
            {
                ++start;
            }
            lineText = lineText.mid(start);
            lineStyles.erase(lineStyles.begin(), lineStyles.begin() + start);
            lineSizes.erase(lineSizes.begin(), lineSizes.begin() + start);
        }

        if (!text.isEmpty())
        {
            const QChar last = text.back();
            const bool canJoin = !lineText.isEmpty() && lineText.front().isLower() && text.size() >= 2 && text[text.size() - 2].isLetter();
            bool keepCompoundHyphen = false;
            if (last != QChar(QChar::SoftHyphen) && isHyphen(last) && canJoin && words)
            {
                const QString before = getTrailingLetters(text.left(text.size() - 1));
                const QString after = getLeadingLetters(lineText);
                keepCompoundHyphen = !words->count(before + after) && words->count(before) && words->count(after);
            }

            if (keepCompoundHyphen)
            {
                // "machine-generated" - nothing between the lines
            }
            else if (last == QChar(QChar::SoftHyphen) || (isHyphen(last) && canJoin))
            {
                // "fire-" + "fighters" = "firefighters"
                text.chop(1);
                characterStyles.pop_back();
                characterSizes.pop_back();
            }
            else if (!last.isSpace())
            {
                text += QChar(' ');
                characterStyles.push_back(characterStyles.back());
                characterSizes.push_back(characterSizes.back());
            }
        }

        text += lineText;
        characterStyles.insert(characterStyles.end(), lineStyles.begin(), lineStyles.end());
        characterSizes.insert(characterSizes.end(), lineSizes.begin(), lineSizes.end());
    }

    // A soft hyphen inside the text is invisible
    for (qsizetype i = text.size() - 1; i >= 0; --i)
    {
        if (text[i] == QChar(QChar::SoftHyphen))
        {
            text.remove(i, 1);
            characterStyles.erase(characterStyles.begin() + i);
            characterSizes.erase(characterSizes.begin() + i);
        }
    }
}

std::vector<OdtSpan> createSpans(const QString& text,
                                 const std::vector<int>& characterStyles,
                                 const std::vector<double>& characterSizes,
                                 const std::vector<OdtCharacterStyle>& styles)
{
    std::vector<OdtSpan> spans;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        OdtTextStyle style = createTextStyle(characterStyles[i], characterSizes[i], styles);

        // A space takes the style of the text before it - fewer spans, the same look
        if (text[i].isSpace() && !spans.empty())
        {
            spans.back().text += text[i];
            continue;
        }

        if (spans.empty() || !(spans.back().style == style))
        {
            OdtSpan span;
            span.style = style;
            spans.push_back(span);
        }
        spans.back().text += text[i];
    }
    return spans;
}

/// Tab stops of a paragraph from the positions of the wide gaps of its lines, relative
/// to the left edge of the paragraph - the columns of "Name<tab>Value" stay aligned
std::vector<double> getTabStops(const std::vector<const LineInfo*>& lines, double paragraphLeft)
{
    std::vector<double> stops;
    for (const LineInfo* line : lines)
    {
        for (double position : line->text.tabPositions)
        {
            const double stop = position - paragraphLeft;
            if (stop > 1.0)
            {
                stops.push_back(stop);
            }
        }
    }
    std::sort(stops.begin(), stops.end());
    std::vector<double> result;
    for (double stop : stops)
    {
        if (result.empty() || stop - result.back() > 2.0)
        {
            result.push_back(stop);
        }
    }
    return result;
}

/// An element of the body of a page - a paragraph or an image - with its position
struct PageItem
{
    OdtParagraph paragraph;
    bool isImage = false;
    bool rotated = false;
    double top = 0.0;
    double bottom = 0.0;
    double firstBaseline = 0.0;
    double lastBaseline = 0.0;
    double firstSize = DEFAULT_FONT_SIZE;
    double lastSize = DEFAULT_FONT_SIZE;
    double dominantSize = DEFAULT_FONT_SIZE;
    double boldFraction = 0.0;
    size_t lineCount = 0;
};

/// Normalizes the text of a header/footer line: page numbers differ on each page
QString normalizeRepeatedText(const QString& text)
{
    static const QRegularExpression digits("\\d+");
    static const QRegularExpression spaces("\\s+");
    QString result = text;
    result.replace(digits, "#");
    result.replace(spaces, " ");
    return result.trimmed();
}

/// Turns a header/footer line into a paragraph; digits equal to the page number become
/// a page number field (and the number of pages a page count field)
OdtParagraph createRepeatedParagraph(const LineInfo& line,
                                     int pageNumber,
                                     int pageCount,
                                     const DocumentMetrics& metrics,
                                     const std::vector<OdtCharacterStyle>& styles)
{
    OdtParagraph paragraph;
    paragraph.alignment = getAlignment({ &line }, metrics);
    if (paragraph.alignment == OdtAlignment::Left)
    {
        paragraph.marginLeft = qMax(0.0, line.left - metrics.bodyLeft);
    }

    QString text;
    std::vector<int> characterStyles;
    std::vector<double> characterSizes;
    createParagraphText({ &line }, false, text, characterStyles, characterSizes);
    paragraph.spans = createSpans(text, characterStyles, characterSizes, styles);
    if (paragraph.alignment == OdtAlignment::Left)
    {
        paragraph.tabStops = getTabStops({ &line }, metrics.bodyLeft + paragraph.marginLeft);
    }

    // Split the spans at the page numbers
    static const QRegularExpression digits("\\d+");
    std::vector<OdtSpan> result;
    for (const OdtSpan& span : paragraph.spans)
    {
        qsizetype position = 0;
        QRegularExpressionMatchIterator it = digits.globalMatch(span.text);
        while (it.hasNext())
        {
            QRegularExpressionMatch match = it.next();
            const int number = match.captured().toInt();
            OdtSpan::Kind kind = OdtSpan::Kind::Text;
            if (number == pageNumber)
            {
                kind = OdtSpan::Kind::PageNumber;
            }
            else if (number == pageCount && pageCount > 0)
            {
                kind = OdtSpan::Kind::PageCount;
            }

            if (kind != OdtSpan::Kind::Text)
            {
                if (match.capturedStart() > position)
                {
                    result.push_back(OdtSpan{ span.text.mid(position, match.capturedStart() - position), span.style, OdtSpan::Kind::Text });
                }
                result.push_back(OdtSpan{ match.captured(), span.style, kind });
                position = match.capturedEnd();
            }
        }
        if (position < span.text.size())
        {
            result.push_back(OdtSpan{ span.text.mid(position), span.style, OdtSpan::Kind::Text });
        }
    }
    paragraph.spans = std::move(result);
    return paragraph;
}

}   // namespace

double OdtLayoutLine::right() const
{
    double result = 0.0;
    for (const OdtLayoutCharacter& character : characters)
    {
        result = qMax(result, character.right);
    }
    return result;
}

double OdtLayoutLine::baseline() const
{
    std::vector<double> baselines;
    baselines.reserve(characters.size());
    for (const OdtLayoutCharacter& character : characters)
    {
        baselines.push_back(character.baseline);
    }
    return percentile(baselines, 0.5);
}

double OdtLayoutLine::fontSize() const
{
    // The most common size (by characters) - a line with one big initial letter is not big
    std::map<int, int> counts;
    for (const OdtLayoutCharacter& character : characters)
    {
        if (!character.character.isSpace())
        {
            ++counts[qRound(character.fontSize * 2.0)];
        }
    }
    if (counts.empty())
    {
        return characters.empty() ? DEFAULT_FONT_SIZE : characters.front().fontSize;
    }
    return std::max_element(counts.begin(), counts.end(), [](const auto& l, const auto& r) { return l.second < r.second; })->first / 2.0;
}

OdtLayoutBuilder::LineText OdtLayoutBuilder::getLineText(const OdtLayoutLine& line)
{
    LineText result;
    const OdtLayoutCharacter* previous = nullptr;

    for (const OdtLayoutCharacter& character : line.characters)
    {
        if (previous)
        {
            const double size = safeSize(qMin(previous->fontSize, character.fontSize));

            // The same glyph again at (almost) the same place - "fake bold" made by
            // printing the text twice with a small offset
            if (character.character == previous->character && qAbs(character.x - previous->x) < 0.15 * size)
            {
                continue;
            }

            const double gap = character.x - previous->right;
            QChar separator;
            if (gap > 3.0 * size)
            {
                separator = QChar('\t');
            }
            else if (gap > 0.13 * size && !character.character.isSpace() && !previous->character.isSpace())
            {
                separator = QChar(' ');
            }

            if (separator == QChar('\t'))
            {
                result.tabPositions.push_back(character.x);
            }

            if (!separator.isNull())
            {
                result.text += separator;
                result.styles.push_back(previous->style);
                result.sizes.push_back(previous->fontSize);
            }
        }

        result.text += character.character;
        result.styles.push_back(character.style);
        result.sizes.push_back(character.fontSize);
        previous = &character;
    }

    return result;
}

bool OdtLayoutBuilder::startsWithBullet(const QString& text, QChar* bullet)
{
    if (text.size() < 3)
    {
        return false;
    }

    static const QString strongBullets = QString::fromUtf8("•●◦○▪■□‣⁃∙➢➤►✓✔◆◇❖") + QChar(0xF0B7) + QChar(0xF0A7) + QChar(0xF076) + QChar(0xF0D8) + QChar(0xF0FC) + QChar(0xF06E);
    static const QString weakBullets = QString::fromUtf8("-–—*·");

    const QChar first = text.front();
    const bool followedBySpace = text[1].isSpace();

    if (strongBullets.contains(first) && !text.mid(1).trimmed().isEmpty())
    {
        if (bullet)
        {
            // Symbol-font bullets (private use area) are written as the ordinary bullet
            *bullet = first.unicode() >= 0xF000 ? QChar(0x2022) : first;
        }
        return true;
    }

    if (weakBullets.contains(first) && followedBySpace && !text.mid(2).trimmed().isEmpty())
    {
        if (bullet)
        {
            *bullet = first;
        }
        return true;
    }

    return false;
}

void OdtLayoutBuilder::mergeLinesOnBaseline(std::vector<OdtLayoutLine>& lines)
{
    static const QRegularExpression numberMarker("^\\(?[0-9A-Za-z]{1,3}[.)]$");

    bool changed = true;
    while (changed)
    {
        changed = false;

        // 1) A lone list marker ("•", "1.", "a)") - attach it to the text right of it
        for (size_t i = 0; i < lines.size() && !changed; ++i)
        {
            const OdtLayoutLine& marker = lines[i];
            if (marker.rotated || marker.characters.empty())
            {
                continue;
            }

            const QString text = getLineText(marker).text.trimmed();
            const bool isMarker = text.size() <= 4 && (startsWithBullet(text + QString(" x"), nullptr) || numberMarker.match(text).hasMatch());
            if (!isMarker)
            {
                continue;
            }

            const double markerSize = safeSize(marker.fontSize());
            const double markerRight = marker.right();
            const double markerBaseline = marker.baseline();

            size_t best = lines.size();
            double bestGap = 6.0 * markerSize;
            for (size_t j = 0; j < lines.size(); ++j)
            {
                const OdtLayoutLine& candidate = lines[j];
                if (j == i || candidate.rotated || candidate.characters.empty())
                {
                    continue;
                }

                const double size = qMin(markerSize, safeSize(candidate.fontSize()));
                const double gap = candidate.left() - markerRight;
                if (qAbs(candidate.baseline() - markerBaseline) < 0.35 * size && gap > -0.5 && gap < bestGap)
                {
                    best = j;
                    bestGap = gap;
                }
            }

            if (best < lines.size())
            {
                OdtLayoutLine& target = lines[best];
                target.characters.insert(target.characters.begin(), marker.characters.begin(), marker.characters.end());
                lines.erase(lines.begin() + i);
                changed = true;
            }
        }

        // 2) Two one-line blocks next to each other on the same baseline - one line
        if (!changed)
        {
            std::map<int, int> linesInBlock;
            for (const OdtLayoutLine& line : lines)
            {
                ++linesInBlock[line.block];
            }

            for (size_t i = 0; i + 1 < lines.size() && !changed; ++i)
            {
                OdtLayoutLine& first = lines[i];
                const OdtLayoutLine& second = lines[i + 1];
                if (first.rotated || second.rotated || first.characters.empty() || second.characters.empty() ||
                    first.block == second.block || linesInBlock[first.block] != 1 || linesInBlock[second.block] != 1)
                {
                    continue;
                }

                const double size1 = safeSize(first.fontSize());
                const double size2 = safeSize(second.fontSize());
                if (qMax(size1, size2) / qMin(size1, size2) > 1.3)
                {
                    continue;
                }

                if (qAbs(first.baseline() - second.baseline()) < 0.35 * qMin(size1, size2) && second.left() >= first.right() - 0.5)
                {
                    first.characters.insert(first.characters.end(), second.characters.begin(), second.characters.end());
                    lines.erase(lines.begin() + i + 1);
                    changed = true;
                }
            }
        }
    }
}

OdtDocument OdtLayoutBuilder::build(std::vector<OdtLayoutPage> pages,
                                    const std::vector<OdtCharacterStyle>& characterStyles,
                                    std::vector<OdtImage> images,
                                    const Settings& settings)
{
    OdtDocument document;
    document.title = settings.title;
    document.images = std::move(images);

    for (OdtLayoutPage& page : pages)
    {
        mergeLinesOnBaseline(page.lines);
    }

    // Line information of all pages
    std::vector<std::vector<LineInfo>> pageLines(pages.size());
    for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
    {
        for (const OdtLayoutLine& line : pages[pageIndex].lines)
        {
            if (!line.characters.empty())
            {
                pageLines[pageIndex].push_back(createLineInfo(line, characterStyles));
            }
        }
    }

    // Words of the document, for the hyphens at line ends. The first word of a line and the
    // last one of a hyphenated line are left out - they may be the parts of a split word.
    WordSet documentWords;
    {
        static const QRegularExpression wordExpression("\\p{L}+");
        for (const std::vector<LineInfo>& lines : pageLines)
        {
            for (const LineInfo& line : lines)
            {
                const QString& text = line.text.text;
                const bool hyphenated = !text.isEmpty() && isHyphen(text.back());
                std::vector<QString> lineWords;
                QRegularExpressionMatchIterator it = wordExpression.globalMatch(text);
                while (it.hasNext())
                {
                    lineWords.push_back(it.next().captured().toLower());
                }
                for (size_t i = 1; i < lineWords.size(); ++i)
                {
                    if (!(hyphenated && i + 1 == lineWords.size()))
                    {
                        documentWords.insert(lineWords[i]);
                    }
                }
            }
        }
    }

    // Body text size = the most common size (by characters)
    DocumentMetrics metrics;
    {
        std::map<int, int> sizeCounts;
        std::map<int, std::map<QString, int>> familyCounts;
        for (const std::vector<LineInfo>& lines : pageLines)
        {
            for (const LineInfo& line : lines)
            {
                if (line.rotated)
                {
                    continue;
                }
                for (const OdtLayoutCharacter& character : line.line->characters)
                {
                    const int size = qRound(character.fontSize * 2.0);
                    ++sizeCounts[size];
                    if (character.style >= 0 && character.style < int(characterStyles.size()))
                    {
                        ++familyCounts[size][characterStyles[character.style].fontFamily];
                    }
                }
            }
        }

        if (!sizeCounts.empty())
        {
            const int bodySize = std::max_element(sizeCounts.begin(), sizeCounts.end(), [](const auto& l, const auto& r) { return l.second < r.second; })->first;
            metrics.bodySize = safeSize(bodySize / 2.0);

            const std::map<QString, int>& families = familyCounts[bodySize];
            if (!families.empty())
            {
                document.bodyStyle.text.fontFamily = std::max_element(families.begin(), families.end(), [](const auto& l, const auto& r) { return l.second < r.second; })->first;
            }
        }
        document.bodyStyle.text.fontSize = metrics.bodySize;
        document.bodyStyle.used = true;

        // Usual distance of the lines of a paragraph, for each font size. The lower
        // part of the distribution is taken: the distances between paragraphs are larger.
        std::map<int, std::vector<double>> distances;
        for (const std::vector<LineInfo>& lines : pageLines)
        {
            for (size_t i = 1; i < lines.size(); ++i)
            {
                const LineInfo& previous = lines[i - 1];
                const LineInfo& current = lines[i];
                const double distance = current.baseline - previous.baseline;
                if (previous.block == current.block && !current.rotated && qRound(previous.size * 2.0) == qRound(current.size * 2.0) &&
                    distance > 0.5 * current.size && distance < 3.0 * current.size)
                {
                    distances[qRound(current.size * 2.0)].push_back(distance);
                }
            }
        }
        for (const auto& [size, values] : distances)
        {
            // The median of the distances near the smallest one: the line distance itself,
            // without the paragraph gaps and without a bias (an error of 0.1 pt per line
            // makes a full page overflow)
            const double smallest = *std::min_element(values.begin(), values.end());
            std::vector<double> lineDistances;
            std::copy_if(values.begin(), values.end(), std::back_inserter(lineDistances), [smallest](double value) { return value < smallest * 1.15; });
            metrics.pitches[size] = percentile(lineDistances, 0.5);
        }
    }

    // Repeated headers and footers: the same text at the same place on most of the pages
    // (page numbers differ - digits are ignored). They are moved into the page header and
    // footer of the word processor document: there they repeat on every page, exactly as
    // in the PDF, and they do not interrupt the text, which flows from page to page.
    std::set<std::pair<size_t, size_t>> removedLines;
    std::set<std::pair<size_t, size_t>> removedImages;
    std::vector<std::pair<size_t, size_t>> headerLines;
    std::vector<std::pair<size_t, size_t>> footerLines;
    std::set<int> runningPageNumberZones;

    const size_t threshold = qMax<size_t>(2, size_t(std::ceil(0.6 * double(pages.size()))));
    if (settings.moveHeadersAndFooters && pages.size() >= 2)
    {
        struct Occurrences
        {
            std::vector<std::pair<size_t, size_t>> items;
            std::set<size_t> pages;
            double minBaseline = std::numeric_limits<double>::max();
            double maxBaseline = std::numeric_limits<double>::lowest();
        };

        std::map<std::pair<QString, int>, Occurrences> candidates;
        for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
        {
            const double height = pages[pageIndex].height;
            for (size_t lineIndex = 0; lineIndex < pageLines[pageIndex].size(); ++lineIndex)
            {
                const LineInfo& line = pageLines[pageIndex][lineIndex];
                int zone = 0;
                if (line.baseline < 0.14 * height)
                {
                    zone = 1;
                }
                else if (line.baseline > 0.88 * height)
                {
                    zone = 2;
                }

                if (zone == 0 || line.rotated)
                {
                    continue;
                }

                const QString key = normalizeRepeatedText(line.text.text);
                if (key.isEmpty())
                {
                    continue;
                }

                Occurrences& occurrences = candidates[std::make_pair(key, zone)];
                occurrences.items.emplace_back(pageIndex, lineIndex);
                occurrences.pages.insert(pageIndex);
                occurrences.minBaseline = qMin(occurrences.minBaseline, line.baseline);
                occurrences.maxBaseline = qMax(occurrences.maxBaseline, line.baseline);
            }
        }

        for (const auto& [key, occurrences] : candidates)
        {
            // Only the occurrences at the usual place count (a title page may have the
            // same text elsewhere - it stays in the body there)
            std::vector<double> baselines;
            for (const auto& item : occurrences.items)
            {
                baselines.push_back(pageLines[item.first][item.second].baseline);
            }
            const double usualBaseline = percentile(baselines, 0.5);

            std::vector<std::pair<size_t, size_t>> items;
            std::set<size_t> itemPages;
            for (const auto& item : occurrences.items)
            {
                if (qAbs(pageLines[item.first][item.second].baseline - usualBaseline) <= 4.0)
                {
                    items.push_back(item);
                    itemPages.insert(item.first);
                }
            }

            if (itemPages.size() >= threshold)
            {
                removedLines.insert(items.begin(), items.end());
                (key.second == 1 ? headerLines : footerLines).push_back(items.front());
            }
        }

        // Running headers which differ from page to page ("Chapter 2: Syntax ... 14"), but
        // carry the number of their page. They are left out of the body (they would be in
        // the middle of the text); a page number field takes their place. The printed number
        // may differ from the page index by a constant (front matter numbered separately) -
        // the most common difference is found first. Books often have the number in the
        // header, but in the footer of the first page of a chapter - both zones count.
        {
            static const QRegularExpression digits("\\d+");
            struct ZoneLine
            {
                size_t page = 0;
                size_t line = 0;
                int zone = 0;
                std::vector<int> numbers;
            };
            std::vector<ZoneLine> zoneLines;
            std::map<int, std::set<size_t>> offsetPages;
            for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
            {
                const double height = pages[pageIndex].height;
                for (size_t lineIndex = 0; lineIndex < pageLines[pageIndex].size(); ++lineIndex)
                {
                    const LineInfo& line = pageLines[pageIndex][lineIndex];
                    const int zone = line.baseline < 0.14 * height ? 1 : (line.baseline > 0.88 * height ? 2 : 0);
                    if (zone == 0 || line.rotated || removedLines.count(std::make_pair(pageIndex, lineIndex)) || line.text.text.size() > 100)
                    {
                        continue;
                    }

                    ZoneLine zoneLine{ pageIndex, lineIndex, zone, { } };
                    QRegularExpressionMatchIterator it = digits.globalMatch(line.text.text);
                    while (it.hasNext())
                    {
                        const QString number = it.next().captured();
                        if (number.size() <= 5)
                        {
                            zoneLine.numbers.push_back(number.toInt());
                            offsetPages[number.toInt() - pages[pageIndex].pageNumber].insert(pageIndex);
                        }
                    }
                    if (!zoneLine.numbers.empty())
                    {
                        zoneLines.push_back(std::move(zoneLine));
                    }
                }
            }

            auto best = std::max_element(offsetPages.begin(), offsetPages.end(), [](const auto& l, const auto& r) { return l.second.size() < r.second.size(); });
            if (best != offsetPages.end() && best->second.size() >= threshold)
            {
                const int offset = best->first;
                int zoneCounts[3] = { 0, 0, 0 };
                std::pair<size_t, size_t> firstItem[3];
                for (const ZoneLine& zoneLine : zoneLines)
                {
                    const int printedNumber = pages[zoneLine.page].pageNumber + offset;
                    if (std::find(zoneLine.numbers.begin(), zoneLine.numbers.end(), printedNumber) != zoneLine.numbers.end())
                    {
                        removedLines.emplace(zoneLine.page, zoneLine.line);
                        if (zoneCounts[zoneLine.zone]++ == 0)
                        {
                            firstItem[zoneLine.zone] = std::make_pair(zoneLine.page, zoneLine.line);
                        }
                    }
                }

                const int zone = zoneCounts[1] >= zoneCounts[2] ? 1 : 2;
                std::vector<std::pair<size_t, size_t>>& repeatedLines = zone == 1 ? headerLines : footerLines;
                if (repeatedLines.empty())
                {
                    // The line gives the place of the header/footer; its paragraph is a page number
                    runningPageNumberZones.insert(zone);
                    repeatedLines.push_back(firstItem[zone]);
                }
            }
        }

        // Repeated images at the top or bottom of the pages (a letterhead logo)
        std::map<std::tuple<int, int, int, int, int>, std::vector<std::pair<size_t, size_t>>> imageCandidates;
        for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
        {
            const OdtLayoutPage& page = pages[pageIndex];
            for (size_t imageIndex = 0; imageIndex < page.images.size(); ++imageIndex)
            {
                const OdtLayoutImage& image = page.images[imageIndex];
                if (image.rect.top() < 0.2 * page.height || image.rect.bottom() > 0.8 * page.height)
                {
                    const auto key = std::make_tuple(image.imageIndex, qRound(image.rect.x() / 4.0), qRound(image.rect.y() / 4.0), qRound(image.rect.width() / 4.0), qRound(image.rect.height() / 4.0));
                    imageCandidates[key].emplace_back(pageIndex, imageIndex);
                }
            }
        }
        for (const auto& [key, items] : imageCandidates)
        {
            std::set<size_t> imagePages;
            for (const auto& item : items)
            {
                imagePages.insert(item.first);
            }
            if (imagePages.size() >= threshold)
            {
                removedImages.insert(items.begin(), items.end());
                const OdtLayoutImage& image = pages[items.front().first].images[items.front().second];
                OdtPageImage pageImage;
                pageImage.imageIndex = image.imageIndex;
                pageImage.x = image.rect.x();
                pageImage.y = image.rect.y();
                pageImage.width = image.rect.width();
                pageImage.height = image.rect.height();
                document.headerImages.push_back(pageImage);
            }
        }
    }

    // Images, which are not exported: full page images under text (a scanned page with an
    // OCR text layer, a page background) - the text is there, the picture would only
    // duplicate it and push it to the next page
    for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
    {
        const OdtLayoutPage& page = pages[pageIndex];
        const double pageArea = page.width * page.height;
        for (size_t imageIndex = 0; imageIndex < page.images.size(); ++imageIndex)
        {
            const QRectF rect = page.images[imageIndex].rect.intersected(QRectF(0, 0, page.width, page.height));
            const bool hasText = !pageLines[pageIndex].empty();
            if (hasText && rect.width() * rect.height() > 0.8 * pageArea)
            {
                removedImages.emplace(pageIndex, imageIndex);
            }
        }
    }

    // Area of the body text (all pages together)
    {
        double left = std::numeric_limits<double>::max();
        double right = std::numeric_limits<double>::lowest();
        double top = std::numeric_limits<double>::max();
        double bottom = std::numeric_limits<double>::lowest();
        std::vector<double> lineRights;

        for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
        {
            for (size_t lineIndex = 0; lineIndex < pageLines[pageIndex].size(); ++lineIndex)
            {
                const LineInfo& line = pageLines[pageIndex][lineIndex];
                if (line.rotated || removedLines.count(std::make_pair(pageIndex, lineIndex)))
                {
                    continue;
                }
                left = qMin(left, line.left);
                lineRights.push_back(line.right);
                top = qMin(top, line.top());
                bottom = qMax(bottom, line.bottom());
            }

            const OdtLayoutPage& page = pages[pageIndex];
            for (size_t imageIndex = 0; imageIndex < page.images.size(); ++imageIndex)
            {
                if (removedImages.count(std::make_pair(pageIndex, imageIndex)))
                {
                    continue;
                }
                const QRectF rect = page.images[imageIndex].rect.intersected(QRectF(0, 0, page.width, page.height));
                left = qMin(left, rect.left());
                right = qMax(right, rect.right());
                top = qMin(top, rect.top());
                bottom = qMax(bottom, rect.bottom());
            }
        }

        // The right edge of the text: a few lines sticking out (a long URL, a line of
        // code running into the margin) do not make the margin of the whole document
        if (!lineRights.empty())
        {
            right = qMax(right, percentile(lineRights, 0.98));
        }

        const OdtLayoutPage* firstPage = pages.empty() ? nullptr : &pages.front();
        const double width = firstPage ? firstPage->width : 612.0;
        const double height = firstPage ? firstPage->height : 792.0;
        if (left > right || top > bottom)
        {
            left = qMin(72.0, width / 4);
            right = width - left;
            top = qMin(72.0, height / 4);
            bottom = height - top;
        }

        metrics.bodyLeft = left;
        metrics.bodyRight = right;
        metrics.bodyTop = top;
        metrics.bodyBottom = bottom;

        // Page layout of the first page. A little slack is added on the right and at the
        // bottom: the word processor measures text with its own fonts, a line which just
        // fitted in the PDF must not wrap, a full page must not overflow to the next one.
        OdtPageLayout& layout = document.page;
        layout.width = width;
        layout.height = height;
        const double minimalMargin = 9.0;
        const double slack = 1.2 * metrics.bodySize;
        layout.marginLeft = qBound(minimalMargin, left, width / 3);
        layout.marginRight = qBound(minimalMargin, width - right - slack, width / 3);
        layout.marginTop = qBound(minimalMargin, top, height / 3);
        layout.marginBottom = qBound(minimalMargin, height - bottom - slack, height / 3);

        // Header and footer between the page edge and the body
        double headerTop = std::numeric_limits<double>::max();
        for (const auto& item : headerLines)
        {
            headerTop = qMin(headerTop, pageLines[item.first][item.second].top());
        }
        if (!headerLines.empty() || !document.headerImages.empty())
        {
            const double marginTop = qBound(minimalMargin, qMin(headerTop, top) - 1.0, layout.marginTop);
            layout.headerHeight = qMax(1.0, layout.marginTop - marginTop);
            layout.marginTop = marginTop;
        }

        double footerBottom = std::numeric_limits<double>::lowest();
        for (const auto& item : footerLines)
        {
            footerBottom = qMax(footerBottom, pageLines[item.first][item.second].bottom());
        }
        if (!footerLines.empty())
        {
            const double marginBottom = qBound(minimalMargin, height - footerBottom - 1.0, layout.marginBottom);
            layout.footerHeight = qMax(1.0, layout.marginBottom - marginBottom);
            layout.marginBottom = marginBottom;
        }
    }

    // Header and footer paragraphs, ordered from top to bottom
    auto createRepeated = [&](std::vector<std::pair<size_t, size_t>> items, std::vector<OdtParagraph>& paragraphs)
    {
        std::sort(items.begin(), items.end(), [&](const auto& l, const auto& r)
        {
            const LineInfo& a = pageLines[l.first][l.second];
            const LineInfo& b = pageLines[r.first][r.second];
            return std::make_pair(qRound(a.baseline), a.left) < std::make_pair(qRound(b.baseline), b.left);
        });

        for (const auto& item : items)
        {
            const OdtLayoutPage& page = pages[item.first];
            paragraphs.push_back(createRepeatedParagraph(pageLines[item.first][item.second], page.pageNumber, settings.documentPageCount, metrics, characterStyles));
        }
    };
    createRepeated(headerLines, document.header);
    createRepeated(footerLines, document.footer);

    // Running headers/footers of varying text - only the page number is kept
    for (int zone : runningPageNumberZones)
    {
        std::vector<OdtParagraph>& paragraphs = zone == 1 ? document.header : document.footer;
        const std::pair<size_t, size_t> item = zone == 1 ? headerLines.front() : footerLines.front();
        const LineInfo& line = pageLines[item.first][item.second];

        OdtParagraph paragraph;
        paragraph.alignment = OdtAlignment::Center;
        OdtSpan span;
        span.kind = OdtSpan::Kind::PageNumber;
        span.text = QString::number(pages[item.first].pageNumber);
        span.style = createTextStyle(line.dominantStyle, line.size, characterStyles);
        paragraph.spans.push_back(span);
        paragraphs.clear();
        paragraphs.push_back(paragraph);
    }

    // The body - paragraphs of each page, images between them
    std::vector<PageItem> allItems;
    for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
    {
        const std::vector<LineInfo>& lines = pageLines[pageIndex];
        const OdtLayoutPage& page = pages[pageIndex];

        std::vector<const LineInfo*> bodyLines;
        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
        {
            if (!removedLines.count(std::make_pair(pageIndex, lineIndex)))
            {
                bodyLines.push_back(&lines[lineIndex]);
            }
        }

        // Right edge of each block - lines ending well before it end their paragraph
        std::map<int, double> blockRight;
        for (const LineInfo* line : bodyLines)
        {
            double& value = blockRight[line->block];
            value = qMax(value, line->right);
        }

        std::vector<ParagraphLines> paragraphs;
        for (const LineInfo* line : bodyLines)
        {
            if (paragraphs.empty() || isParagraphBreak(*paragraphs.back().lines.back(), *line, paragraphs.back().lines.size(), blockRight[line->block], metrics))
            {
                paragraphs.emplace_back();
            }
            paragraphs.back().lines.push_back(line);
        }

        std::vector<PageItem> items;
        for (const ParagraphLines& paragraphLines : paragraphs)
        {
            const std::vector<const LineInfo*>& paragraphLineList = paragraphLines.lines;
            const LineInfo* first = paragraphLineList.front();
            const LineInfo* last = paragraphLineList.back();

            PageItem item;
            item.rotated = first->rotated;
            item.top = first->top();
            item.bottom = last->bottom();
            item.firstBaseline = first->baseline;
            item.lastBaseline = last->baseline;
            item.firstSize = first->size;
            item.lastSize = last->size;
            item.lineCount = paragraphLineList.size();

            OdtParagraph& paragraph = item.paragraph;
            paragraph.listBullet = first->bullet;
            paragraph.alignment = getAlignment(paragraphLineList, metrics);

            QString text;
            std::vector<int> styles;
            std::vector<double> sizes;
            createParagraphText(paragraphLineList, !paragraph.listBullet.isNull(), text, styles, sizes, &documentWords);
            paragraph.spans = createSpans(text, styles, sizes, characterStyles);

            // Dominant size and boldness, for the headings
            std::map<int, int> sizeCounts;
            int boldCount = 0;
            int count = 0;
            for (qsizetype i = 0; i < text.size(); ++i)
            {
                if (text[i].isSpace())
                {
                    continue;
                }
                ++count;
                ++sizeCounts[qRound(sizes[i] * 2.0)];
                if (styles[i] >= 0 && styles[i] < int(characterStyles.size()) && characterStyles[styles[i]].bold)
                {
                    ++boldCount;
                }
            }
            item.boldFraction = count > 0 ? double(boldCount) / count : 0.0;
            item.dominantSize = sizeCounts.empty() ? first->size : std::max_element(sizeCounts.begin(), sizeCounts.end(), [](const auto& l, const auto& r) { return l.second < r.second; })->first / 2.0;

            // Indentation, relative to the left margin
            if (!item.rotated && (paragraph.alignment == OdtAlignment::Left || paragraph.alignment == OdtAlignment::Justify))
            {
                if (!paragraph.listBullet.isNull())
                {
                    paragraph.marginLeft = qMax(0.0, first->textLeft - metrics.bodyLeft);
                    paragraph.textIndent = qMin(0.0, first->left - first->textLeft);
                }
                else
                {
                    double otherLeft = first->left;
                    if (paragraphLineList.size() > 1)
                    {
                        otherLeft = std::numeric_limits<double>::max();
                        for (size_t i = 1; i < paragraphLineList.size(); ++i)
                        {
                            otherLeft = qMin(otherLeft, paragraphLineList[i]->left);
                        }
                    }

                    paragraph.marginLeft = otherLeft - metrics.bodyLeft;
                    paragraph.textIndent = first->left - otherLeft;
                    if (paragraph.marginLeft < 0.4 * first->size)
                    {
                        // Within the noise of the glyph side bearings
                        paragraph.textIndent += paragraph.marginLeft;
                        paragraph.marginLeft = 0.0;
                    }
                    if (qAbs(paragraph.textIndent) < 0.4 * first->size)
                    {
                        paragraph.textIndent = 0.0;
                    }
                    paragraph.marginLeft = qMax(0.0, paragraph.marginLeft);
                }

                paragraph.tabStops = getTabStops(paragraphLineList, metrics.bodyLeft + paragraph.marginLeft + qMin(0.0, paragraph.textIndent));
            }

            // The lines keep their distance from the PDF (exactly) - the word processor
            // font may have a larger natural line height, and the pages would overflow
            if (!item.rotated && metrics.pitches.count(qRound(item.firstSize * 2.0)))
            {
                paragraph.lineHeight = metrics.getPitch(item.firstSize);
            }

            items.push_back(std::move(item));
        }

        // Images between the paragraphs, by their vertical position
        std::vector<const OdtLayoutImage*> pageImages;
        for (size_t imageIndex = 0; imageIndex < page.images.size(); ++imageIndex)
        {
            if (!removedImages.count(std::make_pair(pageIndex, imageIndex)))
            {
                pageImages.push_back(&page.images[imageIndex]);
            }
        }
        std::sort(pageImages.begin(), pageImages.end(), [](const OdtLayoutImage* l, const OdtLayoutImage* r) { return l->rect.top() < r->rect.top(); });

        const double bodyWidth = qMax(36.0, document.page.width - document.page.marginLeft - document.page.marginRight);
        for (const OdtLayoutImage* image : pageImages)
        {
            PageItem item;
            item.isImage = true;
            item.top = image->rect.top();
            item.bottom = image->rect.bottom();

            OdtParagraph& paragraph = item.paragraph;
            paragraph.imageIndex = image->imageIndex;
            paragraph.imageWidth = image->rect.width();
            paragraph.imageHeight = image->rect.height();

            // An image wider than the text area is made smaller (aspect ratio kept)
            if (paragraph.imageWidth > bodyWidth)
            {
                const double scale = bodyWidth / paragraph.imageWidth;
                paragraph.imageWidth *= scale;
                paragraph.imageHeight *= scale;
            }
            const double maxHeight = qMax(36.0, document.page.height - document.page.marginTop - document.page.marginBottom - document.page.headerHeight - document.page.footerHeight - 2.0 * metrics.bodySize);
            if (paragraph.imageHeight > maxHeight)
            {
                const double scale = maxHeight / paragraph.imageHeight;
                paragraph.imageWidth *= scale;
                paragraph.imageHeight *= scale;
            }

            const double center = image->rect.center().x();
            const double bodyCenter = (metrics.bodyLeft + metrics.bodyRight) / 2.0;
            if (qAbs(center - bodyCenter) < 0.05 * (metrics.bodyRight - metrics.bodyLeft))
            {
                paragraph.alignment = OdtAlignment::Center;
            }
            else if (metrics.bodyRight - image->rect.right() < 6.0 && image->rect.left() > bodyCenter)
            {
                paragraph.alignment = OdtAlignment::Right;
            }
            else
            {
                paragraph.marginLeft = qMax(0.0, qMin(image->rect.left() - metrics.bodyLeft, bodyWidth - paragraph.imageWidth));
            }

            auto it = std::find_if(items.begin(), items.end(), [image](const PageItem& other) { return !other.isImage && !other.rotated && other.top > image->rect.top() - 1.0; });
            items.insert(it, std::move(item));
        }

        // Vertical spacing - the space above each element, as in the PDF
        for (size_t i = 0; i < items.size(); ++i)
        {
            PageItem& item = items[i];
            double spaceBefore = 0.0;
            if (item.rotated)
            {
                spaceBefore = 0.0;
            }
            else if (i == 0)
            {
                spaceBefore = item.top - metrics.bodyTop;
            }
            else
            {
                const PageItem& previous = items[i - 1];
                if (previous.rotated)
                {
                    spaceBefore = 0.0;
                }
                else
                {
                    // Where the word processor puts the lines: a line of an exact height has
                    // its baseline at (height - descent) from its top, a line of a natural
                    // height at about the ascent of the font (Calibri/Carlito 0.95)
                    auto getBaselineOffset = [](const PageItem& element)
                    {
                        return element.paragraph.lineHeight > 0.0 ? element.paragraph.lineHeight - DESCENT * element.firstSize : 0.95 * element.firstSize;
                    };

                    const double previousBottom = previous.isImage ? previous.bottom : previous.lastBaseline + DESCENT * previous.lastSize;
                    const double currentTop = item.isImage ? item.top : item.firstBaseline - getBaselineOffset(item);
                    spaceBefore = currentTop - previousBottom;
                }
            }

            // Next column starts higher - no space; very large gaps are shortened
            item.paragraph.spaceBefore = std::round(qBound(0.0, spaceBefore, 72.0) * 10.0) / 10.0;
        }

        if (items.empty())
        {
            // An empty page stays a page
            PageItem item;
            items.push_back(std::move(item));
        }

        if (pageIndex > 0)
        {
            items.front().paragraph.pageBreakBefore = true;
        }

        allItems.insert(allItems.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
    }

    // Headings: short paragraphs in a font larger than the body text. The largest size is
    // "Heading 1", the next one "Heading 2" and so on.
    // An entry of a table of contents ("Article 2 - Fire Station..... 4") is in the size of
    // the heading it points to, but it is not a heading.
    static const QRegularExpression tableOfContentsEntry("(\\.\\s?\\.|\u2026|_{3,}|\\t)\\s*\\d+\\s*$");
    auto isHeadingCandidate = [&metrics](const PageItem& item)
    {
        const QString text = item.paragraph.getPlainText().trimmed();
        return !item.isImage && !item.rotated && !text.isEmpty() && text.size() <= 200 && item.lineCount <= 3 &&
               item.dominantSize >= metrics.bodySize * 1.15 && !tableOfContentsEntry.match(text).hasMatch();
    };

    std::vector<int> headingSizes;
    for (const PageItem& item : allItems)
    {
        if (isHeadingCandidate(item))
        {
            headingSizes.push_back(qRound(item.dominantSize * 2.0));
        }
    }
    std::sort(headingSizes.begin(), headingSizes.end(), std::greater<int>());
    headingSizes.erase(std::unique(headingSizes.begin(), headingSizes.end()), headingSizes.end());

    // Sizes within a point are one level (a 15.5 pt and a 16 pt title)
    std::vector<std::pair<int, int>> sizeLevels;
    int level = 0;
    int levelSize = 0;
    for (int size : headingSizes)
    {
        if (level == 0 || levelSize - size > 2)
        {
            level = qMin(level + 1, 3);
            levelSize = size;
        }
        sizeLevels.emplace_back(size, level);
    }

    for (PageItem& item : allItems)
    {
        if (!isHeadingCandidate(item))
        {
            continue;
        }

        const int size = qRound(item.dominantSize * 2.0);
        auto it = std::find_if(sizeLevels.begin(), sizeLevels.end(), [size](const auto& sizeLevel) { return sizeLevel.first == size; });
        if (it == sizeLevels.end())
        {
            continue;
        }

        const int headingLevel = it->second;
        item.paragraph.headingLevel = headingLevel;
        item.paragraph.listBullet = QChar();

        // The style of the heading level - from its first paragraph
        OdtNamedStyle& style = document.headingStyles[headingLevel - 1];
        if (!style.used)
        {
            std::map<OdtTextStyle, int> styleCounts;
            for (const OdtSpan& span : item.paragraph.spans)
            {
                styleCounts[span.style] += int(span.text.size());
            }
            if (!styleCounts.empty())
            {
                style.text = std::max_element(styleCounts.begin(), styleCounts.end(), [](const auto& l, const auto& r) { return l.second < r.second; })->first;
            }
            style.used = true;
        }
    }

    // Levels without any heading still get a sensible style
    for (int i = 0; i < 3; ++i)
    {
        OdtNamedStyle& style = document.headingStyles[i];
        if (!style.used)
        {
            style.text = document.bodyStyle.text;
            style.text.bold = true;
            style.text.fontSize = roundToHalf(metrics.bodySize * (1.6 - 0.2 * i));
        }
    }

    for (PageItem& item : allItems)
    {
        document.body.push_back(std::move(item.paragraph));
    }

    return document;
}

}   // namespace pdfplugin
