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

#ifndef PDFFIRESAVEDSIGNATURE_H
#define PDFFIRESAVEDSIGNATURE_H

#include "pdfdocument.h"
#include "pdfutils.h"

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPainterPath>
#include <QPointF>
#include <QSizeF>
#include <QString>

#include <vector>

class QPainter;

namespace pdf
{

/// PDF Fire: a hand-drawn or typed signature (or initials) of the Fill & Sign tools.
/// It is a picture of a signature, placed into the page content - not a signature with
/// a certificate. It can be saved for later (outside of any document, see
/// PDFFireSavedSignatureStore) and placed again with one click.
class PDF4QTLIBCORESHARED_EXPORT PDFFireSavedSignature
{
public:
    enum class Kind
    {
        None,
        Drawn,      ///< Strokes drawn with the mouse or a pen
        Typed       ///< A text in a handwriting font
    };

    using Stroke = std::vector<QPointF>;

    Kind getKind() const { return m_kind; }
    bool isEmpty() const;

    /// Drawn signature: the strokes, in the coordinates of a pad of the given size
    /// (x to the right, y down)
    void setStrokes(std::vector<Stroke> strokes, QSizeF padSize);
    const std::vector<Stroke>& getStrokes() const { return m_strokes; }
    QSizeF getPadSize() const { return m_padSize; }

    /// Typed signature: the text and the family of its font
    void setText(QString text, QString fontFamily);
    const QString& getText() const { return m_text; }
    const QString& getFontFamily() const { return m_fontFamily; }

    QColor getColor() const { return m_color; }
    void setColor(QColor color) { m_color = color; }

    /// Width of the pen of a drawn signature, relative to the height of its pad
    qreal getPenWidth() const { return m_penWidth; }
    void setPenWidth(qreal penWidth) { m_penWidth = penWidth; }

    /// The outline of the signature (x to the right, y down), its bounding box starts at
    /// (0, 0). A drawn signature is stroked, a typed one is filled.
    QPainterPath getPath() const;

    /// The width / height of the signature, as it is placed (1 for an empty signature)
    qreal getAspectRatio() const;

    /// Paints the signature into the rectangle (it keeps its proportions, it is centered)
    void paint(QPainter* painter, const QRectF& rect, const QColor& color) const;
    void paint(QPainter* painter, const QRectF& rect) const { paint(painter, rect, m_color); }

    /// A picture of the signature (transparent background)
    QImage toImage(QSize size, QColor color) const;

    QJsonObject toJson() const;
    static PDFFireSavedSignature fromJson(const QJsonObject& object);

    bool operator==(const PDFFireSavedSignature&) const = default;

    /// Writes the signature into the content of a page. The rectangle is in the page space
    /// (as PDF: y goes up); on a rotated page the signature is turned with the page, so it
    /// is upright when the page is shown (as the rectangle is on the screen).
    static PDFOperationResult placeOnPage(const PDFDocument* document, PDFInteger pageIndex, const QRectF& rect,
                                          const PDFFireSavedSignature& signature, PDFDocument* result);

private:
    /// The bounding box of the drawn strokes (without the pen)
    QRectF getStrokesBoundingRect() const;
    qreal getAbsolutePenWidth() const;

    Kind m_kind = Kind::None;
    std::vector<Stroke> m_strokes;
    QSizeF m_padSize;
    QString m_text;
    QString m_fontFamily;
    QColor m_color = QColor(16, 32, 96);   // a dark blue ink
    qreal m_penWidth = 0.025;
};

/// PDF Fire: the signature and the initials saved for later, in the application data
/// folder (never in a document). One of each.
class PDF4QTLIBCORESHARED_EXPORT PDFFireSavedSignatureStore
{
public:
    enum class Role
    {
        Signature,
        Initials
    };

    /// The file, where the signatures are stored (signatures.json in the application data folder)
    static QString getFileName();

    static PDFFireSavedSignature load(Role role);
    static bool save(Role role, const PDFFireSavedSignature& signature);
    static bool remove(Role role);

private:
    static QJsonObject readAll();
    static bool writeAll(const QJsonObject& object);
    static QString getKey(Role role);
};

}   // namespace pdf

#endif // PDFFIRESAVEDSIGNATURE_H
