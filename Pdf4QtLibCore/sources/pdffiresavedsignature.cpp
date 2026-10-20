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

#include "pdffiresavedsignature.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>
#include <limits>

#include "pdfdbgheap.h"

namespace pdf
{

namespace
{

// Limits of a stored signature (the file is read back - never trust its size)
constexpr size_t MAXIMAL_STROKE_COUNT = 2000;
constexpr size_t MAXIMAL_POINT_COUNT = 200000;
constexpr int MAXIMAL_TEXT_LENGTH = 200;

/// The size of the font, in which a typed signature is made into an outline
constexpr int TYPED_FONT_SIZE = 64;

}   // namespace

bool PDFFireSavedSignature::isEmpty() const
{
    switch (m_kind)
    {
        case Kind::Drawn:
            return m_strokes.empty();
        case Kind::Typed:
            return m_text.trimmed().isEmpty();
        default:
            return true;
    }
}

void PDFFireSavedSignature::setStrokes(std::vector<Stroke> strokes, QSizeF padSize)
{
    strokes.erase(std::remove_if(strokes.begin(), strokes.end(), [](const Stroke& stroke) { return stroke.empty(); }), strokes.end());
    m_kind = Kind::Drawn;
    m_strokes = std::move(strokes);
    m_padSize = padSize;
    m_text.clear();
    m_fontFamily.clear();
}

void PDFFireSavedSignature::setText(QString text, QString fontFamily)
{
    m_kind = Kind::Typed;
    m_text = text.left(MAXIMAL_TEXT_LENGTH);
    m_fontFamily = std::move(fontFamily);
    m_strokes.clear();
    m_padSize = QSizeF();
}

QRectF PDFFireSavedSignature::getStrokesBoundingRect() const
{
    qreal left = std::numeric_limits<qreal>::max();
    qreal top = std::numeric_limits<qreal>::max();
    qreal right = std::numeric_limits<qreal>::lowest();
    qreal bottom = std::numeric_limits<qreal>::lowest();

    for (const Stroke& stroke : m_strokes)
    {
        for (const QPointF& point : stroke)
        {
            left = qMin(left, point.x());
            top = qMin(top, point.y());
            right = qMax(right, point.x());
            bottom = qMax(bottom, point.y());
        }
    }

    if (left > right)
    {
        return QRectF();
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

qreal PDFFireSavedSignature::getAbsolutePenWidth() const
{
    const qreal padHeight = m_padSize.height() > 0 ? m_padSize.height() : 100.0;
    return qBound(0.001, m_penWidth, 0.5) * padHeight;
}

QPainterPath PDFFireSavedSignature::getPath() const
{
    QPainterPath path;

    switch (m_kind)
    {
        case Kind::Drawn:
        {
            const QRectF boundingRect = getStrokesBoundingRect();
            const qreal halfPen = getAbsolutePenWidth() * 0.5;
            const QPointF offset = boundingRect.topLeft() - QPointF(halfPen, halfPen);
            for (const Stroke& stroke : m_strokes)
            {
                path.moveTo(stroke.front() - offset);
                if (stroke.size() == 1)
                {
                    // A dot - the round cap of a very short line
                    path.lineTo(stroke.front() - offset + QPointF(0.01, 0.0));
                }
                for (size_t i = 1; i < stroke.size(); ++i)
                {
                    path.lineTo(stroke[i] - offset);
                }
            }
            break;
        }

        case Kind::Typed:
        {
            QFont font(m_fontFamily);
            font.setPixelSize(TYPED_FONT_SIZE);
            path.addText(0, 0, font, m_text.simplified());
            path.translate(-path.boundingRect().topLeft());
            break;
        }

        default:
            break;
    }

    return path;
}

qreal PDFFireSavedSignature::getAspectRatio() const
{
    QSizeF size;
    if (m_kind == Kind::Drawn)
    {
        const qreal pen = getAbsolutePenWidth();
        size = getStrokesBoundingRect().size() + QSizeF(pen, pen);
    }
    else
    {
        size = getPath().boundingRect().size();
    }

    if (size.width() <= 0 || size.height() <= 0)
    {
        return 1.0;
    }

    // Very flat or very tall signatures are limited, so they stay visible
    return qBound(0.2, size.width() / size.height(), 12.0);
}

void PDFFireSavedSignature::paint(QPainter* painter, const QRectF& rect, const QColor& color) const
{
    if (isEmpty() || rect.isEmpty())
    {
        return;
    }

    const QPainterPath path = getPath();
    QSizeF contentSize = path.boundingRect().size();
    qreal pen = 0.0;
    if (m_kind == Kind::Drawn)
    {
        pen = getAbsolutePenWidth();
        contentSize = getStrokesBoundingRect().size() + QSizeF(pen, pen);
    }
    contentSize = contentSize.expandedTo(QSizeF(1.0, 1.0));

    const qreal scale = qMin(rect.width() / contentSize.width(), rect.height() / contentSize.height());
    const QPointF origin(rect.center().x() - contentSize.width() * scale * 0.5, rect.center().y() - contentSize.height() * scale * 0.5);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(origin);
    painter->scale(scale, scale);
    if (m_kind == Kind::Drawn)
    {
        painter->setPen(QPen(color, pen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
    }
    else
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
    }
    painter->drawPath(path);
    painter->restore();
}

QImage PDFFireSavedSignature::toImage(QSize size, QColor color) const
{
    QImage image(size.expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    const qreal margin = qMin(size.width(), size.height()) * 0.04;
    paint(&painter, QRectF(QPointF(0, 0), QSizeF(size)).adjusted(margin, margin, -margin, -margin), color);
    painter.end();

    return image;
}

QJsonObject PDFFireSavedSignature::toJson() const
{
    QJsonObject object;
    switch (m_kind)
    {
        case Kind::Drawn:
        {
            object["kind"] = QStringLiteral("drawn");
            object["pad"] = QJsonArray{ m_padSize.width(), m_padSize.height() };
            QJsonArray strokes;
            for (const Stroke& stroke : m_strokes)
            {
                QJsonArray coordinates;
                for (const QPointF& point : stroke)
                {
                    // Tenths of a pixel are enough
                    coordinates.append(std::round(point.x() * 10.0) / 10.0);
                    coordinates.append(std::round(point.y() * 10.0) / 10.0);
                }
                strokes.append(coordinates);
            }
            object["strokes"] = strokes;
            object["pen"] = m_penWidth;
            break;
        }

        case Kind::Typed:
            object["kind"] = QStringLiteral("typed");
            object["text"] = m_text;
            object["font"] = m_fontFamily;
            break;

        default:
            return QJsonObject();
    }

    object["color"] = m_color.name(QColor::HexRgb);
    return object;
}

PDFFireSavedSignature PDFFireSavedSignature::fromJson(const QJsonObject& object)
{
    PDFFireSavedSignature signature;
    const QString kind = object.value("kind").toString();

    if (kind == QLatin1String("drawn"))
    {
        const QJsonArray pad = object.value("pad").toArray();
        const QSizeF padSize(pad.at(0).toDouble(), pad.at(1).toDouble());

        std::vector<Stroke> strokes;
        size_t pointCount = 0;
        for (const QJsonValue& strokeValue : object.value("strokes").toArray())
        {
            const QJsonArray coordinates = strokeValue.toArray();
            Stroke stroke;
            for (qsizetype i = 0; i + 1 < coordinates.size() && pointCount < MAXIMAL_POINT_COUNT; i += 2, ++pointCount)
            {
                if (!coordinates.at(i).isDouble() || !coordinates.at(i + 1).isDouble())
                {
                    continue;
                }
                const qreal x = coordinates.at(i).toDouble();
                const qreal y = coordinates.at(i + 1).toDouble();
                if (std::isfinite(x) && std::isfinite(y) && qAbs(x) < 1e6 && qAbs(y) < 1e6)
                {
                    stroke.emplace_back(x, y);
                }
            }
            if (!stroke.empty())
            {
                strokes.push_back(std::move(stroke));
            }
            if (strokes.size() >= MAXIMAL_STROKE_COUNT)
            {
                break;
            }
        }

        const bool isPadValid = padSize.width() > 0 && padSize.height() > 0 && padSize.width() < 1e5 && padSize.height() < 1e5;
        signature.setStrokes(std::move(strokes), isPadValid ? padSize : QSizeF(400, 100));
        const qreal pen = object.value("pen").toDouble(signature.m_penWidth);
        signature.m_penWidth = (pen > 0.0 && pen <= 0.5) ? pen : 0.025;
    }
    else if (kind == QLatin1String("typed"))
    {
        signature.setText(object.value("text").toString(), object.value("font").toString());
    }
    else
    {
        return PDFFireSavedSignature();
    }

    const QColor color = QColor::fromString(object.value("color").toString());
    if (color.isValid())
    {
        signature.m_color = color;
    }

    return signature;
}

PDFOperationResult PDFFireSavedSignature::placeOnPage(const PDFDocument* document, PDFInteger pageIndex, const QRectF& rect,
                                                      const PDFFireSavedSignature& signature, PDFDocument* result)
{
    Q_ASSERT(document);
    Q_ASSERT(result);

    if (signature.isEmpty())
    {
        return PDFTranslationContext::tr("The signature is empty.");
    }

    const PDFPage* page = (pageIndex >= 0 && size_t(pageIndex) < document->getCatalog()->getPageCount()) ? document->getCatalog()->getPage(pageIndex) : nullptr;
    if (!page || !rect.isValid() || rect.isEmpty())
    {
        return PDFTranslationContext::tr("Invalid place of the signature.");
    }

    try
    {
        int rotation = 0;
        switch (page->getPageRotation())
        {
            case PageRotation::Rotate90: rotation = 90; break;
            case PageRotation::Rotate180: rotation = 180; break;
            case PageRotation::Rotate270: rotation = 270; break;
            default: break;
        }

        // The size on the screen - on a turned page the rectangle is transposed
        const QSizeF size = (rotation == 90 || rotation == 270) ? rect.size().transposed() : rect.size();

        // Signature (y down) -> upright space (y up) -> turned with the page -> page space.
        // The same matrices as the appearance of a signature with a certificate.
        QTransform rotationTransform;
        switch (rotation)
        {
            case 90: rotationTransform = QTransform(0, 1, -1, 0, size.height(), 0); break;
            case 180: rotationTransform = QTransform(-1, 0, 0, -1, size.width(), size.height()); break;
            case 270: rotationTransform = QTransform(0, -1, 1, 0, 0, size.width()); break;
            default: break;
        }
        const QTransform transform = QTransform(1, 0, 0, -1, 0, size.height()) * rotationTransform * QTransform::fromTranslate(rect.left(), rect.top());

        PDFDocumentModifier modifier(document);
        PDFPageContentStreamBuilder contentStreamBuilder(modifier.getBuilder(), PDFContentStreamBuilder::CoordinateSystem::PDF, PDFPageContentStreamBuilder::Mode::PlaceAfter);
        QPainter* painter = contentStreamBuilder.begin(page->getPageReference());
        if (!painter)
        {
            return PDFTranslationContext::tr("The signature cannot be written into the page.");
        }

        painter->setTransform(transform, true);
        signature.paint(painter, QRectF(QPointF(0, 0), size));
        contentStreamBuilder.end(painter);
        modifier.markPageContentsChanged();

        if (!modifier.finalize() || !modifier.getDocument())
        {
            return PDFTranslationContext::tr("The signature cannot be written into the page.");
        }
        *result = *modifier.getDocument();
    }
    catch (const PDFException& exception)
    {
        return exception.getMessage();
    }

    return true;
}

QString PDFFireSavedSignatureStore::getFileName()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("signatures.json"));
}

QString PDFFireSavedSignatureStore::getKey(Role role)
{
    return role == Role::Initials ? QStringLiteral("initials") : QStringLiteral("signature");
}

QJsonObject PDFFireSavedSignatureStore::readAll()
{
    QFile file(getFileName());
    if (file.size() > 16 * 1024 * 1024 || !file.open(QFile::ReadOnly))
    {
        return QJsonObject();
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool PDFFireSavedSignatureStore::writeAll(const QJsonObject& object)
{
    const QString fileName = getFileName();
    QDir().mkpath(QFileInfo(fileName).absolutePath());

    // Only the user can read the file - it is the user's signature
    QSaveFile file(fileName);
    if (!file.open(QFile::WriteOnly))
    {
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit())
    {
        return false;
    }
    QFile::setPermissions(fileName, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

PDFFireSavedSignature PDFFireSavedSignatureStore::load(Role role)
{
    return PDFFireSavedSignature::fromJson(readAll().value(getKey(role)).toObject());
}

bool PDFFireSavedSignatureStore::save(Role role, const PDFFireSavedSignature& signature)
{
    if (signature.isEmpty())
    {
        return remove(role);
    }

    QJsonObject object = readAll();
    object[getKey(role)] = signature.toJson();
    return writeAll(object);
}

bool PDFFireSavedSignatureStore::remove(Role role)
{
    QJsonObject object = readAll();
    if (!object.contains(getKey(role)))
    {
        return true;
    }
    object.remove(getKey(role));
    return writeAll(object);
}

}   // namespace pdf
