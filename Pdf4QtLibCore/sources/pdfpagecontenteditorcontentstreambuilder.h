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

#ifndef PDFPAGECONTENTEDITORCONTENTSTREAMBUILDER_H
#define PDFPAGECONTENTEDITORCONTENTSTREAMBUILDER_H

#include "pdfpagecontenteditorprocessor.h"
#include "pdfeditorfallbackfont.h"

#include <QHash>
#include <QPaintDevice>

namespace pdf
{
class PDFPageContentElement;
class PDFContentEditorPaintEngine;
class PDFFireEmbeddedFont;
class PDFPageContentEditorContentStreamBuilder;

class PDF4QTLIBCORESHARED_EXPORT PDFContentEditorPaintDevice : public QPaintDevice
{
public:
    PDFContentEditorPaintDevice(PDFPageContentEditorContentStreamBuilder* builder, QRectF mediaRect, QRectF mediaRectMM);
    virtual ~PDFContentEditorPaintDevice() override;

    virtual int devType() const override;
    virtual QPaintEngine* paintEngine() const override;

protected:
    virtual int metric(PaintDeviceMetric metric) const override;

private:
    PDFContentEditorPaintEngine* m_paintEngine;
    QRectF m_mediaRect;
    QRectF m_mediaRectMM;
};

class PDF4QTLIBCORESHARED_EXPORT PDFPageContentEditorContentStreamBuilder
{
public:
    PDFPageContentEditorContentStreamBuilder(PDFDocument* document);

    /// Writes the edited element. Elements of a transparency group, which can't be
    /// flattened into the page, are written into a form XObject of the group. The group
    /// is finished, when an element outside of the group is written, or when the output
    /// (the content or the resource dictionaries) is requested.
    void writeEditedElement(const PDFEditedPageContentElement* element);

    /// Finishes all open transparency groups (their form XObjects are written
    /// into the XObject dictionary and painted by the output content).
    void finishTransparencyGroups();

    /// Returns the output content (open transparency groups are finished first)
    const QByteArray& getOutputContent();

    /// Resource dictionaries (open transparency groups are finished first)
    const PDFDictionaryBuilder& getFontDictionary() { finishTransparencyGroups(); finishEmbeddedFonts(); return m_fontDictionary; }
    const PDFDictionaryBuilder& getXObjectDictionary() { finishTransparencyGroups(); return m_xobjectDictionary; }
    const PDFDictionaryBuilder& getGraphicStateDictionary() { finishTransparencyGroups(); return m_graphicStateDictionary; }
    const PDFDictionaryBuilder& getShadingDictionary() { finishTransparencyGroups(); return m_shadingDictionary; }

    void setFontDictionary(PDFDictionaryBuilder newFontDictionary);
    void setXObjectDictionary(PDFDictionaryBuilder newXObjectDictionary);
    void setGraphicStateDictionary(PDFDictionaryBuilder newGraphicStateDictionary);
    void setShadingDictionary(PDFDictionaryBuilder newShadingDictionary);

    const QStringList& getErrors() const { return m_errors; }
    void clearErrors() { m_errors.clear(); }

    void writeStyledPath(const QPainterPath& path,
                         const QPen& pen,
                         const QBrush& brush,
                         bool isStroking,
                         bool isFilling);

    /// Writes styled path with the given graphic state. Optional clip path
    /// is expressed in the page coordinate space (it is applied before
    /// the current transformation matrix is written). An empty clip path
    /// means no clipping.
    void writeStyledPath(const QPainterPath& path,
                         const PDFPageContentProcessorState& state,
                         bool isStroking,
                         bool isFilling,
                         const QPainterPath& clipPath = QPainterPath());

    /// Writes image, which is placed into the rectangle expressed in the page
    /// coordinate space. The image keeps its aspect ratio and it is centered
    /// in the rectangle. Neither the transformation matrix of the previously
    /// written element, nor the graphic state parameters, which take part in
    /// painting an image (constant alpha, alpha source flag, blend mode,
    /// rendering intent and overprint), are applied to the image - all of them
    /// are reset to the default values.
    void writeImage(const QImage& image, const QRectF& rectangle);

    /// PDF Fire: writes the text as real text (searchable, selectable) with the font
    /// embedded into the document. Returns false, when it is not possible (the font is
    /// not a TrueType font, its licence does not allow embedding, a character is
    /// missing in it) - the text has to be drawn as outlines then.
    /// \param font Font (the size in the units of the painter)
    /// \param text Text
    /// \param position Position of the base line start (in the painter coordinates, y down)
    /// \param state State (the transformation matrix maps the painter coordinates to the page)
    /// \param color Color of the text
    /// \param clipPath Clip path in the page coordinates (empty - no clipping)
    bool writeTextWithEmbeddedFont(const QFont& font, const QString& text, QPointF position,
                                   const PDFPageContentProcessorState& state, const QColor& color,
                                   const QPainterPath& clipPath);

    /// Writes image placed by the painter transform. Optional clip path
    /// is expressed in the page coordinate space. An empty clip path
    /// means no clipping.
    void writeImage(const QImage& image, QTransform transform, const QRectF& rectangle, const QPainterPath& clipPath = QPainterPath());

    const PDFPageContentProcessorState& getCurrentState() { return m_currentState; }

    /// Writes the path construction operators (m, l, c, h) for the given path,
    /// without any painting operator.
    static void writePathGeometry(QTextStream& stream, const QPainterPath& path);

private:
    /// Transparency group, whose elements are being written
    struct OpenTransparencyGroup
    {
        PDFEditedPageContentTransparencyGroupPointer group;
        QByteArray outputContent;           ///< Output content of the enclosing group (or of the page)
        PDFPageContentProcessorState state; ///< Current state of the enclosing group (or of the page)
        QRectF boundingBox;                 ///< Bounding box of the elements of the group, in the page coordinate space
    };

    /// Finishes transparency groups, which don't contain the element, and starts
    /// transparency groups of the element, which are not yet started.
    void updateTransparencyGroups(const PDFEditedPageContentElement* element);
    void beginTransparencyGroup(const PDFEditedPageContentTransparencyGroupPointer& group);
    void endTransparencyGroup();

    /// Returns the bounding box of the area, which can be painted by the element
    /// (including the stroke and limited by the clip path), in the page coordinate space
    static QRectF getPaintedAreaBoundingBox(const PDFEditedPageContentElement* element);

    bool isNeededToWriteCurrentTransformationMatrix() const;

    void writeCurrentTransformationMatrix(QTextStream& stream);
    void writeStateDifference(QTextStream& stream, const PDFPageContentProcessorState& state);

    void writePainterPath(QTextStream& stream,
                          const QPainterPath& path,
                          bool isStroking,
                          bool isFilling);

    /// Writes the path as a clip path (path construction operators followed
    /// by "W n" or "W* n", according to the path fill rule).
    void writeClipPath(QTextStream& stream, const QPainterPath& clipPath);

    /// Writes the text object
    /// \param stream Stream
    /// \param text Text items as text
    /// \param fontKey Key of the font of the current graphic state (empty, if unknown)
    void writeText(QTextStream& stream, const QString& text, const QByteArray& fontKey);
    void writeTextCommand(QTextStream& stream, const QXmlStreamReader& reader);

    /// Writes text characters using the current text font. Characters, which
    /// cannot be encoded into the current font, are written using an on-the-fly
    /// generated Type 3 fallback font.
    void writeTextWithFallback(QTextStream& stream, const QString& characters);

    void flushPendingText(QTextStream& stream);
    void writeTextHexString(QTextStream& stream, const QByteArray& encodedText);

    void writeImage(QTextStream& stream, const QImage& image);
    void writeImageObject(QTextStream& stream, const PDFObject& imageObject);

    /// Writes the 'sh' operator painting the shading object. The shading object
    /// is added into the shading dictionary, if it is not already present.
    void writeShadingObject(QTextStream& stream, const PDFObject& shadingObject);

    QByteArray selectFont(const QByteArray& font);

    /// Returns the key of the font object in the font dictionary. If the font
    /// object is not present in the font dictionary, it is added under the given
    /// key, or under a modified key, if the key denotes another font.
    /// \param key Preferred key
    /// \param fontObject Font object
    QByteArray getFontResourceKey(const QByteArray& key, const PDFObject& fontObject);

    void addError(const QString& error);

    /// PDF Fire: writes the objects of the embedded fonts into the font dictionary
    void finishEmbeddedFonts();

    struct EmbeddedFont
    {
        QByteArray key;
        std::shared_ptr<PDFFireEmbeddedFont> font; ///< nullptr - the font cannot be embedded
    };
    std::map<QString, EmbeddedFont> m_embeddedFonts;

    PDFDocument* m_document = nullptr;
    PDFDictionaryBuilder m_fontDictionary;
    PDFDictionaryBuilder m_xobjectDictionary;
    PDFDictionaryBuilder m_graphicStateDictionary;
    PDFDictionaryBuilder m_shadingDictionary;
    QByteArray m_outputContent;
    PDFPageContentProcessorState m_currentState;
    QString m_pendingTextArray; ///< PDF Fire: strings and kerning of the TJ operator being collected
    PDFFontPointer m_textFont;
    QHash<QByteArray, PDFFontPointer> m_fontOverrides;
    QHash<QByteArray, PDFObject> m_fontResourceObjects; ///< Font objects of the fonts of the written text element
    QStringList m_errors;
    PDFEditorFallbackFontManager m_fallbackFontManager;
    QByteArray m_currentTextFontKey;    ///< Resource key of the last written Tf operator
    PDFReal m_currentTextFontSize = 0.0;
    std::vector<OpenTransparencyGroup> m_transparencyGroups; ///< Open transparency groups, from the outermost one
};

}   // namespace pdf

#endif // PDFPAGECONTENTEDITORCONTENTSTREAMBUILDER_H
