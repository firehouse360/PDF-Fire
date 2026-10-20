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

// Ported from PdfCraft crates/audit/src/cover.rs and lib.rs (MIT OR Apache-2.0):
// the rule of a cover (painted after the text, opaque, over the text, contrast
// below 3), and the WCAG contrast. The content is walked by the content processor
// of PDF Fire, so the graphics state, forms and color spaces are those of rendering.

#include "pdffirecovercheck.h"
#include "pdfcms.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"
#include "pdfpagecontentprocessor.h"
#include "pdftextlayout.h"

#include <QMutex>

#include <cmath>
#include <limits>

#include "pdfdbgheap.h"

namespace pdf
{

namespace
{

/// Part of a character's box, which must be under a cover to count as hidden
constexpr PDFReal HiddenCharacterFraction = 0.5;

/// WCAG contrast ratio, below which the text is unreadable against the cover
constexpr PDFReal CoverContrast = 3.0;

struct DrawnCharacter
{
    QString text;
    QRectF box;         ///< Box of the glyph (its ink)
    QRectF advanceBox;  ///< Box of the advance of the character (for the spaces between the words)
    QColor color;
    bool invisible = false;
    size_t order = 0;
};

struct DrawnRectangle
{
    QRectF rect;
    QColor color;
    size_t order = 0;
};

/// Collects the characters and the opaque filled rectangles of a page, in paint order
class PDFCoverCheckProcessor : public PDFPageContentProcessor
{
    using BaseClass = PDFPageContentProcessor;

public:
    using BaseClass::BaseClass;

    std::vector<DrawnCharacter> characters;
    std::vector<DrawnRectangle> rectangles;

protected:
    virtual bool isContentKindSuppressed(ContentKind kind) const override
    {
        switch (kind)
        {
            case ContentKind::Shapes:
            case ContentKind::Text:
            case ContentKind::Forms:
                return false;

            default:
                return true;
        }
    }

    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override
    {
        Q_UNUSED(stroke);
        Q_UNUSED(fillRule);

        if (!fill || text || isContentSuppressed())
        {
            return;
        }

        const PDFPageContentProcessorState* state = getGraphicState();
        if (state->getAlphaFilling() < 1.0 || state->getBlendMode() != BlendMode::Normal ||
            (state->getFillColorSpace() && state->getFillColorSpace()->asPatternColorSpace()))
        {
            return;
        }

        // An axis-aligned rectangle (in the page): one polygon, which fills its box
        const QPainterPath mapped = getCurrentWorldMatrix().map(path);
        const QList<QPolygonF> polygons = mapped.toFillPolygons();
        if (polygons.size() != 1)
        {
            return;
        }

        const QRectF box = mapped.boundingRect();
        const QPolygonF& polygon = polygons.front();
        PDFReal area = 0.0;
        for (qsizetype i = 0; i < polygon.size(); ++i)
        {
            const QPointF& a = polygon[i];
            const QPointF& b = polygon[(i + 1) % polygon.size()];
            area += a.x() * b.y() - b.x() * a.y();
        }
        area = std::abs(area) * 0.5;

        const PDFReal boxArea = box.width() * box.height();
        if (boxArea <= 0.0 || std::abs(area - boxArea) > boxArea * 0.01)
        {
            return;
        }

        rectangles.push_back({ box, state->getFillColor(), m_order++ });
    }

    virtual void performOutputCharacter(const PDFTextCharacterInfo& info) override
    {
        if (isContentSuppressed() || (info.character.isSpace() && info.text.isEmpty()))
        {
            return;
        }

        const QRectF advanceBox = info.matrix.mapRect(QRectF(0.0, 0.0, info.advance, qAbs(info.fontSize) * 0.7));
        QRectF box = info.outline.boundingRect();
        box = box.isEmpty() ? advanceBox : info.matrix.mapRect(box);

        const PDFPageContentProcessorState* state = getGraphicState();
        const TextRenderingMode mode = state->getTextRenderingMode();
        const bool invisible = mode == TextRenderingMode::Invisible || mode == TextRenderingMode::Clip;
        const bool stroked = mode == TextRenderingMode::Stroke || mode == TextRenderingMode::StrokeClip;

        DrawnCharacter character;
        character.text = info.text.isEmpty() ? QString(info.character) : info.text;
        character.box = box;
        character.advanceBox = advanceBox;
        character.color = stroked ? state->getStrokeColor() : state->getFillColor();
        character.invisible = invisible;
        character.order = m_order++;
        characters.push_back(std::move(character));
    }

private:
    size_t m_order = 0;
};

PDFReal getLuminance(const QColor& color)
{
    auto linear = [](PDFReal c)
    {
        c = qBound(0.0, c, 1.0);
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const QColor rgb = color.toRgb();
    return 0.2126 * linear(rgb.redF()) + 0.7152 * linear(rgb.greenF()) + 0.0722 * linear(rgb.blueF());
}

PDFReal getArea(const QRectF& rect)
{
    return rect.isValid() ? rect.width() * rect.height() : 0.0;
}

/// The text of the hidden characters, with spaces between the words and the lines
QString joinCharacters(const std::vector<const DrawnCharacter*>& characters)
{
    QString text;
    const DrawnCharacter* previous = nullptr;
    for (const DrawnCharacter* character : characters)
    {
        if (previous)
        {
            const QRectF& previousBox = previous->advanceBox;
            const QRectF& box = character->advanceBox;
            const PDFReal height = qMax(previousBox.height(), box.height());
            const bool newLine = std::abs(box.center().y() - previousBox.center().y()) > height * 0.5;
            const bool gap = box.left() - previousBox.right() > height * 0.2;
            if ((newLine || gap) && !text.endsWith(' '))
            {
                text += ' ';
            }
        }
        text += character->text;
        previous = character;
    }
    return text.simplified();
}

/// Rectangles of the filled square comments, which are opaque
std::vector<DrawnRectangle> getCommentBoxes(const PDFDocument* document, const PDFPage* page)
{
    std::vector<DrawnRectangle> boxes;
    PDFDocumentDataLoaderDecorator loader(document);
    for (const PDFObjectReference annotation : page->getAnnotations())
    {
        const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(annotation));
        if (!dictionary || loader.readNameFromDictionary(dictionary, "Subtype") != "Square")
        {
            continue;
        }

        const PDFInteger flags = loader.readIntegerFromDictionary(dictionary, "F", 0);
        const PDFReal opacity = loader.readNumberFromDictionary(dictionary, "CA", 1.0);
        const std::vector<PDFReal> interior = loader.readNumberArrayFromDictionary(dictionary, "IC");
        if ((flags & (2 | 32)) != 0 || opacity < 1.0 || interior.empty())
        {
            continue;
        }

        QColor color;
        switch (interior.size())
        {
            case 1:
                color = QColor::fromRgbF(interior[0], interior[0], interior[0]);
                break;
            case 3:
                color = QColor::fromRgbF(interior[0], interior[1], interior[2]);
                break;
            case 4:
                color = QColor::fromCmykF(interior[0], interior[1], interior[2], interior[3]).toRgb();
                break;
            default:
                continue;
        }

        const QRectF rect = loader.readRectangle(dictionary->get("Rect"), QRectF()).normalized();
        if (rect.isValid())
        {
            // Annotations are drawn over the whole page content
            boxes.push_back({ rect, color, std::numeric_limits<size_t>::max() });
        }
    }

    return boxes;
}

}   // namespace

PDFReal PDFFireCoverCheck::getContrast(const QColor& first, const QColor& second)
{
    const PDFReal l1 = getLuminance(first);
    const PDFReal l2 = getLuminance(second);
    return (qMax(l1, l2) + 0.05) / (qMin(l1, l2) + 0.05);
}

std::vector<PDFFireCoverCheck::Finding> PDFFireCoverCheck::check(const PDFDocument* document,
                                                                 const PDFFontCache* fontCache,
                                                                 const PDFCMS* cms,
                                                                 const PDFOptionalContentActivity* optionalContentActivity,
                                                                 const PDFMeshQualitySettings& meshQualitySettings,
                                                                 std::vector<PDFInteger> pages)
{
    std::vector<Finding> findings;
    if (!document)
    {
        return findings;
    }

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    if (pages.empty())
    {
        for (PDFInteger i = 0; i < pageCount; ++i)
        {
            pages.push_back(i);
        }
    }

    QMutex mutex;
    auto checkPage = [&](PDFInteger pageIndex)
    {
        const PDFPage* page = pageIndex >= 0 && pageIndex < pageCount ? document->getCatalog()->getPage(pageIndex) : nullptr;
        if (!page)
        {
            return;
        }

        PDFCoverCheckProcessor processor(page, document, fontCache, cms, optionalContentActivity, QTransform(), meshQualitySettings);
        processor.processContents();

        std::vector<std::pair<CoverKind, DrawnRectangle>> covers;
        for (const DrawnRectangle& rectangle : processor.rectangles)
        {
            covers.emplace_back(CoverKind::PageContent, rectangle);
        }
        for (const DrawnRectangle& rectangle : getCommentBoxes(document, page))
        {
            covers.emplace_back(CoverKind::Comment, rectangle);
        }

        std::vector<Finding> pageFindings;
        for (const auto& [kind, cover] : covers)
        {
            std::vector<const DrawnCharacter*> hidden;
            for (const DrawnCharacter& character : processor.characters)
            {
                if (character.order >= cover.order)
                {
                    // Drawn after the box, so it is on top of it
                    continue;
                }

                const PDFReal area = getArea(character.box);
                if (area <= 0.0 || getArea(character.box.intersected(cover.rect)) < area * HiddenCharacterFraction)
                {
                    continue;
                }

                if (character.invisible || getContrast(character.color, cover.color) < CoverContrast)
                {
                    hidden.push_back(&character);
                }
            }

            const QString text = joinCharacters(hidden);
            if (!text.isEmpty())
            {
                pageFindings.push_back({ pageIndex, kind, cover.rect, cover.color, text });
            }
        }

        if (!pageFindings.empty())
        {
            QMutexLocker lock(&mutex);
            findings.insert(findings.end(), pageFindings.cbegin(), pageFindings.cend());
        }
    };

    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pages.cbegin(), pages.cend(), checkPage);

    // By pages, from the top of each page
    std::sort(findings.begin(), findings.end(), [](const Finding& a, const Finding& b)
    {
        if (a.pageIndex != b.pageIndex)
        {
            return a.pageIndex < b.pageIndex;
        }
        if (!qFuzzyCompare(a.rect.bottom(), b.rect.bottom()))
        {
            return a.rect.bottom() > b.rect.bottom();
        }
        return a.rect.left() < b.rect.left();
    });

    return findings;
}

}   // namespace pdf
