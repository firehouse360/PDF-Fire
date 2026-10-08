// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdfredact.h"
#include "pdfpainter.h"
#include "pdfdocumentbuilder.h"
#include "pdfoptimizer.h"
#include "pdfpathboolean.h"
#include "pdffireredaction.h"
#include "pdfdbgheap.h"

namespace pdf
{

PDFRedact::PDFRedact(const PDFDocument* document,
                     const PDFFontCache* fontCache,
                     const PDFCMS* cms,
                     const PDFOptionalContentActivity* optionalContentActivity,
                     const PDFMeshQualitySettings* meshQualitySettings,
                     QColor redactFillColor) :
    m_document(document),
    m_fontCache(fontCache),
    m_cms(cms),
    m_optionalContentActivity(optionalContentActivity),
    m_meshQualitySettings(meshQualitySettings),
    m_redactFillColor(redactFillColor)
{

}

QColor PDFRedact::getLabelColor() const
{
    // The chosen color, otherwise (a box has its own text, but no label color was chosen)
    // a color, which can be read on the fill
    if (m_labelColor.isValid())
    {
        return m_labelColor;
    }
    return m_redactFillColor.isValid() && m_redactFillColor.lightness() >= 128 ? QColor(Qt::black) : QColor(Qt::white);
}

void PDFRedact::writeOutlinedPage(PDFDocumentBuilder* builder, PDFRenderer* renderer, size_t pageIndex, PDFObjectReference newPageReference)
{
    const PDFPage* page = m_document->getCatalog()->getPage(pageIndex);

    PDFPrecompiledPage compiledPage;
    renderer->compile(&compiledPage, pageIndex);

    PDFPageContentStreamBuilder contentStreamBuilder(builder);

    QPainterPath redactPath;

    for (const PDFObjectReference& annotationReference : page->getAnnotations())
    {
        PDFAnnotationPtr annotation = PDFAnnotation::parse(&m_document->getStorage(), annotationReference);
        if (!annotation || annotation->getType() != AnnotationType::Redact)
        {
            continue;
        }

        // We have redact annotation here
        const PDFRedactAnnotation* redactAnnotation = dynamic_cast<const PDFRedactAnnotation*>(annotation.get());
        Q_ASSERT(redactAnnotation);

        redactPath = PDFPathBoolean::unite(redactPath, redactAnnotation->getRedactionRegion().getPath());
    }

    QTransform matrix;
    matrix.translate(0, page->getMediaBox().height());
    matrix.scale(1.0, -1.0);

    QPainter* painter = contentStreamBuilder.begin(newPageReference);
    compiledPage.redact(redactPath, matrix, m_redactFillColor);
    compiledPage.draw(painter, QRectF(), matrix, PDFRenderer::None, 1.0);

    // PDF Fire: the labels over the filled areas (the own text of a box, or the default text)
    const QPainterPath label = PDFFireRedaction::createPageLabels(m_document, page, m_labelText);
    const QColor labelColor = getLabelColor();
    if (!label.isEmpty())
    {
        painter->save();
        painter->setWorldTransform(QTransform());
        painter->fillPath(matrix.map(label), labelColor);
        painter->restore();
    }

    contentStreamBuilder.end(painter);
}

PDFDocument PDFRedact::perform(Options options)
{
    PDFDocumentBuilder builder;
    builder.createDocument();
    m_messages.clear();

    PDFRenderer renderer(m_document,
                         m_fontCache,
                         m_cms,
                         m_optionalContentActivity,
                         PDFRenderer::None,
                         *m_meshQualitySettings);

    std::map<PDFObjectReference, PDFObjectReference> mapOldPageRefToNewPageRef;
    std::vector<PDFObjectReference> newPageReferences;

    // PDF Fire: all pages are created first, so links can point to any page
    for (size_t i = 0; i < m_document->getCatalog()->getPageCount(); ++i)
    {
        const PDFPage* page = m_document->getCatalog()->getPage(i);

        PDFObjectReference newPageReference = builder.appendPage(page->getMediaBox());
        mapOldPageRefToNewPageRef[page->getPageReference()] = newPageReference;
        newPageReferences.push_back(newPageReference);

        if (!page->getCropBox().isEmpty())
        {
            builder.setPageCropBox(newPageReference, page->getCropBox());
        }
        if (!page->getBleedBox().isEmpty())
        {
            builder.setPageBleedBox(newPageReference, page->getBleedBox());
        }
        if (!page->getTrimBox().isEmpty())
        {
            builder.setPageTrimBox(newPageReference, page->getTrimBox());
        }
        if (!page->getArtBox().isEmpty())
        {
            builder.setPageArtBox(newPageReference, page->getArtBox());
        }
        builder.setPageRotation(newPageReference, page->getPageRotation());
    }

    if (options.testFlag(KeepTextSearchable))
    {
        // PDF Fire: the content outside of the redacted areas is kept as it is (text stays
        // searchable). A page, which can't be filtered safely, is converted to outlines.
        PDFFireRedaction redaction(m_document, m_fontCache, m_cms, m_meshQualitySettings, &builder);
        for (size_t i = 0; i < newPageReferences.size(); ++i)
        {
            const PDFPage* page = m_document->getCatalog()->getPage(i);
            const QPainterPath area = PDFFireRedaction::getRedactionArea(m_document, page);

            QString failureReason;
            const QPainterPath label = PDFFireRedaction::createPageLabels(m_document, page, m_labelText);
            if (!redaction.writePageContent(i, area, newPageReferences[i], m_redactFillColor, &failureReason, label, getLabelColor()))
            {
                writeOutlinedPage(&builder, &renderer, i, newPageReferences[i]);
                m_messages << PDFTranslationContext::tr("Page %1 was converted to outlines (its text is not searchable): %2").arg(i + 1).arg(failureReason);
            }

            redaction.writeAnnotations(i, area, newPageReferences[i], mapOldPageRefToNewPageRef);
        }
        redaction.copyOptionalContentProperties();
    }
    else
    {
        for (size_t i = 0; i < newPageReferences.size(); ++i)
        {
            writeOutlinedPage(&builder, &renderer, i, newPageReferences[i]);
        }
    }

    if (options.testFlag(CopyTitle))
    {
        builder.setDocumentTitle(m_document->getInfo()->title);
    }

    if (options.testFlag(CopyMetadata))
    {
        PDFObject info = m_document->getTrailerDictionary()->get("Info");
        if (!info.isNull())
        {
            std::vector<PDFObject> copiedObjects = builder.copyFrom({ info }, m_document->getStorage(), true);
            if (copiedObjects.size() == 1 && copiedObjects.front().isReference())
            {
                builder.setDocumentInfo(copiedObjects.front().getReference());
            }
        }
    }

    if (options.testFlag(CopyOutline))
    {
        PDFObject catalog = m_document->getObject(m_document->getTrailerDictionary()->get("Root"));
        if (const PDFDictionary* catalogDictionary = m_document->getDictionaryFromObject(catalog))
        {
            if (catalogDictionary->hasKey("Outlines"))
            {
                QSharedPointer<PDFOutlineItem> outlineRoot = PDFOutlineItem::parse(&m_document->getStorage(), catalogDictionary->get("Outlines"));

                if (outlineRoot)
                {
                    auto resolveNamedDestination = [this, &mapOldPageRefToNewPageRef](PDFOutlineItem* item)
                    {
                        PDFActionGoTo* action = dynamic_cast<PDFActionGoTo*>(item->getAction());
                        if (action)
                        {
                            if (action->getDestination().isNamedDestination())
                            {
                                const PDFDestination* destination = m_document->getCatalog()->getNamedDestination(action->getDestination().getName());
                                if (destination)
                                {
                                    action->setDestination(*destination);
                                }
                            }

                            PDFDestination destination = action->getDestination();
                            auto it = mapOldPageRefToNewPageRef.find(destination.getPageReference());
                            if (it != mapOldPageRefToNewPageRef.cend())
                            {
                                destination.setPageReference(it->second);
                                action->setDestination(destination);
                            }
                        }
                    };
                    outlineRoot->apply(resolveNamedDestination);

                    builder.setOutline(outlineRoot.data());
                }
            }
        }
    }

    PDFDocument redactedDocument = builder.build();
    PDFOptimizer optimizer(PDFOptimizer::All, nullptr);
    optimizer.setDocument(&redactedDocument);
    optimizer.optimize();
    return optimizer.takeOptimizedDocument();
}

}   // namespace pdf
