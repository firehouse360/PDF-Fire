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

#ifndef PDFREDACT_H
#define PDFREDACT_H

#include "pdfdocument.h"
#include "pdfrenderer.h"

#include <QStringList>

namespace pdf
{
class PDFDocumentBuilder;

/// Create redacted document from the document, which have redact annotations.
/// Redacted document has removed content marked by these annotations, and
/// annotations themselfs are removed.
class PDF4QTLIBCORESHARED_EXPORT PDFRedact
{
public:
    explicit PDFRedact(const PDFDocument* document,
                       const PDFFontCache* fontCache,
                       const PDFCMS* cms,
                       const PDFOptionalContentActivity* optionalContentActivity,
                       const PDFMeshQualitySettings* meshQualitySettings,
                       QColor redactFillColor);

    enum Option
    {
        None            = 0x0000,
        CopyTitle       = 0x0001,
        CopyMetadata    = 0x0002,
        CopyOutline     = 0x0004,
        KeepTextSearchable = 0x0008  ///< PDF Fire: content outside of the areas is kept (text stays text), see PDFFireRedaction
    };
    Q_DECLARE_FLAGS(Options, Option)


    pdf::PDFDocument perform(Options options);

    /// PDF Fire: a label (for example "REDACTED") written in the centre of every redacted
    /// area, over the fill. Empty text = plain boxes (the default).
    void setLabel(const QString& text, QColor color) { m_labelText = text; m_labelColor = color; }

    /// PDF Fire: messages about the pages, which had to be converted to outlines
    /// (when the content of the page could not be filtered safely)
    const QStringList& getMessages() const { return m_messages; }

private:
    /// PDF Fire: writes the page converted to outlines (the original method)
    void writeOutlinedPage(PDFDocumentBuilder* builder, PDFRenderer* renderer, size_t pageIndex, PDFObjectReference newPageReference);

    QStringList m_messages;
    const PDFDocument* m_document;
    const PDFFontCache* m_fontCache;
    const PDFCMS* m_cms;
    const PDFOptionalContentActivity* m_optionalContentActivity;
    const PDFMeshQualitySettings* m_meshQualitySettings;
    QColor m_redactFillColor;
    QString m_labelText;
    QColor m_labelColor;
};

}   // namespace pdf

#endif // PDFREDACT_H
