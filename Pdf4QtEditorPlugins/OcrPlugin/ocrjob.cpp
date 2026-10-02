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

#include "ocrjob.h"
#include "ocrengine.h"

#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"
#include "pdfpainter.h"
#include "pdfrenderer.h"
#include "pdftextlayoutgenerator.h"

#include <QCoreApplication>
#include <QPainter>

#include <cmath>

namespace pdfplugin
{

namespace
{

/// Everything the renderer needs, private to the job (the font cache of the window
/// must not be used from another thread)
struct RenderContext
{
    explicit RenderContext(const pdf::PDFDocument* document) :
        fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT),
        optionalContentActivity(document, pdf::OCUsage::View, nullptr)
    {
        pdf::PDFModifiedDocument modifiedDocument(const_cast<pdf::PDFDocument*>(document), &optionalContentActivity);
        fontCache.setDocument(modifiedDocument);
    }

    pdf::PDFFontCache fontCache;
    pdf::PDFOptionalContentActivity optionalContentActivity;
    pdf::PDFCMSGeneric cms;
    pdf::PDFMeshQualitySettings meshQualitySettings;
};

QString tr(const char* text)
{
    return QCoreApplication::translate("pdfplugin::OcrJob", text);
}

}   // namespace

OcrJob::OcrJob(pdf::PDFDocument document, Settings settings) :
    m_document(std::move(document)),
    m_settings(std::move(settings))
{

}

bool OcrJob::hasText(const pdf::PDFDocument* document, const pdf::PDFPage* page)
{
    RenderContext context(document);
    pdf::PDFTextLayoutGenerator generator(pdf::PDFRenderer::None, page, document, &context.fontCache, &context.cms,
                                          &context.optionalContentActivity, QTransform(), context.meshQualitySettings);
    generator.processContents();
    const pdf::PDFTextLayout textLayout = generator.createTextLayout();

    for (const pdf::PDFTextBlock& block : textLayout.getTextBlocks())
    {
        for (const pdf::PDFTextLine& line : block.getLines())
        {
            for (const pdf::TextCharacter& character : line.getCharacters())
            {
                if (!character.character.isSpace() && !character.character.isNull())
                {
                    return true;
                }
            }
        }
    }

    return false;
}

QImage OcrJob::renderPage(const pdf::PDFDocument* document, pdf::PDFInteger pageIndex, int dpi, QTransform* imageToPage)
{
    const pdf::PDFPage* page = document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        return QImage();
    }

    // The whole media box, as it is displayed (rotated). Huge pages are limited, so
    // the image cannot exhaust the memory (and Tesseract limits the image size too).
    const QSizeF sizeInPoints = page->getRotatedMediaBox().size();
    double scale = dpi / 72.0;
    constexpr double maximumSide = 12000.0;
    const double largestSide = qMax(sizeInPoints.width(), sizeInPoints.height()) * scale;
    if (largestSide > maximumSide)
    {
        scale *= maximumSide / largestSide;
    }

    const QSize imageSize(qMax(1, qRound(sizeInPoints.width() * scale)), qMax(1, qRound(sizeInPoints.height() * scale)));
    if (!std::isfinite(scale) || imageSize.width() < 8 || imageSize.height() < 8)
    {
        return QImage();
    }

    RenderContext context(document);
    const pdf::PDFRenderer::Features features = pdf::PDFRenderer::Antialiasing | pdf::PDFRenderer::TextAntialiasing |
                                                pdf::PDFRenderer::SmoothImages | pdf::PDFRenderer::ClipToCropBox;
    pdf::PDFRenderer renderer(document, &context.fontCache, &context.cms, &context.optionalContentActivity, features, context.meshQualitySettings);
    pdf::PDFPrecompiledPage precompiledPage;
    renderer.compile(&precompiledPage, pageIndex);

    QImage image(imageSize, QImage::Format_RGB32);
    if (image.isNull())
    {
        return QImage();
    }
    image.fill(Qt::white);

    const QTransform pageToImage = pdf::PDFRenderer::createPagePointToDevicePointMatrix(page, QRectF(QPointF(0, 0), QSizeF(imageSize)));
    {
        QPainter painter(&image);
        precompiledPage.draw(&painter, page->getCropBox(), pageToImage, features, 1.0);
    }

    *imageToPage = pageToImage.inverted();
    return image;
}

void OcrJob::run()
{
    OcrEngine engine;
    if (!engine.init(m_settings.language, &m_errorMessage))
    {
        return;
    }

    int order = 0;
    for (const pdf::PDFInteger pageIndex : m_settings.pages)
    {
        if (m_cancelled)
        {
            return;
        }

        m_currentPageNumber = int(pageIndex + 1);
        m_currentPageOrder = ++order;
        const int baseProgress = (order - 1) * 100;
        m_progress = baseProgress;

        const pdf::PDFPage* page = m_document.getCatalog()->getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        if (m_settings.skipPagesWithText && hasText(&m_document, page))
        {
            ++m_skippedPageCount;
            continue;
        }

        if (m_cancelled)
        {
            return;
        }

        QTransform imageToPage;
        const QImage image = renderPage(&m_document, pageIndex, m_settings.dpi, &imageToPage);
        if (image.isNull())
        {
            m_errorMessage = tr("Page %1 could not be rendered.").arg(pageIndex + 1);
            return;
        }

        // The rendering takes about a tenth of the time of a page, the recognition the rest
        m_progress = baseProgress + 10;

        QString errorMessage;
        auto isCancelled = [this]() { return m_cancelled.load(); };
        auto progress = [this, baseProgress](int percent) { m_progress = baseProgress + 10 + percent * 90 / 100; };
        OcrPageText text = engine.recognize(image, m_settings.dpi, isCancelled, progress, &errorMessage);

        if (m_cancelled)
        {
            return;
        }

        if (!errorMessage.isEmpty())
        {
            m_errorMessage = tr("Page %1: %2").arg(pageIndex + 1).arg(errorMessage);
            return;
        }

        if (text.isEmpty())
        {
            ++m_pagesWithoutTextCount;
        }
        else
        {
            m_results.push_back(PageResult{ pageIndex, std::move(text), imageToPage });
        }
    }

    m_progress = getMaximumProgress();
}

}   // namespace pdfplugin
