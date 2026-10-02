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

#include "pdffireredaction.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfpagecontentprocessor.h"
#include "pdfannotation.h"
#include "pdfaction.h"
#include "pdffont.h"
#include "pdfimage.h"
#include "pdfpattern.h"
#include "pdffunction.h"
#include "pdfpathboolean.h"
#include "pdfstreamfilters.h"
#include "pdfparser.h"
#include "pdfexception.h"
#include "pdfmeshqualitysettings.h"

#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QPainterPathStroker>

#include <cmath>
#include <cstring>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

namespace
{

// ---------------------------------------------------------------------------------------
// Writing of content stream syntax
// ---------------------------------------------------------------------------------------

QByteArray formatNumber(PDFReal value)
{
    if (!std::isfinite(value))
    {
        value = 0.0;
    }

    // Content streams do not allow the exponent notation, very big numbers are clamped
    value = qBound(-1.0e12, value, 1.0e12);

    const PDFReal rounded = std::round(value);
    if (std::abs(value - rounded) < 1.0e-10)
    {
        return QByteArray::number(static_cast<qlonglong>(rounded));
    }

    QByteArray text = QByteArray::number(value, 'f', 10);
    if (text.contains('.'))
    {
        while (text.endsWith('0'))
        {
            text.chop(1);
        }
        if (text.endsWith('.'))
        {
            text.chop(1);
        }
    }
    if (text == "-0" || text.isEmpty())
    {
        text = "0";
    }
    return text;
}

void writeName(QByteArray& out, const QByteArray& name)
{
    static const char hexDigits[] = "0123456789ABCDEF";
    out += '/';
    for (char c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x21 || u > 0x7E || std::strchr("#()<>[]{}/%", c))
        {
            out += '#';
            out += hexDigits[(u >> 4) & 0x0F];
            out += hexDigits[u & 0x0F];
        }
        else
        {
            out += c;
        }
    }
}

void writeString(QByteArray& out, const QByteArray& string)
{
    out += '<';
    out += string.toHex();
    out += '>';
}

void writeObject(QByteArray& out, const PDFObject& object)
{
    switch (object.getType())
    {
        case PDFObject::Type::Bool:
            out += object.getBool() ? "true" : "false";
            break;

        case PDFObject::Type::Int:
            out += QByteArray::number(object.getInteger());
            break;

        case PDFObject::Type::Real:
            out += formatNumber(object.getReal());
            break;

        case PDFObject::Type::String:
            writeString(out, object.getString());
            break;

        case PDFObject::Type::Name:
            writeName(out, object.getString());
            break;

        case PDFObject::Type::Array:
        {
            out += '[';
            bool first = true;
            for (const PDFObject& item : *object.getArray())
            {
                if (!first)
                {
                    out += ' ';
                }
                first = false;
                writeObject(out, item);
            }
            out += ']';
            break;
        }

        case PDFObject::Type::Dictionary:
        {
            out += "<<";
            const PDFDictionary* dictionary = object.getDictionary();
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                writeName(out, dictionary->getKey(i).getString());
                out += ' ';
                writeObject(out, dictionary->getValue(i));
                out += ' ';
            }
            out += ">>";
            break;
        }

        default:
            // Null, references and streams can't be operands in the content stream
            out += "null";
            break;
    }
}

void writePath(QByteArray& out, const QPainterPath& path)
{
    const int count = path.elementCount();
    for (int i = 0; i < count; ++i)
    {
        const QPainterPath::Element element = path.elementAt(i);
        switch (element.type)
        {
            case QPainterPath::MoveToElement:
                if (i > 0)
                {
                    out += "h\n";
                }
                out += formatNumber(element.x) + ' ' + formatNumber(element.y) + " m\n";
                break;

            case QPainterPath::LineToElement:
                out += formatNumber(element.x) + ' ' + formatNumber(element.y) + " l\n";
                break;

            case QPainterPath::CurveToElement:
            {
                if (i + 2 < count)
                {
                    const QPainterPath::Element c2 = path.elementAt(i + 1);
                    const QPainterPath::Element end = path.elementAt(i + 2);
                    out += formatNumber(element.x) + ' ' + formatNumber(element.y) + ' ' +
                           formatNumber(c2.x) + ' ' + formatNumber(c2.y) + ' ' +
                           formatNumber(end.x) + ' ' + formatNumber(end.y) + " c\n";
                }
                i += 2;
                break;
            }

            default:
                break;
        }
    }

    if (count > 0)
    {
        out += "h\n";
    }
}

QByteArray writeMatrix(const QTransform& matrix)
{
    return formatNumber(matrix.m11()) + ' ' + formatNumber(matrix.m12()) + ' ' +
           formatNumber(matrix.m21()) + ' ' + formatNumber(matrix.m22()) + ' ' +
           formatNumber(matrix.dx()) + ' ' + formatNumber(matrix.dy());
}

PDFObject createFlateStream(PDFDictionaryBuilder dictionary, const QByteArray& data)
{
    dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
    QByteArray compressed = PDFFlateDecodeFilter::compress(data);
    return PDFObject::createStream(PDFStream(std::move(dictionary), std::move(compressed)));
}

PDFObject createNumberArray(std::initializer_list<PDFReal> values)
{
    PDFArrayBuilder array;
    for (PDFReal value : values)
    {
        array.appendItem(PDFObject::createReal(value));
    }
    return PDFObject::createArray(std::move(array));
}

// ---------------------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------------------

/// Returns true, if the path (in page coordinates) touches the area. Degenerate paths (with
/// zero width or height - a glyph of zero size, a collapsed image) are tested as a small
/// rectangle around them, so they are never "invisible" to the test.
bool hitsArea(const QPainterPath& area, const QRectF& areaBounds, const QPainterPath& path)
{
    if (area.isEmpty() || path.elementCount() == 0)
    {
        return false;
    }

    constexpr PDFReal degenerateSize = 0.01;
    constexpr PDFReal inflation = 0.05;

    QRectF bounds = path.controlPointRect();
    const bool degenerate = bounds.width() < degenerateSize || bounds.height() < degenerateSize;
    if (degenerate)
    {
        bounds.adjust(-inflation, -inflation, inflation, inflation);
    }

    if (!bounds.intersects(areaBounds))
    {
        return false;
    }

    if (degenerate)
    {
        QPainterPath rectanglePath;
        rectanglePath.addRect(bounds);
        return area.intersects(rectanglePath);
    }

    // The area of the path could still be zero (a line with fill operator): then nothing is
    // painted, but we use the bounding rectangle in that case, to be on the safe side.
    if (area.intersects(path))
    {
        return true;
    }

    return false;
}

bool hitsArea(const QPainterPath& area, const QRectF& areaBounds, const QPolygonF& polygon)
{
    QPainterPath path;
    path.addPolygon(polygon);
    path.closeSubpath();

    // A polygon with a negligible area (glyph of a font with zero size, zero horizontal
    // scaling...) is tested by its bounding rectangle, inflated if necessary.
    PDFReal doubleArea = 0.0;
    for (int i = 0, count = polygon.size(); i < count; ++i)
    {
        const QPointF& a = polygon[i];
        const QPointF& b = polygon[(i + 1) % count];
        doubleArea += a.x() * b.y() - b.x() * a.y();
    }

    if (std::abs(doubleArea) < 1.0e-6)
    {
        QRectF bounds = polygon.boundingRect();
        bounds.adjust(-0.05, -0.05, 0.05, 0.05);
        QPainterPath rectanglePath;
        rectanglePath.addRect(bounds);
        return hitsArea(area, areaBounds, rectanglePath);
    }

    return hitsArea(area, areaBounds, path);
}

QPolygonF mapRectangle(const QTransform& matrix, const QRectF& rectangle)
{
    QPolygonF polygon;
    polygon << rectangle.topLeft() << rectangle.topRight() << rectangle.bottomRight() << rectangle.bottomLeft();
    return matrix.map(polygon);
}

// ---------------------------------------------------------------------------------------
// Safe copy of objects into the new document
// ---------------------------------------------------------------------------------------

/// Copies objects from the source document into the new document. It never follows
/// references to objects, which are not page content (pages, annotations, structure tree,
/// files, actions...) - such references are replaced by null, so no hidden part of the
/// original document can be dragged into the redacted one through a resource.
class SafeCopier
{
public:
    explicit SafeCopier(const PDFDocument* document, PDFDocumentBuilder* builder) :
        m_document(document),
        m_builder(builder)
    {

    }

    PDFObject copy(const PDFObject& object) { return copyImpl(object, 0); }

    /// Copies a dictionary, but leaves out given keys (direct dictionaries only)
    PDFObject copyDictionaryWithout(const PDFDictionary* dictionary, const std::set<QByteArray>& keys)
    {
        PDFDictionaryBuilder result;
        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            const QByteArray key = dictionary->getKey(i).getString();
            if (keys.count(key) || isDroppedKey(key))
            {
                continue;
            }
            result.addEntry(PDFInplaceOrMemoryString(key), copyImpl(dictionary->getValue(i), 1));
        }
        return PDFObject::createDictionary(std::move(result));
    }

private:
    static bool isDroppedKey(const QByteArray& key)
    {
        static const std::set<QByteArray> droppedKeys = {
            "P", "Parent", "PieceInfo", "Metadata", "Alternates", "OPI", "AF", "Thumb",
            "StructParent", "StructParents", "Popup", "IRT", "A", "AA", "Length", "Ref"
        };
        return droppedKeys.count(key);
    }

    bool isForbidden(const PDFObject& object) const
    {
        const PDFDictionary* dictionary = nullptr;
        if (object.isDictionary())
        {
            dictionary = object.getDictionary();
        }
        else if (object.isStream())
        {
            dictionary = object.getStream()->getDictionary();
        }

        if (!dictionary)
        {
            return false;
        }

        const PDFObject& type = m_document->getObject(dictionary->get("Type"));
        if (!type.isName())
        {
            return false;
        }

        static const std::set<QByteArray> forbiddenTypes = {
            "Page", "Pages", "Annot", "Catalog", "StructTreeRoot", "StructElem", "Outlines",
            "Filespec", "EmbeddedFile", "Sig", "ObjStm", "XRef", "Action", "Thread", "Bead",
            "MCR", "OBJR", "Template", "Collection"
        };
        return forbiddenTypes.count(type.getString());
    }

    PDFObject copyImpl(const PDFObject& object, int depth)
    {
        if (depth > 64)
        {
            return PDFObject();
        }

        switch (object.getType())
        {
            case PDFObject::Type::Reference:
            {
                const PDFObjectReference reference = object.getReference();
                auto it = m_mapping.find(reference);
                if (it != m_mapping.cend())
                {
                    return it->second.isValid() ? PDFObject::createReference(it->second) : PDFObject();
                }

                const PDFObject& target = m_document->getObjectByReference(reference);
                if (target.isNull() || isForbidden(target))
                {
                    m_mapping[reference] = PDFObjectReference();
                    return PDFObject();
                }

                const PDFObjectReference newReference = m_builder->addObject(PDFObject());
                m_mapping[reference] = newReference;
                m_builder->setObject(newReference, copyImpl(target, 0));
                return PDFObject::createReference(newReference);
            }

            case PDFObject::Type::Array:
            {
                PDFArrayBuilder array;
                for (const PDFObject& item : *object.getArray())
                {
                    array.appendItem(copyImpl(item, depth + 1));
                }
                return PDFObject::createArray(std::move(array));
            }

            case PDFObject::Type::Dictionary:
            {
                if (isForbidden(object))
                {
                    return PDFObject();
                }
                return copyDictionaryWithout(object.getDictionary(), { });
            }

            case PDFObject::Type::Stream:
            {
                if (isForbidden(object))
                {
                    return PDFObject();
                }

                const PDFStream* stream = object.getStream();
                PDFDictionaryBuilder dictionary;
                const PDFDictionary* sourceDictionary = stream->getDictionary();
                for (size_t i = 0; i < sourceDictionary->getCount(); ++i)
                {
                    const QByteArray key = sourceDictionary->getKey(i).getString();
                    if (isDroppedKey(key))
                    {
                        continue;
                    }
                    dictionary.addEntry(PDFInplaceOrMemoryString(key), copyImpl(sourceDictionary->getValue(i), depth + 1));
                }
                QByteArray content = *stream->getContent();
                return PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content)));
            }

            default:
                return object;
        }
    }

    const PDFDocument* m_document;
    PDFDocumentBuilder* m_builder;
    std::map<PDFObjectReference, PDFObjectReference> m_mapping;
};

}   // namespace

// ---------------------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------------------

class PDFFireRedaction::Impl
{
public:
    explicit Impl(const PDFDocument* document,
                  const PDFFontCache* fontCache,
                  const PDFCMS* cms,
                  const PDFMeshQualitySettings* meshQualitySettings,
                  PDFDocumentBuilder* builder) :
        document(document),
        fontCache(fontCache),
        cms(cms),
        meshQualitySettings(meshQualitySettings),
        builder(builder),
        copier(document, builder)
    {

    }

    const PDFDocument* document;
    const PDFFontCache* fontCache;
    const PDFCMS* cms;
    const PDFMeshQualitySettings* meshQualitySettings;
    PDFDocumentBuilder* builder;
    SafeCopier copier;
    int nameCounter = 0;

    /// Appends a content stream to the page (after its content) with given XObject resources
    void appendContentToPage(PDFObjectReference page, const QByteArray& content, const std::map<QByteArray, PDFObject>& xobjects);
};

namespace
{

/// Kinds of resources
constexpr const char* RES_FONT = "Font";
constexpr const char* RES_XOBJECT = "XObject";
constexpr const char* RES_EXTGSTATE = "ExtGState";
constexpr const char* RES_COLORSPACE = "ColorSpace";
constexpr const char* RES_PATTERN = "Pattern";
constexpr const char* RES_SHADING = "Shading";
constexpr const char* RES_PROPERTIES = "Properties";

/// Keys of the marked content properties, which hold text (alternate description,
/// replacement text, expansion) - they could tell the redacted text.
const std::set<QByteArray>& getMarkedContentTextKeys()
{
    static const std::set<QByteArray> keys = { "ActualText", "Alt", "E" };
    return keys;
}

/// Result of the redaction of an image
struct ImageResult
{
    enum class Status
    {
        Ok,     ///< New image was created
        Drop,   ///< Image must be dropped entirely
        Fail    ///< Image can't be redacted, page must be converted to outlines
    };

    Status status = Status::Fail;
    PDFObject image;
    QString error;
};

/// Segment of a path (user space)
struct PathSegment
{
    bool isCurve = false;
    QPointF p0;
    QPointF c1;
    QPointF c2;
    QPointF p1;

    QPointF evaluate(PDFReal t) const
    {
        if (!isCurve)
        {
            return p0 + (p1 - p0) * t;
        }
        const PDFReal u = 1.0 - t;
        return p0 * (u * u * u) + c1 * (3.0 * u * u * t) + c2 * (3.0 * u * t * t) + p1 * (t * t * t);
    }

    PDFReal length() const
    {
        if (!isCurve)
        {
            return QLineF(p0, p1).length();
        }
        PDFReal result = 0.0;
        QPointF last = p0;
        for (int i = 1; i <= 64; ++i)
        {
            const QPointF point = evaluate(i / 64.0);
            result += QLineF(last, point).length();
            last = point;
        }
        return result;
    }
};

/// Subpath (user space) - closing is remembered, because it changes the stroke (join instead of caps)
struct Subpath
{
    QPointF start;
    std::vector<PathSegment> segments;
    bool closed = false;
};

/// Content stream filter of a single page
class RedactionFilter : public PDFPageContentProcessor
{
public:
    explicit RedactionFilter(const PDFPage* page,
                             PDFFireRedaction::Impl* owner,
                             const QPainterPath& area);

    struct Context
    {
        QByteArray content;
        std::map<QByteArray, std::map<QByteArray, PDFObject>> resources;

        /// Depth of the operators being processed (0 = no operator is being processed,
        /// 1 = operator of this content stream, > 1 = nested operator, for example in
        /// a glyph of Type 3 font or in a tiling pattern - these are ignored)
        int depth = 0;

        Operator op = Operator::Invalid;
        QByteArray command;
        std::vector<PDFObject> operands;
        bool operandsValid = false;

        // Path being constructed
        QByteArray pathOperators;
        QPainterPath path;
        std::vector<Subpath> subpaths;
        QPointF currentPoint;
        bool hasCurrentPoint = false;
        bool clip = false;
        bool clipEvenOdd = false;

        // Balance of the operators
        int saveDepth = 0;
        int markedContentDepth = 0;
        int compatibilityDepth = 0;
        bool inText = false;

        /// Open constructs (q = 'q', marked content = 'm', compatibility = 'x') in the order of opening
        QByteArray openStack;

        /// Balance of q/Q of the nested operators of the current operator (glyphs of Type 3 fonts,
        /// patterns) - the processor executes them, so they must be balanced
        int nestedSaveBalance = 0;

        /// Text matrix and text line matrix, as the PDF specification defines them (the text
        /// position in the redacted page is written explicitly, so all viewers use these)
        QTransform textMatrix;
        QTransform textLineMatrix;

        // XObject being painted
        enum class XObjectAction
        {
            None,
            Emit,
            Drop,
            AwaitImage,
            Form
        };
        XObjectAction xobjectAction = XObjectAction::None;
        QByteArray xobjectName;
        PDFObject pendingFontObject;

        // Form XObject (this context is a form, which will be written as a new form)
        bool isForm = false;
        const PDFStream* formStream = nullptr;
    };

    /// Processes the page. Returns false, if the page can't be filtered safely.
    bool run(QString* failureReason);

    Context* getRootContext() { return m_contexts.empty() ? nullptr : m_contexts.front().get(); }

    /// Creates resource dictionary from the resources of the context
    static PDFObject createResources(const Context& context);

    /// Returns closing operators of the context (unclosed text object, marked content, saved states)
    static QByteArray getClosingOperators(const Context& context);

protected:
    virtual void performInterceptInstruction(Operator currentOperator, ProcessOrder processOrder, const QByteArray& operatorAsText) override;
    virtual bool performOriginalImagePainting(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference) override;
    virtual bool isContentSuppressedByOC(PDFObjectReference ocgOrOcmd) override;
    virtual bool isContentKindSuppressed(ContentKind kind) const override;

private:
    struct GlyphCode
    {
        QByteArray bytes;
        PDFReal width = 0.0;    ///< Horizontal displacement in text space units for font size 1
        bool isSpace = false;   ///< Single byte code 32 (word spacing is applied)
        bool approximateWidth = false; ///< Width is from the font program (font without widths), viewers can use other metrics
    };

    Context& top() { return *m_contexts.back(); }
    const Context& top() const { return *m_contexts.back(); }

    void setFailed(const QString& reason);

    void handleOperator(Context& context);
    bool checkOperands(const Context& context) const;
    bool isSafeFunction(const PDFObject& function, int depth = 0) const;
    void handleOperatorEnd(Context& context);

    void emitOperator(Context& context);
    void emitOperator(Context& context, const std::vector<PDFObject>& operands, const QByteArray& command);

    void registerResource(Context& context, const char* category, const QByteArray& name);
    QByteArray createUniqueName(Context& context, const char* category, const char* prefix);
    const PDFDictionary* getResourceDictionary(const char* category) const;

    void handlePathConstruction(Context& context);
    void handlePathPainting(Context& context);
    QPainterPath getStrokeOutline(const QPainterPath& path) const;
    bool cutStroke(const std::vector<Subpath>& subpaths, QByteArray& result) const;
    void getRemovedIntervals(const QPointF& a, const QPointF& b, std::vector<std::pair<PDFReal, PDFReal>>& intervals) const;
    bool isUnsafePattern(const PDFAbstractColorSpace* colorSpace) const;

    void handleText(Context& context);
    bool splitCodes(const PDFFont* font, const PDFObject& fontObject, const QByteArray& string, PDFReal fontSize,
                    const PDFRealizedFontPointer& realizedFont, std::vector<GlyphCode>& codes, QString* error);
    QRectF getGlyphInkRectangle(const PDFRealizedFontPointer& realizedFont, const QByteArray& code);
    static bool isMetricCompatibleFont(const PDFFont* font, const PDFRealizedFontPointer& realizedFont);

    void handleXObject(Context& context);
    void finishForm();

    ImageResult redactImage(const PDFImage& image, const PDFStream* stream, bool isInlineImage);
    PDFObject createInlineImageXObject(const PDFStream* stream);
    PDFObject resolveImageColorSpace(const PDFObject& colorSpace, bool isInlineImage);
    std::vector<uint8_t> getCoverage(int width, int height, const QTransform& pixelToPage, bool* ok) const;
    PDFObject redactMaskStream(const PDFStream* stream, bool isSoftMask, const QTransform& ctm, QString* error);

    PDFFireRedaction::Impl* m_owner;
    QPainterPath m_area;
    QRectF m_areaBounds;
    QList<QPolygonF> m_areaPolygons;
    bool m_hasArea = false;
    bool m_failed = false;
    QString m_failureReason;
    std::vector<std::unique_ptr<Context>> m_contexts;
    /// Font dictionaries of the fonts (the font is held, so its address can't be reused by another font)
    std::map<const PDFFont*, std::pair<PDFFontPointer, PDFObject>> m_fontObjects;
    std::map<std::pair<const void*, QByteArray>, QRectF> m_glyphInkCache;
    /// Realized fonts used as keys of the cache (held for the same reason)
    std::map<const void*, PDFRealizedFontPointer> m_realizedFonts;
    PDFRenderErrorReporterDummy m_dummyReporter;
};

RedactionFilter::RedactionFilter(const PDFPage* page, PDFFireRedaction::Impl* owner, const QPainterPath& area) :
    PDFPageContentProcessor(page, owner->document, owner->fontCache, owner->cms, nullptr, QTransform(), *owner->meshQualitySettings),
    m_owner(owner),
    m_area(area),
    m_areaBounds(area.boundingRect()),
    m_areaPolygons(area.toSubpathPolygons()),
    m_hasArea(!area.isEmpty())
{
    m_contexts.emplace_back(std::make_unique<Context>());
}

bool RedactionFilter::run(QString* failureReason)
{
    try
    {
        processContents();
    }
    catch (const PDFException& exception)
    {
        setFailed(exception.getMessage());
    }
    catch (const std::exception& exception)
    {
        setFailed(QString::fromLocal8Bit(exception.what()));
    }

    if (!m_failed && m_contexts.size() != 1)
    {
        setFailed(PDFTranslationContext::tr("Processing of a form XObject was not finished."));
    }

    if (m_failed && failureReason)
    {
        *failureReason = m_failureReason;
    }

    return !m_failed;
}

void RedactionFilter::setFailed(const QString& reason)
{
    if (!m_failed)
    {
        m_failed = true;
        m_failureReason = reason;
    }
}

bool RedactionFilter::isContentSuppressedByOC(PDFObjectReference ocgOrOcmd)
{
    // Content of all optional content groups (also of the hidden ones) is processed,
    // hidden content in the area must be removed as well.
    Q_UNUSED(ocgOrOcmd);
    return false;
}

bool RedactionFilter::isContentKindSuppressed(ContentKind kind) const
{
    switch (kind)
    {
        case ContentKind::Shapes:
        case ContentKind::Text:
        case ContentKind::Shading:
        case ContentKind::Tiling:
            // We do not need to paint anything
            return true;

        case ContentKind::Images:
        {
            // Image is needed only, if it is an inline image of the content stream being
            // filtered, or an image XObject, which must be redacted.
            const Context& context = top();
            if (context.depth == 0)
            {
                return false;
            }
            return !(context.depth == 1 && context.op == Operator::PaintXObject && context.xobjectAction == Context::XObjectAction::AwaitImage);
        }

        case ContentKind::Forms:
        {
            // Only forms, which are being filtered, are processed
            const Context& context = top();
            return !(context.isForm && context.depth == 0);
        }

        default:
            break;
    }

    return false;
}

const PDFDictionary* RedactionFilter::getResourceDictionary(const char* category) const
{
    if (category == RES_FONT) return getFontDictionary();
    if (category == RES_XOBJECT) return getXObjectDictionary();
    if (category == RES_EXTGSTATE) return getExtendedGraphicStateDictionary();
    if (category == RES_COLORSPACE) return getColorSpaceDictionary();
    if (category == RES_PATTERN) return getPatternDictionary();
    if (category == RES_SHADING) return getShadingDictionary();
    if (category == RES_PROPERTIES) return getPropertiesDictionary();
    return nullptr;
}

void RedactionFilter::registerResource(Context& context, const char* category, const QByteArray& name)
{
    auto& categoryResources = context.resources[category];
    if (categoryResources.count(name))
    {
        return;
    }

    const PDFDictionary* dictionary = getResourceDictionary(category);
    if (!dictionary || !dictionary->hasKey(name))
    {
        // Missing resource - the operator is invalid anyway
        return;
    }

    const PDFObject& object = dictionary->get(name);
    if (category == RES_PROPERTIES && m_hasArea)
    {
        // Text in the marked content properties could tell the redacted text
        const PDFObject& dereferenced = getDocument()->getObject(object);
        if (dereferenced.isDictionary())
        {
            categoryResources[name] = m_owner->copier.copyDictionaryWithout(dereferenced.getDictionary(), getMarkedContentTextKeys());
            return;
        }
    }

    categoryResources[name] = m_owner->copier.copy(object);
}

QByteArray RedactionFilter::createUniqueName(Context& context, const char* category, const char* prefix)
{
    const PDFDictionary* dictionary = getResourceDictionary(category);
    while (true)
    {
        QByteArray name = QByteArray(prefix) + QByteArray::number(++m_owner->nameCounter);
        if (context.resources[category].count(name) || (dictionary && dictionary->hasKey(name)))
        {
            continue;
        }
        return name;
    }
}

PDFObject RedactionFilter::createResources(const Context& context)
{
    PDFDictionaryBuilder resources;
    for (const auto& [category, items] : context.resources)
    {
        if (items.empty())
        {
            continue;
        }

        PDFDictionaryBuilder categoryDictionary;
        for (const auto& [name, object] : items)
        {
            categoryDictionary.addEntry(PDFInplaceOrMemoryString(name), object);
        }
        resources.addEntry(PDFInplaceOrMemoryString(category), PDFObject::createDictionary(std::move(categoryDictionary)));
    }
    return PDFObject::createDictionary(std::move(resources));
}

QByteArray RedactionFilter::getClosingOperators(const Context& context)
{
    QByteArray result;
    if (!context.inText && context.openStack.isEmpty())
    {
        return result;
    }

    result += "\n";
    if (context.inText)
    {
        result += "ET\n";
    }
    for (qsizetype i = context.openStack.size() - 1; i >= 0; --i)
    {
        switch (context.openStack[i])
        {
            case 'q': result += "Q\n"; break;
            case 'm': result += "EMC\n"; break;
            case 'x': result += "EX\n"; break;
            default: break;
        }
    }
    return result;
}

void RedactionFilter::performInterceptInstruction(Operator currentOperator, ProcessOrder processOrder, const QByteArray& operatorAsText)
{
    if (m_contexts.empty())
    {
        return;
    }

    Context& context = top();

    if (processOrder == ProcessOrder::BeforeOperation)
    {
        if (context.depth > 0)
        {
            // Nested operator (glyph of Type 3 font, tiling pattern, soft mask...). The processor
            // executes it - if its q/Q are not balanced, the state of the processor would not
            // be the state, which the viewers have, after the operator.
            ++context.depth;
            if (currentOperator == Operator::SaveGraphicState)
            {
                ++context.nestedSaveBalance;
            }
            else if (currentOperator == Operator::RestoreGraphicState)
            {
                if (--context.nestedSaveBalance < 0 && m_hasArea)
                {
                    setFailed(PDFTranslationContext::tr("Graphic state is restored more times than saved."));
                }
            }
            return;
        }

        context.nestedSaveBalance = 0;

        context.depth = 1;
        context.op = currentOperator;
        context.command = operatorAsText;
        context.operands.clear();
        context.operandsValid = true;

        try
        {
            const auto& operands = getOperands();
            size_t position = 0;
            auto fetcher = [&operands, &position]() -> PDFLexicalAnalyzer::Token
            {
                if (position < operands.size())
                {
                    return operands[position++];
                }
                return PDFLexicalAnalyzer::Token();
            };

            PDFParser parser(fetcher);
            while (parser.lookahead().type != PDFLexicalAnalyzer::TokenType::EndOfFile)
            {
                context.operands.push_back(parser.getObject());
            }
        }
        catch (const PDFException&)
        {
            context.operandsValid = false;
        }

        if (!m_failed)
        {
            // The processor calls this function before it guards the call of the function after
            // the operation, an exception here would leave the depth of the context wrong.
            try
            {
                handleOperator(context);
            }
            catch (const PDFException& exception)
            {
                setFailed(exception.getMessage());
            }
            catch (const std::exception& exception)
            {
                setFailed(QString::fromLocal8Bit(exception.what()));
            }
        }
        return;
    }

    // After operation
    if (context.depth == 0)
    {
        // This is the end of the operator, which painted the form being filtered
        // in the context below this one.
        if (context.isForm && m_contexts.size() > 1)
        {
            finishForm();
            Context& parentContext = top();
            parentContext.depth = 0;
            parentContext.xobjectAction = Context::XObjectAction::None;
        }
        return;
    }

    --context.depth;
    if (context.depth == 0)
    {
        if (context.nestedSaveBalance != 0 && m_hasArea)
        {
            setFailed(PDFTranslationContext::tr("Graphic state is not restored in a glyph or a pattern."));
        }
        context.nestedSaveBalance = 0;

        if (!m_failed)
        {
            try
            {
                handleOperatorEnd(context);
            }
            catch (const PDFException& exception)
            {
                setFailed(exception.getMessage());
            }
        }
    }
}

void RedactionFilter::emitOperator(Context& context)
{
    emitOperator(context, context.operands, context.command);
}

void RedactionFilter::emitOperator(Context& context, const std::vector<PDFObject>& operands, const QByteArray& command)
{
    for (const PDFObject& operand : operands)
    {
        writeObject(context.content, operand);
        context.content += ' ';
    }
    context.content += command;
    context.content += '\n';
}

void RedactionFilter::handleOperator(Context& context)
{
    if (!context.operandsValid || (m_hasArea && !checkOperands(context)))
    {
        if (m_hasArea)
        {
            // The processor executes the operator (with the operands it can read), other viewers
            // can execute it differently - positions of the content would not be known.
            setFailed(PDFTranslationContext::tr("The page has an operator with invalid operands (%1).").arg(QString::fromLatin1(context.command)));
        }
        return;
    }

    auto operandName = [&context](size_t index) -> QByteArray
    {
        if (index < context.operands.size() && context.operands[index].isName())
        {
            return context.operands[index].getString();
        }
        return QByteArray();
    };

    switch (context.op)
    {
        case Operator::MoveCurrentPoint:
        case Operator::LineTo:
        case Operator::Bezier123To:
        case Operator::Bezier23To:
        case Operator::Bezier13To:
        case Operator::EndSubpath:
        case Operator::Rectangle:
            handlePathConstruction(context);
            return;

        case Operator::ClipWinding:
        case Operator::ClipEvenOdd:
            context.clip = true;
            context.clipEvenOdd = context.op == Operator::ClipEvenOdd;
            return;

        case Operator::PathStroke:
        case Operator::PathCloseStroke:
        case Operator::PathFillWinding:
        case Operator::PathFillWinding2:
        case Operator::PathFillEvenOdd:
        case Operator::PathFillStrokeWinding:
        case Operator::PathFillStrokeEvenOdd:
        case Operator::PathCloseFillStrokeWinding:
        case Operator::PathCloseFillStrokeEvenOdd:
        case Operator::PathClear:
            handlePathPainting(context);
            return;

        case Operator::SaveGraphicState:
            ++context.saveDepth;
            context.openStack += 'q';
            emitOperator(context);
            return;

        case Operator::RestoreGraphicState:
            if (context.saveDepth > 0)
            {
                --context.saveDepth;
                const qsizetype index = context.openStack.lastIndexOf('q');
                if (index >= 0)
                {
                    context.openStack.remove(index, 1);
                }
                emitOperator(context);
            }
            else if (m_hasArea && context.isForm)
            {
                // In a form, the processor would restore the state saved before the form
                setFailed(PDFTranslationContext::tr("Graphic state is restored more times than saved."));
            }
            return;

        case Operator::TextBegin:
            context.textMatrix = QTransform();
            context.textLineMatrix = QTransform();
            if (!context.inText)
            {
                context.inText = true;
                emitOperator(context);
            }
            return;

        case Operator::TextMoveByOffset:
        case Operator::TextSetLeadingAndMoveByOffset:
        case Operator::TextSetMatrix:
        case Operator::TextMoveByLeading:
        {
            if (!m_hasArea)
            {
                emitOperator(context);
                return;
            }

            // The text position is tracked, it is written explicitly before each text operator
            PDFDocumentDataLoaderDecorator loader(getDocument());
            std::vector<PDFReal> numbers;
            for (const PDFObject& operand : context.operands)
            {
                numbers.push_back(loader.readNumber(operand, 0.0));
            }

            switch (context.op)
            {
                case Operator::TextMoveByOffset:
                    context.textLineMatrix = QTransform(1, 0, 0, 1, numbers[0], numbers[1]) * context.textLineMatrix;
                    break;
                case Operator::TextSetLeadingAndMoveByOffset:
                    context.textLineMatrix = QTransform(1, 0, 0, 1, numbers[0], numbers[1]) * context.textLineMatrix;
                    context.content += formatNumber(-numbers[1]) + " TL\n";
                    break;
                case Operator::TextSetMatrix:
                    context.textLineMatrix = QTransform(numbers[0], numbers[1], numbers[2], numbers[3], numbers[4], numbers[5]);
                    break;
                default:
                    context.textLineMatrix = QTransform(1, 0, 0, 1, 0, -getGraphicState()->getTextLeading()) * context.textLineMatrix;
                    break;
            }
            context.textMatrix = context.textLineMatrix;
            return;
        }

        case Operator::TextEnd:
            if (context.inText)
            {
                context.inText = false;
                emitOperator(context);
            }
            return;

        case Operator::TextSetFontAndFontSize:
        {
            const QByteArray name = operandName(0);
            registerResource(context, RES_FONT, name);
            const PDFDictionary* fonts = getFontDictionary();
            context.pendingFontObject = fonts ? fonts->get(name) : PDFObject();
            emitOperator(context);
            return;
        }

        case Operator::SetGraphicState:
        {
            const QByteArray name = operandName(0);
            const PDFDictionary* states = getExtendedGraphicStateDictionary();
            if (states && m_hasArea)
            {
                const PDFDictionary* state = getDocument()->getDictionaryFromObject(states->get(name));
                if (state)
                {
                    const PDFObject& softMask = getDocument()->getObject(state->get("SMask"));
                    if (softMask.isDictionary())
                    {
                        // Soft mask is a content stream of its own, which can't be filtered
                        setFailed(PDFTranslationContext::tr("The page uses a soft mask."));
                        return;
                    }
                }
            }
            registerResource(context, RES_EXTGSTATE, name);
            emitOperator(context);
            return;
        }

        case Operator::ColorSetStrokingColorSpace:
        case Operator::ColorSetFillingColorSpace:
        {
            const QByteArray name = operandName(0);
            const PDFDictionary* colorSpaces = getColorSpaceDictionary();
            if (colorSpaces && colorSpaces->hasKey(name))
            {
                registerResource(context, RES_COLORSPACE, name);
            }
            emitOperator(context);
            return;
        }

        case Operator::ColorSetStrokingColorN:
        case Operator::ColorSetFillingColorN:
        {
            if (!context.operands.empty() && context.operands.back().isName())
            {
                registerResource(context, RES_PATTERN, context.operands.back().getString());
            }
            emitOperator(context);
            return;
        }

        case Operator::ShadingPaintShape:
        {
            const QByteArray name = operandName(0);
            const PDFDictionary* shadings = getShadingDictionary();
            if (m_hasArea && shadings)
            {
                // Function based and mesh shadings can hold a picture - if such shading
                // can be painted into the area, it can't be filtered.
                const PDFObject& shadingObject = getDocument()->getObject(shadings->get(name));
                const PDFDictionary* shading = shadingObject.isStream() ? shadingObject.getStream()->getDictionary() : getDocument()->getDictionaryFromObject(shadingObject);
                if (shading)
                {
                    PDFDocumentDataLoaderDecorator loader(getDocument());
                    const PDFInteger shadingType = loader.readIntegerFromDictionary(shading, "ShadingType", 0);
                    const QTransform& ctm = getGraphicState()->getCurrentTransformationMatrix();
                    const QRectF boundingBox = loader.readRectangle(shading->get("BBox"), QRectF());
                    const bool canHit = !boundingBox.isValid() || hitsArea(m_area, m_areaBounds, mapRectangle(ctm, boundingBox));
                    if (canHit)
                    {
                        if ((shadingType != 2 && shadingType != 3) || !isSafeFunction(shading->get("Function")))
                        {
                            setFailed(PDFTranslationContext::tr("A shading, which can hold a picture, is painted on a redacted page."));
                            return;
                        }

                        // The gradient is not painted in the area (clipping path is the whole
                        // plane without the area)
                        bool invertible = false;
                        const QTransform inverted = ctm.inverted(&invertible);
                        if (!invertible)
                        {
                            return;
                        }
                        QPainterPath outside;
                        outside.addRect(m_areaBounds.united(getPage()->getMediaBox()).adjusted(-10000, -10000, 10000, 10000));
                        outside = PDFPathBoolean::subtract(outside, m_area);
                        registerResource(context, RES_SHADING, name);
                        context.content += "q\n";
                        writePath(context.content, inverted.map(outside));
                        context.content += outside.fillRule() == Qt::OddEvenFill ? "W* n\n" : "W n\n";
                        emitOperator(context);
                        context.content += "Q\n";
                        return;
                    }
                }
            }
            registerResource(context, RES_SHADING, name);
            emitOperator(context);
            return;
        }

        case Operator::MarkedContentPointWithProperties:
        case Operator::MarkedContentBeginWithProperties:
        {
            if (context.operands.size() == 2)
            {
                if (context.operands[1].isName())
                {
                    registerResource(context, RES_PROPERTIES, context.operands[1].getString());
                }
                else if (context.operands[1].isDictionary() && m_hasArea)
                {
                    // Inline properties: the text keys are removed
                    PDFDictionaryBuilder properties;
                    const PDFDictionary* dictionary = context.operands[1].getDictionary();
                    for (size_t i = 0; i < dictionary->getCount(); ++i)
                    {
                        const QByteArray key = dictionary->getKey(i).getString();
                        if (!getMarkedContentTextKeys().count(key))
                        {
                            properties.addEntry(PDFInplaceOrMemoryString(key), dictionary->getValue(i));
                        }
                    }
                    context.operands[1] = PDFObject::createDictionary(std::move(properties));
                }
            }

            if (context.op == Operator::MarkedContentBeginWithProperties)
            {
                ++context.markedContentDepth;
                context.openStack += 'm';
            }
            emitOperator(context);
            return;
        }

        case Operator::MarkedContentBegin:
            ++context.markedContentDepth;
            context.openStack += 'm';
            emitOperator(context);
            return;

        case Operator::MarkedContentEnd:
            if (context.markedContentDepth > 0)
            {
                --context.markedContentDepth;
                const qsizetype index = context.openStack.lastIndexOf('m');
                if (index >= 0)
                {
                    context.openStack.remove(index, 1);
                }
                emitOperator(context);
            }
            return;

        case Operator::CompatibilityBegin:
            ++context.compatibilityDepth;
            context.openStack += 'x';
            emitOperator(context);
            return;

        case Operator::CompatibilityEnd:
            if (context.compatibilityDepth > 0)
            {
                --context.compatibilityDepth;
                const qsizetype index = context.openStack.lastIndexOf('x');
                if (index >= 0)
                {
                    context.openStack.remove(index, 1);
                }
                emitOperator(context);
            }
            return;

        case Operator::TextShowTextString:
        case Operator::TextShowTextIndividualSpacing:
        case Operator::TextNextLineShowText:
        case Operator::TextSetSpacingAndShowText:
            handleText(context);
            return;

        case Operator::PaintXObject:
            handleXObject(context);
            return;

        case Operator::Invalid:
        case Operator::InlineImageBegin:
        case Operator::InlineImageData:
        case Operator::InlineImageEnd:
            // Unknown operators are never written - their operands could hold anything
            return;

        default:
            emitOperator(context);
            return;
    }
}

bool RedactionFilter::checkOperands(const Context& context) const
{
    const std::vector<PDFObject>& operands = context.operands;

    // Numbers far out of the page would be computed differently by different viewers
    std::function<bool(const PDFObject&)> isSane = [&isSane](const PDFObject& object) -> bool
    {
        if (object.isReal())
        {
            return std::isfinite(object.getReal()) && std::abs(object.getReal()) <= 1.0e7;
        }
        if (object.isInt())
        {
            return std::abs(object.getInteger()) <= 10000000;
        }
        if (object.isArray())
        {
            return std::all_of(object.getArray()->begin(), object.getArray()->end(), isSane);
        }
        return true;
    };
    if (!std::all_of(operands.cbegin(), operands.cend(), isSane))
    {
        return false;
    }

    auto isNumber = [](const PDFObject& object) { return object.isInt() || object.isReal(); };
    auto numbers = [&](size_t count) { return operands.size() == count && std::all_of(operands.cbegin(), operands.cend(), isNumber); };
    auto name = [&]() { return operands.size() == 1 && operands[0].isName(); };

    switch (context.op)
    {
        case Operator::SetLineWidth:
        case Operator::SetLineCap:
        case Operator::SetLineJoin:
        case Operator::SetMitterLimit:
        case Operator::SetFlatness:
        case Operator::TextSetCharacterSpacing:
        case Operator::TextSetWordSpacing:
        case Operator::TextSetHorizontalScale:
        case Operator::TextSetLeading:
        case Operator::TextSetRenderMode:
        case Operator::TextSetRise:
        case Operator::ColorSetDeviceGrayStroking:
        case Operator::ColorSetDeviceGrayFilling:
            return numbers(1);

        case Operator::SetLineDashPattern:
            return operands.size() == 2 && operands[0].isArray() && isNumber(operands[1]) &&
                   std::all_of(operands[0].getArray()->begin(), operands[0].getArray()->end(), isNumber);

        case Operator::SetRenderingIntent:
        case Operator::SetGraphicState:
        case Operator::ColorSetStrokingColorSpace:
        case Operator::ColorSetFillingColorSpace:
        case Operator::ShadingPaintShape:
        case Operator::PaintXObject:
        case Operator::MarkedContentPoint:
        case Operator::MarkedContentBegin:
            return name();

        case Operator::SaveGraphicState:
        case Operator::RestoreGraphicState:
        case Operator::EndSubpath:
        case Operator::PathStroke:
        case Operator::PathCloseStroke:
        case Operator::PathFillWinding:
        case Operator::PathFillWinding2:
        case Operator::PathFillEvenOdd:
        case Operator::PathFillStrokeWinding:
        case Operator::PathFillStrokeEvenOdd:
        case Operator::PathCloseFillStrokeWinding:
        case Operator::PathCloseFillStrokeEvenOdd:
        case Operator::PathClear:
        case Operator::ClipWinding:
        case Operator::ClipEvenOdd:
        case Operator::TextBegin:
        case Operator::TextEnd:
        case Operator::TextMoveByLeading:
        case Operator::MarkedContentEnd:
        case Operator::CompatibilityBegin:
        case Operator::CompatibilityEnd:
            return operands.empty();

        case Operator::AdjustCurrentTransformationMatrix:
        case Operator::Bezier123To:
        case Operator::TextSetMatrix:
        case Operator::Type3FontSetOffsetAndBB:
            return numbers(6);

        case Operator::MoveCurrentPoint:
        case Operator::LineTo:
        case Operator::TextMoveByOffset:
        case Operator::TextSetLeadingAndMoveByOffset:
        case Operator::Type3FontSetOffset:
            return numbers(2);

        case Operator::Bezier23To:
        case Operator::Bezier13To:
        case Operator::Rectangle:
            return numbers(4);

        case Operator::ColorSetDeviceRGBStroking:
        case Operator::ColorSetDeviceRGBFilling:
            return numbers(3);

        case Operator::ColorSetDeviceCMYKStroking:
        case Operator::ColorSetDeviceCMYKFilling:
            return numbers(4);

        case Operator::TextSetFontAndFontSize:
            return operands.size() == 2 && operands[0].isName() && isNumber(operands[1]);

        case Operator::ColorSetStrokingColor:
        case Operator::ColorSetFillingColor:
            return !operands.empty() && operands.size() <= 32 && std::all_of(operands.cbegin(), operands.cend(), isNumber);

        case Operator::ColorSetStrokingColorN:
        case Operator::ColorSetFillingColorN:
            return !operands.empty() && operands.size() <= 33 && std::all_of(operands.cbegin(), std::prev(operands.cend()), isNumber) &&
                   (isNumber(operands.back()) || operands.back().isName());

        case Operator::TextShowTextString:
        case Operator::TextNextLineShowText:
            return operands.size() == 1 && operands[0].isString();

        case Operator::TextShowTextIndividualSpacing:
            return operands.size() == 1 && operands[0].isArray() &&
                   std::all_of(operands[0].getArray()->begin(), operands[0].getArray()->end(), [&](const PDFObject& item) { return isNumber(item) || item.isString(); });

        case Operator::TextSetSpacingAndShowText:
            return operands.size() == 3 && isNumber(operands[0]) && isNumber(operands[1]) && operands[2].isString();

        case Operator::MarkedContentPointWithProperties:
        case Operator::MarkedContentBeginWithProperties:
            return operands.size() == 2 && operands[0].isName() && (operands[1].isName() || operands[1].isDictionary());

        default:
            return true;
    }
}

bool RedactionFilter::isSafeFunction(const PDFObject& functionObject, int depth) const
{
    // Exponential functions (and their stitching) are smooth gradients. Sampled and
    // PostScript functions can hold arbitrary data (a picture, text...).
    const PDFObject& function = getDocument()->getObject(functionObject);
    if (function.isArray())
    {
        for (const PDFObject& item : *function.getArray())
        {
            if (!isSafeFunction(item, depth + 1))
            {
                return false;
            }
        }
        return true;
    }

    const PDFDictionary* dictionary = function.isStream() ? function.getStream()->getDictionary() : getDocument()->getDictionaryFromObject(function);
    if (!dictionary || depth > 8)
    {
        return false;
    }

    PDFDocumentDataLoaderDecorator loader(getDocument());
    switch (loader.readIntegerFromDictionary(dictionary, "FunctionType", -1))
    {
        case 2:
            return true;
        case 3:
            return isSafeFunction(dictionary->get("Functions"), depth + 1);
        default:
            return false;
    }
}

void RedactionFilter::handleOperatorEnd(Context& context)
{
    switch (context.op)
    {
        case Operator::TextSetFontAndFontSize:
        {
            const PDFFontPointer& font = getGraphicState()->getTextFont();
            if (font)
            {
                m_fontObjects[font.get()] = std::make_pair(font, getDocument()->getObject(context.pendingFontObject));
            }
            context.pendingFontObject = PDFObject();
            break;
        }

        case Operator::SetGraphicState:
        {
            // Graphic state can set the font
            const PDFDictionary* states = getExtendedGraphicStateDictionary();
            if (states && !context.operands.empty() && context.operands.front().isName())
            {
                const PDFDictionary* state = getDocument()->getDictionaryFromObject(states->get(context.operands.front().getString()));
                if (state)
                {
                    const PDFObject& fontArray = getDocument()->getObject(state->get("Font"));
                    const PDFFontPointer& font = getGraphicState()->getTextFont();
                    if (fontArray.isArray() && fontArray.getArray()->getCount() >= 1 && font)
                    {
                        m_fontObjects[font.get()] = std::make_pair(font, getDocument()->getObject(fontArray.getArray()->getItem(0)));
                    }
                }
            }
            break;
        }

        case Operator::PaintXObject:
        {
            switch (context.xobjectAction)
            {
                case Context::XObjectAction::Emit:
                {
                    std::vector<PDFObject> operands = { PDFObject::createName(context.xobjectName) };
                    emitOperator(context, operands, "Do");
                    break;
                }

                case Context::XObjectAction::AwaitImage:
                    // Image was not decoded - it is not written. It can't be redacted.
                    break;

                default:
                    break;
            }
            context.xobjectAction = Context::XObjectAction::None;
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------------------

void RedactionFilter::handlePathConstruction(Context& context)
{
    std::vector<PDFReal> numbers;
    for (const PDFObject& operand : context.operands)
    {
        if (operand.isInt())
        {
            numbers.push_back(operand.getInteger());
        }
        else if (operand.isReal())
        {
            numbers.push_back(operand.getReal());
        }
        else
        {
            return;
        }
    }

    QPainterPath& path = context.path;
    const QPointF current = context.currentPoint;
    std::vector<Subpath>& subpaths = context.subpaths;

    // Segment can be appended only to an open subpath, after closing a subpath,
    // a new subpath starts at the start point of the closed one.
    auto appendSegment = [&](PathSegment segment)
    {
        if (subpaths.empty() || subpaths.back().closed)
        {
            Subpath subpath;
            subpath.start = current;
            subpaths.push_back(subpath);
        }
        subpaths.back().segments.push_back(segment);
        context.currentPoint = segment.p1;
    };

    switch (context.op)
    {
        case Operator::MoveCurrentPoint:
        {
            if (numbers.size() != 2) return;
            path.moveTo(numbers[0], numbers[1]);
            Subpath subpath;
            subpath.start = QPointF(numbers[0], numbers[1]);
            subpaths.push_back(subpath);
            context.currentPoint = subpath.start;
            context.hasCurrentPoint = true;
            break;
        }

        case Operator::LineTo:
        {
            if (numbers.size() != 2 || !context.hasCurrentPoint) return;
            path.lineTo(numbers[0], numbers[1]);
            PathSegment segment;
            segment.p0 = current;
            segment.p1 = QPointF(numbers[0], numbers[1]);
            appendSegment(segment);
            break;
        }

        case Operator::Bezier123To:
        case Operator::Bezier23To:
        case Operator::Bezier13To:
        {
            PathSegment segment;
            segment.isCurve = true;
            segment.p0 = current;
            if (context.op == Operator::Bezier123To)
            {
                if (numbers.size() != 6 || !context.hasCurrentPoint) return;
                segment.c1 = QPointF(numbers[0], numbers[1]);
                segment.c2 = QPointF(numbers[2], numbers[3]);
                segment.p1 = QPointF(numbers[4], numbers[5]);
            }
            else if (context.op == Operator::Bezier23To)
            {
                if (numbers.size() != 4 || !context.hasCurrentPoint) return;
                segment.c1 = current;
                segment.c2 = QPointF(numbers[0], numbers[1]);
                segment.p1 = QPointF(numbers[2], numbers[3]);
            }
            else
            {
                if (numbers.size() != 4 || !context.hasCurrentPoint) return;
                segment.c1 = QPointF(numbers[0], numbers[1]);
                segment.c2 = QPointF(numbers[2], numbers[3]);
                segment.p1 = QPointF(numbers[2], numbers[3]);
            }
            path.cubicTo(segment.c1, segment.c2, segment.p1);
            appendSegment(segment);
            break;
        }

        case Operator::EndSubpath:
            if (!numbers.empty()) return;
            path.closeSubpath();
            if (!subpaths.empty() && !subpaths.back().closed)
            {
                subpaths.back().closed = true;
                context.currentPoint = subpaths.back().start;
            }
            break;

        case Operator::Rectangle:
        {
            if (numbers.size() != 4) return;
            const PDFReal x = numbers[0];
            const PDFReal y = numbers[1];
            const PDFReal w = numbers[2];
            const PDFReal h = numbers[3];
            path.moveTo(x, y);
            path.lineTo(x + w, y);
            path.lineTo(x + w, y + h);
            path.lineTo(x, y + h);
            path.closeSubpath();

            Subpath subpath;
            subpath.start = QPointF(x, y);
            const QPointF corners[4] = { QPointF(x, y), QPointF(x + w, y), QPointF(x + w, y + h), QPointF(x, y + h) };
            for (int i = 0; i < 3; ++i)
            {
                PathSegment segment;
                segment.p0 = corners[i];
                segment.p1 = corners[i + 1];
                subpath.segments.push_back(segment);
            }
            subpath.closed = true;
            subpaths.push_back(subpath);
            context.currentPoint = QPointF(x, y);
            context.hasCurrentPoint = true;
            break;
        }

        default:
            return;
    }

    for (const PDFObject& operand : context.operands)
    {
        writeObject(context.pathOperators, operand);
        context.pathOperators += ' ';
    }
    context.pathOperators += context.command;
    context.pathOperators += '\n';
}

bool RedactionFilter::isUnsafePattern(const PDFAbstractColorSpace* colorSpace) const
{
    // Tiling patterns are content streams (they can hold anything), function based and
    // mesh shadings can hold a picture. Such pattern painted in the area can't be cut,
    // its data would stay in the file. Axial and radial shadings are plain gradients.
    if (const PDFPatternColorSpace* patternColorSpace = colorSpace ? colorSpace->asPatternColorSpace() : nullptr)
    {
        const PDFPattern* pattern = patternColorSpace->getPattern();
        if (!pattern || pattern->getType() != PatternType::Shading || !pattern->getShadingPattern())
        {
            return true;
        }

        const PDFShadingPattern* shading = pattern->getShadingPattern();
        const ShadingType shadingType = shading->getShadingType();
        if (shadingType != ShadingType::Axial && shadingType != ShadingType::Radial)
        {
            return true;
        }

        // Only exponential functions are smooth gradients for sure
        const PDFSingleDimensionShading* singleDimensionShading = dynamic_cast<const PDFSingleDimensionShading*>(shading);
        if (!singleDimensionShading)
        {
            return true;
        }
        for (const PDFFunctionPtr& function : singleDimensionShading->getFunctions())
        {
            if (!dynamic_cast<const PDFExponentialFunction*>(function.get()))
            {
                return true;
            }
        }
        return false;
    }
    return false;
}

QPainterPath RedactionFilter::getStrokeOutline(const QPainterPath& path) const
{
    // Conservative outline of the stroke (solid, the dashes are ignored)
    const PDFPageContentProcessorState* state = getGraphicState();
    const QTransform& ctm = state->getCurrentTransformationMatrix();

    const PDFReal scale = std::sqrt(std::abs(ctm.determinant()));
    const PDFReal minimalWidth = scale > 1.0e-9 ? 0.5 / scale : 0.5;
    PDFReal width = state->getLineWidth();
    if (!(width >= minimalWidth))
    {
        width = minimalWidth;
    }

    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(state->getLineCapStyle());
    stroker.setJoinStyle(state->getLineJoinStyle());
    stroker.setMiterLimit(qMax(1.0, state->getMitterLimit()));
    return stroker.createStroke(path);
}

void RedactionFilter::getRemovedIntervals(const QPointF& a, const QPointF& b, std::vector<std::pair<PDFReal, PDFReal>>& intervals) const
{
    // Parameters, where the segment a-b (page coordinates) crosses the boundary of the area,
    // then every part between two crossings is either inside, or outside of the area.
    std::vector<PDFReal> parameters = { 0.0, 1.0 };
    const QPointF direction = b - a;

    for (const QPolygonF& polygon : m_areaPolygons)
    {
        for (int i = 0, count = polygon.size(); i < count; ++i)
        {
            const QPointF c = polygon[i];
            const QPointF d = polygon[(i + 1) % count];
            const QPointF edge = d - c;
            const PDFReal denominator = direction.x() * edge.y() - direction.y() * edge.x();
            if (std::abs(denominator) < 1.0e-12)
            {
                continue;
            }
            const QPointF offset = c - a;
            const PDFReal t = (offset.x() * edge.y() - offset.y() * edge.x()) / denominator;
            const PDFReal u = (offset.x() * direction.y() - offset.y() * direction.x()) / denominator;
            if (t > 0.0 && t < 1.0 && u >= -1.0e-9 && u <= 1.0 + 1.0e-9)
            {
                parameters.push_back(t);
            }
        }
    }

    std::sort(parameters.begin(), parameters.end());
    for (size_t i = 0; i + 1 < parameters.size(); ++i)
    {
        const PDFReal t0 = parameters[i];
        const PDFReal t1 = parameters[i + 1];
        if (t1 - t0 < 1.0e-12)
        {
            continue;
        }

        const QPointF middle = a + direction * ((t0 + t1) * 0.5);
        if (m_area.contains(middle))
        {
            if (!intervals.empty() && std::abs(intervals.back().second - t0) < 1.0e-12)
            {
                intervals.back().second = t1;
            }
            else
            {
                intervals.emplace_back(t0, t1);
            }
        }
    }
}

bool RedactionFilter::cutStroke(const std::vector<Subpath>& subpaths, QByteArray& result) const
{
    // The parts of the stroked path, which are in the area, are removed. The rest stays
    // a stroke (with the same width, caps, joins, dashes and color), only the cut parts
    // of curves are flattened. Dashes of every new subpath start with the phase, which
    // they had at that place in the original path.
    const PDFPageContentProcessorState* state = getGraphicState();
    const QTransform& ctm = state->getCurrentTransformationMatrix();
    const PDFLineDashPattern& dashPattern = state->getLineDashPattern();

    struct Piece
    {
        PathSegment segment;
        bool removed = false;
        PDFReal offset = 0.0;   ///< Length of the path before this piece (user space)
    };

    struct Run
    {
        std::vector<PathSegment> segments;
        PDFReal offset = 0.0;
        bool closed = false;
    };

    std::vector<Run> runs;
    bool removedAny = false;

    for (const Subpath& subpath : subpaths)
    {
        std::vector<PathSegment> segments = subpath.segments;
        if (subpath.closed && !segments.empty() && QLineF(segments.back().p1, subpath.start).length() > 1.0e-9)
        {
            PathSegment closing;
            closing.p0 = segments.back().p1;
            closing.p1 = subpath.start;
            segments.push_back(closing);
        }

        std::vector<Piece> pieces;
        PDFReal offset = 0.0;
        for (const PathSegment& segment : segments)
        {
            QPolygonF controlPolygon;
            controlPolygon << segment.p0 << segment.p1;
            if (segment.isCurve)
            {
                controlPolygon << segment.c1 << segment.c2;
            }
            const QRectF bounds = ctm.map(controlPolygon).boundingRect().adjusted(-0.01, -0.01, 0.01, 0.01);

            if (!bounds.intersects(m_areaBounds))
            {
                Piece piece;
                piece.segment = segment;
                piece.offset = offset;
                pieces.push_back(piece);
                offset += segment.length();
                continue;
            }

            // Segment can be in the area - curves are flattened (in page space, the
            // segments are at most 0.5 points long)
            std::vector<QPointF> points;
            if (segment.isCurve)
            {
                const PDFReal pageLength = PathSegment{ true, ctm.map(segment.p0), ctm.map(segment.c1), ctm.map(segment.c2), ctm.map(segment.p1) }.length();
                const int count = qBound(4, int(std::ceil(pageLength / 0.5)), 2048);
                for (int i = 0; i <= count; ++i)
                {
                    points.push_back(segment.evaluate(PDFReal(i) / count));
                }
            }
            else
            {
                points = { segment.p0, segment.p1 };
            }

            for (size_t i = 0; i + 1 < points.size(); ++i)
            {
                const QPointF a = points[i];
                const QPointF b = points[i + 1];
                const PDFReal lineLength = QLineF(a, b).length();

                std::vector<std::pair<PDFReal, PDFReal>> removedIntervals;
                getRemovedIntervals(ctm.map(a), ctm.map(b), removedIntervals);

                if (removedIntervals.empty())
                {
                    Piece piece;
                    piece.segment.p0 = a;
                    piece.segment.p1 = b;
                    piece.offset = offset;
                    pieces.push_back(piece);
                    offset += lineLength;
                    continue;
                }

                removedAny = true;
                PDFReal t = 0.0;
                auto addPiece = [&](PDFReal t0, PDFReal t1, bool removed)
                {
                    if (t1 - t0 < 1.0e-12)
                    {
                        return;
                    }
                    Piece piece;
                    piece.segment.p0 = a + (b - a) * t0;
                    piece.segment.p1 = a + (b - a) * t1;
                    piece.removed = removed;
                    piece.offset = offset + lineLength * t0;
                    pieces.push_back(piece);
                };

                for (const auto& [t0, t1] : removedIntervals)
                {
                    addPiece(t, t0, false);
                    addPiece(t0, t1, true);
                    t = t1;
                }
                addPiece(t, 1.0, false);
                offset += lineLength;
            }

            if (!segment.isCurve)
            {
                continue;
            }

            // Flattened curve, which is not removed at all, is kept as a curve
            const size_t flattenedCount = points.size() - 1;
            bool anyRemoved = false;
            for (size_t i = pieces.size() >= flattenedCount ? pieces.size() - flattenedCount : 0; i < pieces.size(); ++i)
            {
                anyRemoved = anyRemoved || pieces[i].removed;
            }
            if (!anyRemoved && pieces.size() >= flattenedCount)
            {
                const PDFReal curveOffset = pieces[pieces.size() - flattenedCount].offset;
                pieces.resize(pieces.size() - flattenedCount);
                Piece piece;
                piece.segment = segment;
                piece.offset = curveOffset;
                pieces.push_back(piece);
            }
        }

        const bool hasRemovedPiece = std::any_of(pieces.cbegin(), pieces.cend(), [](const Piece& piece) { return piece.removed; });
        if (!hasRemovedPiece)
        {
            Run run;
            run.offset = 0.0;
            run.closed = subpath.closed;
            for (const Piece& piece : pieces)
            {
                run.segments.push_back(piece.segment);
            }
            if (run.closed && !run.segments.empty() && !run.segments.back().isCurve && QLineF(run.segments.back().p1, subpath.start).length() < 1.0e-9 &&
                subpath.segments.size() < run.segments.size())
            {
                // The closing segment is written by the closing operator
                run.segments.pop_back();
            }
            if (!run.segments.empty())
            {
                runs.push_back(run);
            }
            continue;
        }

        // Closed subpath is opened behind a removed piece, so its start is joined as before
        if (subpath.closed)
        {
            auto it = std::find_if(pieces.begin(), pieces.end(), [](const Piece& piece) { return piece.removed; });
            std::rotate(pieces.begin(), it, pieces.end());
        }

        Run current;
        bool hasCurrent = false;
        for (const Piece& piece : pieces)
        {
            if (piece.removed)
            {
                if (hasCurrent)
                {
                    runs.push_back(current);
                    current = Run();
                    hasCurrent = false;
                }
                continue;
            }

            if (!hasCurrent)
            {
                current.offset = piece.offset;
                hasCurrent = true;
            }
            current.segments.push_back(piece.segment);
        }
        if (hasCurrent)
        {
            runs.push_back(current);
        }
    }

    if (!removedAny)
    {
        return false;
    }

    auto writeRun = [&result](const Run& run)
    {
        if (run.segments.empty())
        {
            return;
        }
        result += formatNumber(run.segments.front().p0.x()) + ' ' + formatNumber(run.segments.front().p0.y()) + " m\n";
        for (const PathSegment& segment : run.segments)
        {
            if (segment.isCurve)
            {
                result += formatNumber(segment.c1.x()) + ' ' + formatNumber(segment.c1.y()) + ' ' +
                          formatNumber(segment.c2.x()) + ' ' + formatNumber(segment.c2.y()) + ' ' +
                          formatNumber(segment.p1.x()) + ' ' + formatNumber(segment.p1.y()) + " c\n";
            }
            else
            {
                result += formatNumber(segment.p1.x()) + ' ' + formatNumber(segment.p1.y()) + " l\n";
            }
        }
        if (run.closed)
        {
            result += "h\n";
        }
    };

    if (dashPattern.isSolid())
    {
        for (const Run& run : runs)
        {
            writeRun(run);
        }
        result += "S\n";
    }
    else
    {
        QByteArray dashArray = "[";
        for (PDFReal value : dashPattern.getDashArray())
        {
            dashArray += formatNumber(value) + ' ';
        }
        dashArray += ']';

        result += "q\n";
        for (const Run& run : runs)
        {
            result += dashArray + ' ' + formatNumber(dashPattern.getDashOffset() + run.offset) + " d\n";
            writeRun(run);
            result += "S\n";
        }
        result += "Q\n";
    }

    return true;
}

void RedactionFilter::handlePathPainting(Context& context)
{
    bool stroke = false;
    bool fill = false;
    bool close = false;
    Qt::FillRule fillRule = Qt::WindingFill;

    switch (context.op)
    {
        case Operator::PathStroke: stroke = true; break;
        case Operator::PathCloseStroke: stroke = true; close = true; break;
        case Operator::PathFillWinding:
        case Operator::PathFillWinding2: fill = true; break;
        case Operator::PathFillEvenOdd: fill = true; fillRule = Qt::OddEvenFill; break;
        case Operator::PathFillStrokeWinding: fill = true; stroke = true; break;
        case Operator::PathFillStrokeEvenOdd: fill = true; stroke = true; fillRule = Qt::OddEvenFill; break;
        case Operator::PathCloseFillStrokeWinding: fill = true; stroke = true; close = true; break;
        case Operator::PathCloseFillStrokeEvenOdd: fill = true; stroke = true; close = true; fillRule = Qt::OddEvenFill; break;
        default: break;
    }

    QPainterPath userPath = context.path;
    std::vector<Subpath> subpaths = context.subpaths;
    if (close)
    {
        userPath.closeSubpath();
        if (!subpaths.empty())
        {
            subpaths.back().closed = true;
        }
    }
    userPath.setFillRule(fillRule);

    const QByteArray pathOperators = context.pathOperators;
    const bool clip = context.clip;
    const bool clipEvenOdd = context.clipEvenOdd;
    context.path = QPainterPath();
    context.subpaths.clear();
    context.hasCurrentPoint = false;
    context.pathOperators.clear();
    context.clip = false;
    context.clipEvenOdd = false;

    auto emitVerbatim = [&]()
    {
        context.content += pathOperators;
        if (clip)
        {
            context.content += clipEvenOdd ? "W* " : "W ";
        }
        context.content += context.command;
        context.content += '\n';
    };

    if (!m_hasArea || pathOperators.isEmpty() || userPath.elementCount() == 0)
    {
        emitVerbatim();
        return;
    }

    const PDFPageContentProcessorState* state = getGraphicState();
    const QTransform& ctm = state->getCurrentTransformationMatrix();
    const QPainterPath pagePath = ctm.map(userPath);

    const bool hitFill = fill && hitsArea(m_area, m_areaBounds, pagePath);
    const bool hitStroke = stroke && hitsArea(m_area, m_areaBounds, ctm.map(getStrokeOutline(userPath)));
    bool hitClip = false;

    bool invertible = false;
    const QTransform inverted = ctm.inverted(&invertible);
    const QPainterPath areaUser = invertible ? inverted.map(m_area) : QPainterPath();

    QPainterPath clipPath = userPath;
    clipPath.setFillRule(clipEvenOdd ? Qt::OddEvenFill : Qt::WindingFill);
    if (clip && hitsArea(m_area, m_areaBounds, ctm.map(clipPath)))
    {
        // Clipping path, which passes through the area, would keep its shape in the area. If
        // every subpath either contains the whole area or is outside of it, nothing of the path
        // is in the area and it is not changed.
        if (!invertible)
        {
            hitClip = true;
        }
        else
        {
            for (const QPolygonF& polygon : clipPath.toSubpathPolygons())
            {
                QPainterPath subpath;
                subpath.addPolygon(polygon);
                subpath.closeSubpath();
                if (hitsArea(m_area, m_areaBounds, ctm.map(subpath)) && !PDFPathBoolean::subtract(areaUser, subpath).isEmpty())
                {
                    hitClip = true;
                    break;
                }
            }
        }
    }

    if (!fill && !stroke && !clip && hitsArea(m_area, m_areaBounds, pagePath))
    {
        // Path, which is not painted (n operator), is not written - it is not visible
        return;
    }

    if (!hitFill && !hitStroke && !hitClip)
    {
        emitVerbatim();
        return;
    }

    if (!invertible)
    {
        setFailed(PDFTranslationContext::tr("A path in a redacted area has a degenerate transformation."));
        return;
    }

    if ((hitFill && isUnsafePattern(state->getFillColorSpace())) || (hitStroke && isUnsafePattern(state->getStrokeColorSpace())))
    {
        setFailed(PDFTranslationContext::tr("A pattern is painted in a redacted area."));
        return;
    }

    QByteArray result;
    if (fill)
    {
        if (hitFill)
        {
            const QPainterPath remainder = PDFPathBoolean::subtract(userPath, areaUser);
            if (!remainder.isEmpty())
            {
                writePath(result, remainder);
                result += (remainder.fillRule() == Qt::OddEvenFill) ? "f*\n" : "f\n";
            }
        }
        else
        {
            result += pathOperators;
            result += (fillRule == Qt::OddEvenFill) ? "f*\n" : "f\n";
        }
    }

    if (stroke)
    {
        QByteArray cutStrokeOperators;
        if (hitStroke && cutStroke(subpaths, cutStrokeOperators))
        {
            result += cutStrokeOperators;
        }
        else
        {
            result += pathOperators;
            result += close ? "s\n" : "S\n";
        }
    }

    if (clip)
    {
        if (hitClip)
        {
            const QPainterPath newClipPath = PDFPathBoolean::unite(clipPath, areaUser);
            writePath(result, newClipPath);
            result += (newClipPath.fillRule() == Qt::OddEvenFill) ? "W* n\n" : "W n\n";
        }
        else
        {
            result += pathOperators;
            result += clipEvenOdd ? "W* n\n" : "W n\n";
        }
    }

    context.content += result;
}

// ---------------------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------------------

QRectF RedactionFilter::getGlyphInkRectangle(const PDFRealizedFontPointer& realizedFont, const QByteArray& code)
{
    if (!realizedFont)
    {
        return QRectF();
    }

    m_realizedFonts[realizedFont.get()] = realizedFont;
    const auto key = std::make_pair(static_cast<const void*>(realizedFont.get()), code);
    auto it = m_glyphInkCache.find(key);
    if (it != m_glyphInkCache.cend())
    {
        return it->second;
    }

    QRectF rectangle;
    try
    {
        TextSequence sequence;
        realizedFont->fillTextSequence(code, sequence, &m_dummyReporter);
        for (const TextSequenceItem& item : sequence.items)
        {
            if (item.glyph)
            {
                rectangle = item.glyph->boundingRect();
                break;
            }
        }
    }
    catch (const PDFException&)
    {
        rectangle = QRectF();
    }

    m_glyphInkCache[key] = rectangle;
    return rectangle;
}

bool RedactionFilter::isMetricCompatibleFont(const PDFFont* font, const PDFRealizedFontPointer& realizedFont)
{
    // A standard font without widths is drawn with the metrics of the font program. If the
    // system font used for it is a metric compatible clone of the standard font (as the URW
    // fonts are), all viewers place the glyphs at the same positions.
    const PDFSimpleFont* simpleFont = dynamic_cast<const PDFSimpleFont*>(font);
    if (!simpleFont || simpleFont->getStandardFontType() == StandardFontType::Invalid || !realizedFont)
    {
        return false;
    }

    static const char* clones[] = {
        "NimbusSans", "NimbusRoman", "NimbusMono", "LiberationSans", "LiberationSerif", "LiberationMono",
        "Arimo", "Tinos", "Cousine", "TeXGyreHeros", "TeXGyreTermes", "TeXGyreCursor", "Helvetica", "Times-",
        "Courier", "ArialMT", "Arial-", "TimesNewRoman", "StandardSymbolsPS", "D050000L", "Symbol", "ZapfDingbats"
    };

    const QString name = realizedFont->getPostScriptName();
    for (const char* clone : clones)
    {
        if (name.startsWith(QLatin1String(clone)))
        {
            return true;
        }
    }
    return false;
}

bool RedactionFilter::splitCodes(const PDFFont* font,
                                 const PDFObject& fontObject,
                                 const QByteArray& string,
                                 PDFReal fontSize,
                                 const PDFRealizedFontPointer& realizedFont,
                                 std::vector<GlyphCode>& codes,
                                 QString* error)
{
    const PDFDocument* document = getDocument();

    switch (font->getFontType())
    {
        case FontType::Type0:
        {
            const PDFType0Font* type0Font = dynamic_cast<const PDFType0Font*>(font);
            const PDFFontCMap* cmap = font->getCMap();
            if (!type0Font || !cmap || !cmap->isValid())
            {
                *error = PDFTranslationContext::tr("The encoding of a composite font can't be read.");
                return false;
            }
            if (cmap->isVertical())
            {
                *error = PDFTranslationContext::tr("The page has vertical text.");
                return false;
            }

            const std::vector<PDFFontCMap::MappedCode> mappedCodes = cmap->interpretWithCode(string);
            int offset = 0;
            for (const PDFFontCMap::MappedCode& mappedCode : mappedCodes)
            {
                // The encoding is split by the mapping, not by the code space ranges of the CMap.
                // Codes of different lengths (or unmapped codes) could be split differently by
                // other viewers, then positions of the glyphs would not be known.
                if (mappedCode.byteCount != mappedCodes.front().byteCount || (mappedCode.cid == 0 && cmap->getMaxKeyLength() > 1))
                {
                    *error = PDFTranslationContext::tr("The encoding of a composite font can't be split to characters safely.");
                    return false;
                }

                GlyphCode code;
                code.bytes = string.mid(offset, int(mappedCode.byteCount));
                code.width = type0Font->getGlyphAdvance(mappedCode.cid) / 1000.0;
                code.isSpace = mappedCode.byteCount == 1 && static_cast<unsigned char>(string[offset]) == 32;
                offset += int(mappedCode.byteCount);
                codes.push_back(qMove(code));
            }

            if (offset != string.size())
            {
                *error = PDFTranslationContext::tr("A text string can't be split to characters.");
                return false;
            }
            return true;
        }

        case FontType::Type3:
        {
            const PDFType3Font* type3Font = dynamic_cast<const PDFType3Font*>(font);
            if (!type3Font)
            {
                *error = PDFTranslationContext::tr("Type 3 font can't be read.");
                return false;
            }

            for (int i = 0; i < string.size(); ++i)
            {
                const int index = static_cast<unsigned char>(string[i]);
                const QPointF displacement = type3Font->getFontMatrix().map(QPointF(type3Font->getWidth(index), 0.0)) - type3Font->getFontMatrix().map(QPointF(0.0, 0.0));
                GlyphCode code;
                code.bytes = string.mid(i, 1);
                code.width = displacement.x();
                code.isSpace = index == 32;
                codes.push_back(qMove(code));
            }
            return true;
        }

        case FontType::Type1:
        case FontType::TrueType:
        case FontType::MMType1:
        {
            // Widths are read from the font dictionary, as viewers do (see PDF specification,
            // 9.2.4). If there are no widths (standard 14 fonts), the font program is used.
            const PDFDictionary* fontDictionary = document->getDictionaryFromObject(fontObject);
            std::vector<PDFReal> widths;
            PDFInteger firstChar = 0;
            PDFReal missingWidth = 0.0;
            bool hasWidths = false;

            if (fontDictionary)
            {
                PDFDocumentDataLoaderDecorator loader(document);
                const PDFObject& widthsObject = document->getObject(fontDictionary->get("Widths"));
                if (widthsObject.isArray())
                {
                    hasWidths = true;
                    widths = loader.readNumberArray(widthsObject);
                    firstChar = loader.readIntegerFromDictionary(fontDictionary, "FirstChar", 0);
                    if (const PDFDictionary* descriptor = document->getDictionaryFromObject(fontDictionary->get("FontDescriptor")))
                    {
                        missingWidth = loader.readNumberFromDictionary(descriptor, "MissingWidth", 0.0);
                    }
                }
            }

            for (int i = 0; i < string.size(); ++i)
            {
                const int index = static_cast<unsigned char>(string[i]);
                GlyphCode code;
                code.bytes = string.mid(i, 1);
                code.isSpace = index == 32;

                if (hasWidths)
                {
                    const PDFInteger widthIndex = index - firstChar;
                    code.width = (widthIndex >= 0 && widthIndex < PDFInteger(widths.size()) ? widths[widthIndex] : missingWidth) / 1000.0;
                }
                else
                {
                    // Width of the glyph from the font program
                    if (!realizedFont || std::abs(fontSize) < 1.0e-9)
                    {
                        *error = PDFTranslationContext::tr("The width of a character can't be determined.");
                        return false;
                    }

                    PDFReal advance = 0.0;
                    bool found = false;
                    try
                    {
                        TextSequence sequence;
                        realizedFont->fillTextSequence(code.bytes, sequence, &m_dummyReporter);
                        for (const TextSequenceItem& item : sequence.items)
                        {
                            if (item.isCharacter() || item.isAdvance())
                            {
                                advance = item.advance;
                                found = true;
                                break;
                            }
                        }
                    }
                    catch (const PDFException&)
                    {
                        found = false;
                    }

                    if (!found)
                    {
                        *error = PDFTranslationContext::tr("The width of a character can't be determined.");
                        return false;
                    }
                    code.width = advance / std::abs(fontSize);
                    code.approximateWidth = !isMetricCompatibleFont(font, realizedFont);
                }

                codes.push_back(qMove(code));
            }
            return true;
        }

        default:
            break;
    }

    *error = PDFTranslationContext::tr("The page uses an unsupported font.");
    return false;
}

void RedactionFilter::handleText(Context& context)
{
    if (!m_hasArea)
    {
        emitOperator(context);
        return;
    }

    const PDFPageContentProcessorState* state = getGraphicState();
    const PDFFontPointer& font = state->getTextFont();
    if (!font)
    {
        setFailed(PDFTranslationContext::tr("The page has text with a font, which can't be read."));
        return;
    }

    if (!context.inText)
    {
        setFailed(PDFTranslationContext::tr("The page has text outside of a text object."));
        return;
    }

    PDFObject fontObject;
    auto fontObjectIt = m_fontObjects.find(font.get());
    if (fontObjectIt != m_fontObjects.cend())
    {
        fontObject = fontObjectIt->second.second;
    }

    const PDFReal fontSize = state->getTextFontSize();
    const PDFReal horizontalScaling = state->getTextHorizontalScaling() * 0.01;
    PDFReal characterSpacing = state->getTextCharacterSpacing();
    PDFReal wordSpacing = state->getTextWordSpacing();
    const PDFReal rise = state->getTextRise();
    const TextRenderingMode renderingMode = state->getTextRenderingMode();
    const QTransform& ctm = state->getCurrentTransformationMatrix();
    PDFDocumentDataLoaderDecorator loader(getDocument());

    // Operators are written with an explicit text position (Tm), which is computed as the PDF
    // specification says (glyph widths from the font dictionary). So every viewer draws the
    // remaining glyphs exactly at the positions, which were tested against the areas - the
    // position does not depend on how the viewer computed the previous text.
    QByteArray prefix;
    std::vector<PDFObject> elements;

    switch (context.op)
    {
        case Operator::TextShowTextString:
            elements.push_back(context.operands[0]);
            break;

        case Operator::TextShowTextIndividualSpacing:
            for (const PDFObject& item : *context.operands[0].getArray())
            {
                elements.push_back(item);
            }
            break;

        case Operator::TextNextLineShowText:
            elements.push_back(context.operands[0]);
            context.textLineMatrix = QTransform(1, 0, 0, 1, 0, -state->getTextLeading()) * context.textLineMatrix;
            context.textMatrix = context.textLineMatrix;
            break;

        case Operator::TextSetSpacingAndShowText:
        {
            wordSpacing = loader.readNumber(context.operands[0], 0.0);
            characterSpacing = loader.readNumber(context.operands[1], 0.0);
            elements.push_back(context.operands[2]);
            context.textLineMatrix = QTransform(1, 0, 0, 1, 0, -state->getTextLeading()) * context.textLineMatrix;
            context.textMatrix = context.textLineMatrix;
            prefix = formatNumber(wordSpacing) + " Tw " + formatNumber(characterSpacing) + " Tc\n";
            break;
        }

        default:
            return;
    }

    const QTransform startMatrix = context.textMatrix;
    QTransform textMatrix = context.textMatrix;

    const bool isType3 = font->getFontType() == FontType::Type3;
    const bool isStroked = isTextRenderingModeStroked(renderingMode);

    PDFRealizedFontPointer realizedFont;
    if (!isType3)
    {
        try
        {
            realizedFont = getFontCache()->getRealizedFont(font, fontSize, &m_dummyReporter);
        }
        catch (const PDFException&)
        {
            realizedFont = nullptr;
        }
    }

    // Bounding box of the glyphs of Type 3 font (in text space units for font size 1)
    QRectF type3BoundingBox;
    if (isType3)
    {
        const PDFType3Font* type3Font = dynamic_cast<const PDFType3Font*>(font.get());
        const PDFDictionary* fontDictionary = getDocument()->getDictionaryFromObject(fontObject);
        if (type3Font && fontDictionary)
        {
            QRectF box = loader.readRectangle(fontDictionary->get("FontBBox"), QRectF()).normalized();
            if (box.width() > 0.0 && box.height() > 0.0)
            {
                type3BoundingBox = type3Font->getFontMatrix().mapRect(box);
            }
        }
    }

    const PDFReal fontFactor = (fontSize < 0.0) ? -1.0 : 1.0;
    const PDFReal strokeInflation = isStroked ? 0.5 * state->getLineWidth() * std::sqrt(std::abs(ctm.determinant())) + 0.01 : 0.0;

    struct Item
    {
        bool isNumber = false;
        PDFReal number = 0.0;
        GlyphCode code;
        bool removed = false;
    };
    std::vector<Item> items;
    bool removedAny = false;
    bool approximate = false;
    QRectF textExtent;

    for (const PDFObject& element : elements)
    {
        if (element.isInt() || element.isReal())
        {
            Item item;
            item.isNumber = true;
            item.number = loader.readNumber(element, 0.0);
            items.push_back(item);
            textMatrix = QTransform(1, 0, 0, 1, -item.number * 0.001 * fontSize * horizontalScaling, 0) * textMatrix;
            continue;
        }

        std::vector<GlyphCode> codes;
        QString error;
        if (!splitCodes(font.get(), fontObject, element.getString(), fontSize, realizedFont, codes, &error))
        {
            setFailed(error);
            return;
        }

        for (const GlyphCode& code : codes)
        {
            const PDFReal glyphWidth = code.width;
            const PDFReal displacement = (glyphWidth * fontSize + characterSpacing + (code.isSpace ? wordSpacing : 0.0)) * horizontalScaling;
            approximate = approximate || code.approximateWidth;

            // Text space (in units of the font size) to page space
            const QTransform glyphToPage = QTransform(fontSize * horizontalScaling, 0, 0, fontSize, 0, rise) * textMatrix * ctm;
            const PDFReal cellWidth = qMax(std::abs(glyphWidth), 0.3);

            bool removed = false;
            if (isType3)
            {
                // Glyphs of Type 3 font are content streams, their extent is known only from the
                // bounding box of the font - if it touches the area, the page is converted to outlines.
                if (!type3BoundingBox.isValid())
                {
                    setFailed(PDFTranslationContext::tr("Type 3 font without a bounding box is used on a redacted page."));
                    return;
                }
                bool hit = hitsArea(m_area, m_areaBounds, mapRectangle(glyphToPage, QRectF(-0.3, -0.5, cellWidth + 0.6, 2.0)));
                hit = hit || hitsArea(m_area, m_areaBounds, mapRectangle(glyphToPage, type3BoundingBox));
                if (hit)
                {
                    setFailed(PDFTranslationContext::tr("Text in a Type 3 font is in a redacted area."));
                    return;
                }
            }
            else
            {
                const QRectF inkRectangle = getGlyphInkRectangle(realizedFont, code.bytes);
                const bool hasInk = inkRectangle.isValid() && !inkRectangle.isNull();

                // The cell of the glyph (core of the advance box) - it catches glyphs without
                // an outline (invisible text of OCR, spaces, glyphs missing in the font). If the
                // outline is not known, the cell spans from the descent to the ascent.
                const QRectF cellRectangle = hasInk ? QRectF(0.1 * cellWidth, 0.0, 0.8 * cellWidth, 0.6) : QRectF(0.0, -0.3, cellWidth, 1.3);
                removed = hitsArea(m_area, m_areaBounds, mapRectangle(glyphToPage, cellRectangle));
                textExtent = textExtent.united(mapRectangle(glyphToPage, QRectF(0.0, -0.3, cellWidth, 1.3)).boundingRect());

                if (!removed && hasInk)
                {
                    // The outline of the glyph (realized font units are text space units)
                    const QTransform inkToPage = QTransform(horizontalScaling * fontFactor, 0, 0, fontFactor, 0, rise) * textMatrix * ctm;
                    QPolygonF ink = mapRectangle(inkToPage, inkRectangle);
                    if (strokeInflation > 0.0)
                    {
                        const QRectF bounds = ink.boundingRect().adjusted(-strokeInflation, -strokeInflation, strokeInflation, strokeInflation);
                        ink = mapRectangle(QTransform(), bounds);
                    }
                    textExtent = textExtent.united(ink.boundingRect());
                    removed = hitsArea(m_area, m_areaBounds, ink);
                }
            }

            Item item;
            item.code = code;
            item.removed = removed;
            items.push_back(item);
            removedAny = removedAny || removed;
            textMatrix = QTransform(1, 0, 0, 1, displacement, 0) * textMatrix;
        }
    }

    // Font without widths: viewers use their own metrics of the font, the glyphs in the middle
    // of the text could be placed a little differently. If the text (with a reserve) touches
    // the area, it is removed entirely.
    if (approximate)
    {
        const QRectF extent = textExtent;
        const PDFReal reserve = 0.1 * qMax(extent.width(), extent.height()) + 1.0;
        QPolygonF reserveExtent = mapRectangle(QTransform(), extent.adjusted(-reserve, -reserve, reserve, reserve));
        if (removedAny || hitsArea(m_area, m_areaBounds, reserveExtent))
        {
            for (Item& item : items)
            {
                item.removed = !item.isNumber;
            }
            removedAny = true;
        }
    }

    context.textMatrix = textMatrix;

    QByteArray result = prefix;
    result += writeMatrix(startMatrix) + " Tm\n";

    if (!removedAny)
    {
        // The text itself is not changed (' and " are written as Tj, the position is explicit)
        writeObject(result, context.op == Operator::TextSetSpacingAndShowText ? context.operands[2] : context.operands[0]);
        result += (context.op == Operator::TextShowTextIndividualSpacing) ? " TJ\n" : " Tj\n";
        context.content += result;
        return;
    }

    if (std::abs(fontSize) < 1.0e-9)
    {
        setFailed(PDFTranslationContext::tr("Text with zero font size is in a redacted area."));
        return;
    }

    if ((isTextRenderingModeFilled(renderingMode) && isUnsafePattern(state->getFillColorSpace())) ||
        (isStroked && isUnsafePattern(state->getStrokeColorSpace())))
    {
        setFailed(PDFTranslationContext::tr("Text in a redacted area is painted by a pattern."));
        return;
    }

    // Removed glyphs are replaced by a number, which moves the position by their widths
    QByteArray array = "[";
    QByteArray currentString;
    bool hasString = false;
    PDFReal pendingNumber = 0.0;

    auto flushString = [&]()
    {
        if (hasString)
        {
            writeString(array, currentString);
            array += ' ';
            currentString.clear();
            hasString = false;
        }
    };

    auto flushNumber = [&]()
    {
        if (std::abs(pendingNumber) > 1.0e-9)
        {
            array += formatNumber(pendingNumber) + ' ';
        }
        pendingNumber = 0.0;
    };

    for (const Item& item : items)
    {
        if (item.isNumber)
        {
            flushString();
            pendingNumber += item.number;
        }
        else if (item.removed)
        {
            flushString();
            pendingNumber += -(item.code.width * fontSize + characterSpacing + (item.code.isSpace ? wordSpacing : 0.0)) * 1000.0 / fontSize;
        }
        else
        {
            flushNumber();
            currentString += item.code.bytes;
            hasString = true;
        }
    }
    flushString();

    // Nothing behind the last kept glyph needs a position (the next text operator has its own)
    array += "] TJ\n";
    if (array != "[] TJ\n")
    {
        result += array;
    }
    context.content += result;
}

// ---------------------------------------------------------------------------------------
// XObjects and images
// ---------------------------------------------------------------------------------------

void RedactionFilter::handleXObject(Context& context)
{
    context.xobjectAction = Context::XObjectAction::Drop;
    context.xobjectName.clear();

    if (context.operands.size() != 1 || !context.operands[0].isName())
    {
        return;
    }

    const QByteArray name = context.operands[0].getString();
    const PDFDictionary* xobjects = getXObjectDictionary();
    if (!xobjects || !xobjects->hasKey(name))
    {
        return;
    }

    const PDFObject& object = getDocument()->getObject(xobjects->get(name));
    if (!object.isStream())
    {
        return;
    }

    const PDFStream* stream = object.getStream();
    PDFDocumentDataLoaderDecorator loader(getDocument());
    const QByteArray subtype = loader.readNameFromDictionary(stream->getDictionary(), "Subtype");
    context.xobjectName = name;

    if (subtype == "Image")
    {
        const QTransform& ctm = getGraphicState()->getCurrentTransformationMatrix();
        if (!m_hasArea || !hitsArea(m_area, m_areaBounds, mapRectangle(ctm, QRectF(0, 0, 1, 1))))
        {
            registerResource(context, RES_XOBJECT, name);
            context.xobjectAction = Context::XObjectAction::Emit;
        }
        else
        {
            context.xobjectAction = Context::XObjectAction::AwaitImage;
        }
    }
    else if (subtype == "Form")
    {
        if (!m_hasArea)
        {
            registerResource(context, RES_XOBJECT, name);
            context.xobjectAction = Context::XObjectAction::Emit;
        }
        else if (m_contexts.size() >= 32)
        {
            // Forms nested too deeply (or a form, which contains itself) - the form is not processed
            setFailed(PDFTranslationContext::tr("Form XObjects are nested too deeply."));
            context.xobjectAction = Context::XObjectAction::Drop;
        }
        else
        {
            // Form is filtered - a new context is created, it is written as a new form
            context.xobjectAction = Context::XObjectAction::Form;
            auto formContext = std::make_unique<Context>();
            formContext->isForm = true;
            formContext->formStream = stream;
            m_contexts.emplace_back(qMove(formContext));
        }
    }
    else
    {
        // PostScript XObjects and unknown XObjects are not written
        context.xobjectAction = Context::XObjectAction::Drop;
    }
}

void RedactionFilter::finishForm()
{
    std::unique_ptr<Context> formContext = qMove(m_contexts.back());
    m_contexts.pop_back();
    Context& parentContext = top();

    if (formContext->saveDepth != 0 && m_hasArea)
    {
        // The processor restores only the state saved for the form
        setFailed(PDFTranslationContext::tr("Graphic state is not restored in a form."));
    }

    if (m_failed)
    {
        return;
    }

    const PDFDictionary* sourceDictionary = formContext->formStream->getDictionary();
    PDFDictionaryBuilder dictionary;
    dictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    dictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
    for (const char* key : { "BBox", "Matrix", "Group", "OC" })
    {
        if (sourceDictionary->hasKey(key))
        {
            dictionary.setEntry(PDFInplaceOrMemoryString(key), m_owner->copier.copy(sourceDictionary->get(key)));
        }
    }
    dictionary.setEntry(PDFInplaceOrMemoryString("Resources"), createResources(*formContext));

    QByteArray content = formContext->content + getClosingOperators(*formContext);
    const PDFObjectReference reference = m_owner->builder->addObject(createFlateStream(std::move(dictionary), content));

    const QByteArray name = createUniqueName(parentContext, RES_XOBJECT, "PFRfm");
    parentContext.resources[RES_XOBJECT][name] = PDFObject::createReference(reference);
    std::vector<PDFObject> operands = { PDFObject::createName(name) };
    emitOperator(parentContext, operands, "Do");
}

bool RedactionFilter::performOriginalImagePainting(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference)
{
    Q_UNUSED(reference);

    if (m_failed || m_contexts.empty())
    {
        return true;
    }

    Context& context = top();
    const bool isInlineImage = context.depth == 0;
    const bool isAwaitedImage = context.depth == 1 && context.op == Operator::PaintXObject && context.xobjectAction == Context::XObjectAction::AwaitImage;

    if (!isInlineImage && !isAwaitedImage)
    {
        // Nested image (in a pattern, Type 3 glyph...) - ignored
        return true;
    }

    const QTransform& ctm = getGraphicState()->getCurrentTransformationMatrix();
    const bool hit = m_hasArea && hitsArea(m_area, m_areaBounds, mapRectangle(ctm, QRectF(0, 0, 1, 1)));

    if (isInlineImage && m_hasArea)
    {
        // The processor ends the data of an inline image by its length, other viewers by the
        // first "EI" operator. If the data contain "EI", other viewers would execute the rest
        // of the data as operators - they can't be filtered.
        const QByteArray& data = *stream->getContent();
        auto isWhitespace = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0'; };
        for (qsizetype index = data.indexOf("EI"); index >= 0; index = data.indexOf("EI", index + 1))
        {
            const bool before = index == 0 || isWhitespace(data[index - 1]);
            const bool after = index + 2 >= data.size() || isWhitespace(data[index + 2]);
            if (before && after)
            {
                setFailed(PDFTranslationContext::tr("Data of an inline image can be read differently by other viewers."));
                return true;
            }
        }
    }

    PDFObject imageObject;
    if (isInlineImage && !hit && !m_hasArea)
    {
        imageObject = createInlineImageXObject(stream);
        if (imageObject.isNull())
        {
            // The image can't be converted - it is redacted (decoded and written again)
            ImageResult result = redactImage(image, stream, true);
            if (result.status == ImageResult::Status::Fail)
            {
                setFailed(result.error);
                return true;
            }
            imageObject = result.image;
        }
    }
    else
    {
        ImageResult result = redactImage(image, stream, isInlineImage);
        switch (result.status)
        {
            case ImageResult::Status::Ok:
                imageObject = result.image;
                break;

            case ImageResult::Status::Drop:
                if (isAwaitedImage)
                {
                    context.xobjectAction = Context::XObjectAction::Drop;
                }
                return true;

            case ImageResult::Status::Fail:
                setFailed(result.error);
                return true;
        }
    }

    if (imageObject.isNull())
    {
        return true;
    }

    const QByteArray name = createUniqueName(context, RES_XOBJECT, "PFRim");
    context.resources[RES_XOBJECT][name] = PDFObject::createReference(m_owner->builder->addObject(imageObject));

    if (isInlineImage)
    {
        std::vector<PDFObject> operands = { PDFObject::createName(name) };
        emitOperator(context, operands, "Do");
    }
    else
    {
        context.xobjectName = name;
        context.xobjectAction = Context::XObjectAction::Emit;
    }

    return true;
}

PDFObject RedactionFilter::resolveImageColorSpace(const PDFObject& colorSpaceObject, bool isInlineImage)
{
    const PDFObject& colorSpace = getDocument()->getObject(colorSpaceObject);

    auto expandName = [isInlineImage](const QByteArray& name) -> QByteArray
    {
        if (!isInlineImage)
        {
            return name;
        }
        if (name == "G") return "DeviceGray";
        if (name == "RGB") return "DeviceRGB";
        if (name == "CMYK") return "DeviceCMYK";
        if (name == "I") return "Indexed";
        return name;
    };

    if (colorSpace.isName())
    {
        const QByteArray name = expandName(colorSpace.getString());
        if (name == "DeviceGray" || name == "DeviceRGB" || name == "DeviceCMYK")
        {
            return PDFObject::createName(name);
        }

        const PDFDictionary* colorSpaces = getColorSpaceDictionary();
        if (colorSpaces && colorSpaces->hasKey(name))
        {
            return m_owner->copier.copy(colorSpaces->get(name));
        }
        return PDFObject();
    }

    if (colorSpace.isArray())
    {
        const PDFArray* array = colorSpace.getArray();
        PDFArrayBuilder result;
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            const PDFObject& item = getDocument()->getObject(array->getItem(i));
            if (i == 0 && item.isName())
            {
                result.appendItem(PDFObject::createName(expandName(item.getString())));
            }
            else if (i == 1 && item.isName() && isInlineImage)
            {
                // Base color space of the indexed color space
                result.appendItem(resolveImageColorSpace(item, true));
            }
            else
            {
                result.appendItem(m_owner->copier.copy(array->getItem(i)));
            }
        }
        return PDFObject::createArray(std::move(result));
    }

    return PDFObject();
}

PDFObject RedactionFilter::createInlineImageXObject(const PDFStream* stream)
{
    const PDFDictionary* inlineDictionary = stream->getDictionary();
    PDFDictionaryBuilder dictionary;
    dictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    dictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));

    auto expandFilter = [](const QByteArray& name) -> QByteArray
    {
        if (name == "AHx") return "ASCIIHexDecode";
        if (name == "A85") return "ASCII85Decode";
        if (name == "LZW") return "LZWDecode";
        if (name == "Fl") return "FlateDecode";
        if (name == "RL") return "RunLengthDecode";
        if (name == "CCF") return "CCITTFaxDecode";
        if (name == "DCT") return "DCTDecode";
        return name;
    };

    for (size_t i = 0; i < inlineDictionary->getCount(); ++i)
    {
        const QByteArray key = inlineDictionary->getKey(i).getString();
        const PDFObject& value = inlineDictionary->getValue(i);

        if (key == "Length" || key == "Type" || key == "Subtype")
        {
            continue;
        }

        if (key == "ColorSpace")
        {
            PDFObject colorSpace = resolveImageColorSpace(value, true);
            if (colorSpace.isNull())
            {
                return PDFObject();
            }
            dictionary.setEntry(PDFInplaceOrMemoryString(key), colorSpace);
        }
        else if (key == "Filter")
        {
            if (value.isName())
            {
                dictionary.setEntry(PDFInplaceOrMemoryString(key), PDFObject::createName(expandFilter(value.getString())));
            }
            else if (value.isArray())
            {
                PDFArrayBuilder filters;
                for (const PDFObject& filter : *value.getArray())
                {
                    if (!filter.isName())
                    {
                        return PDFObject();
                    }
                    filters.appendItem(PDFObject::createName(expandFilter(filter.getString())));
                }
                dictionary.setEntry(PDFInplaceOrMemoryString(key), PDFObject::createArray(std::move(filters)));
            }
            else
            {
                return PDFObject();
            }
        }
        else if (key == "Width" || key == "Height" || key == "BitsPerComponent" || key == "Decode" ||
                 key == "DecodeParms" || key == "ImageMask" || key == "Interpolate" || key == "Intent")
        {
            dictionary.setEntry(PDFInplaceOrMemoryString(key), value);
        }
        else
        {
            // Unknown key of inline image - it is not written
        }
    }

    QByteArray content = *stream->getContent();
    return PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content)));
}

std::vector<uint8_t> RedactionFilter::getCoverage(int width, int height, const QTransform& pixelToPage, bool* ok) const
{
    *ok = false;
    std::vector<uint8_t> coverage;

    if (width <= 0 || height <= 0 || qint64(width) * qint64(height) > qint64(400) * 1000 * 1000)
    {
        return coverage;
    }

    bool invertible = false;
    const QTransform pageToPixel = pixelToPage.inverted(&invertible);
    if (!invertible)
    {
        return coverage;
    }

    QImage mask(width, height, QImage::Format_Grayscale8);
    if (mask.isNull())
    {
        return coverage;
    }
    mask.fill(0);

    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setTransform(pageToPixel);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawPath(m_area);

        painter.end();
    }

    // Every pixel, which is covered by the area even partially, is overwritten (antialiased
    // coverage is greater than zero). Pixels, which only touch the area, are kept.
    coverage.assign(size_t(width) * size_t(height), 0);
    for (int y = 0; y < height; ++y)
    {
        const uchar* row = mask.constScanLine(y);
        for (int x = 0; x < width; ++x)
        {
            if (row[x])
            {
                coverage[size_t(y) * size_t(width) + size_t(x)] = 1;
            }
        }
    }

    *ok = true;
    return coverage;
}

namespace
{

/// Sets bits of the covered pixels of the sample data to the given value
void overwritePixels(QByteArray& data, unsigned int stride, unsigned int width, unsigned int height,
                     unsigned int bitsPerPixel, const std::vector<uint8_t>& coverage, bool value)
{
    for (unsigned int y = 0; y < height; ++y)
    {
        unsigned char* row = reinterpret_cast<unsigned char*>(data.data()) + size_t(y) * stride;
        for (unsigned int x = 0; x < width; ++x)
        {
            if (!coverage[size_t(y) * width + x])
            {
                continue;
            }

            const size_t bitStart = size_t(x) * bitsPerPixel;
            if (bitStart % 8 == 0 && bitsPerPixel % 8 == 0)
            {
                std::memset(row + bitStart / 8, value ? 0xFF : 0x00, bitsPerPixel / 8);
            }
            else
            {
                for (size_t bit = bitStart; bit < bitStart + bitsPerPixel; ++bit)
                {
                    const unsigned char mask = static_cast<unsigned char>(0x80 >> (bit & 7));
                    if (value)
                    {
                        row[bit >> 3] |= mask;
                    }
                    else
                    {
                        row[bit >> 3] &= static_cast<unsigned char>(~mask);
                    }
                }
            }
        }
    }
}

QByteArray getLastFilterName(const PDFDocument* document, const PDFDictionary* dictionary)
{
    const PDFObject& filters = document->getObject(dictionary->get("Filter"));
    if (filters.isName())
    {
        return filters.getString();
    }
    if (filters.isArray() && filters.getArray()->getCount() > 0)
    {
        const PDFObject& last = document->getObject(filters.getArray()->getItem(filters.getArray()->getCount() - 1));
        if (last.isName())
        {
            return last.getString();
        }
    }
    return QByteArray();
}

}   // namespace

PDFObject RedactionFilter::redactMaskStream(const PDFStream* stream, bool isSoftMask, const QTransform& ctm, QString* error)
{
    PDFImage maskImage;
    try
    {
        maskImage = PDFImage::createImage(getDocument(), stream, PDFColorSpacePointer(new PDFDeviceGrayColorSpace()), isSoftMask, RenderingIntent::Perceptual, &m_dummyReporter);
    }
    catch (const PDFException& exception)
    {
        *error = exception.getMessage();
        return PDFObject();
    }

    const PDFImageData& data = maskImage.getImageData();
    if (!data.isValid() || data.getComponents() != 1 || size_t(data.getStride()) * data.getHeight() > size_t(data.getData().size()))
    {
        *error = PDFTranslationContext::tr("The mask of an image can't be read.");
        return PDFObject();
    }

    const unsigned int width = data.getWidth();
    const unsigned int height = data.getHeight();
    const QTransform pixelToPage = QTransform(1.0 / width, 0, 0, -1.0 / height, 0, 1) * ctm;

    bool ok = false;
    const std::vector<uint8_t> coverage = getCoverage(int(width), int(height), pixelToPage, &ok);
    if (!ok)
    {
        *error = PDFTranslationContext::tr("The mask of an image can't be redacted.");
        return PDFObject();
    }

    QByteArray samples = data.getData();
    PDFDocumentDataLoaderDecorator loader(getDocument());
    const std::vector<PDFReal> decode = loader.readNumberArrayFromDictionary(stream->getDictionary(), "Decode");

    // Covered pixels of the soft mask are transparent, covered pixels of the stencil mask are masked out
    const bool decodeInverted = decode.size() >= 2 && decode[0] > decode[1];
    const bool value = isSoftMask ? decodeInverted : !decodeInverted;
    overwritePixels(samples, data.getStride(), width, height, data.getBitsPerComponent(), coverage, value);

    PDFDictionaryBuilder dictionary;
    dictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    dictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
    dictionary.setEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(width));
    dictionary.setEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(height));
    dictionary.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(data.getBitsPerComponent()));
    if (isSoftMask)
    {
        dictionary.setEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceGray"));
        if (stream->getDictionary()->hasKey("Matte"))
        {
            dictionary.setEntry(PDFInplaceOrMemoryString("Matte"), m_owner->copier.copy(stream->getDictionary()->get("Matte")));
        }
    }
    else
    {
        dictionary.setEntry(PDFInplaceOrMemoryString("ImageMask"), PDFObject::createBool(true));
    }
    if (decode.size() >= 2)
    {
        dictionary.setEntry(PDFInplaceOrMemoryString("Decode"), createNumberArray({ decode[0], decode[1] }));
    }

    // Stride of the decoded data can be greater than needed, data are written packed
    const unsigned int packedStride = (width * data.getBitsPerComponent() + 7) / 8;
    QByteArray packed;
    packed.reserve(int(packedStride * height));
    for (unsigned int y = 0; y < height; ++y)
    {
        packed.append(samples.constData() + size_t(y) * data.getStride(), int(packedStride));
    }

    return PDFObject::createReference(m_owner->builder->addObject(createFlateStream(std::move(dictionary), packed)));
}

ImageResult RedactionFilter::redactImage(const PDFImage& image, const PDFStream* stream, bool isInlineImage)
{
    ImageResult result;
    const QTransform& ctm = getGraphicState()->getCurrentTransformationMatrix();
    const PDFImageData& imageData = image.getImageData();
    const PDFDictionary* sourceDictionary = stream->getDictionary();
    PDFDocumentDataLoaderDecorator loader(getDocument());

    if (!imageData.isValid())
    {
        result.status = ImageResult::Status::Fail;
        result.error = PDFTranslationContext::tr("An image in a redacted area can't be read.");
        return result;
    }

    const unsigned int width = imageData.getWidth();
    const unsigned int height = imageData.getHeight();
    const QTransform pixelToPage = QTransform(1.0 / width, 0, 0, -1.0 / height, 0, 1) * ctm;

    if (!pixelToPage.isInvertible())
    {
        // Image is collapsed to a line or a point, it is not visible, it is dropped
        result.status = ImageResult::Status::Drop;
        return result;
    }

    bool coverageOk = false;
    const std::vector<uint8_t> coverage = getCoverage(int(width), int(height), pixelToPage, &coverageOk);
    if (!coverageOk)
    {
        result.status = ImageResult::Status::Fail;
        result.error = PDFTranslationContext::tr("An image in a redacted area is too big.");
        return result;
    }

    const bool isImageMask = imageData.getMaskingType() == PDFImageData::MaskingType::ImageMask;
    const QByteArray filterName = getLastFilterName(getDocument(), sourceDictionary);
    const bool isJPEG = filterName == "DCTDecode" || filterName == "DCT";
    const bool isJPX = filterName == "JPXDecode";

    const unsigned int components = imageData.getComponents();
    const unsigned int bitsPerComponent = imageData.getBitsPerComponent();
    const unsigned int stride = imageData.getStride();

    PDFObject colorSpace;
    if (!isImageMask && sourceDictionary->hasKey("ColorSpace"))
    {
        colorSpace = resolveImageColorSpace(sourceDictionary->get("ColorSpace"), isInlineImage);
    }

    const bool rawDataUsable = !isJPX &&
                               (isImageMask || (!colorSpace.isNull() && image.getColorSpace() && image.getColorSpace()->getColorComponentCount() == components)) &&
                               (bitsPerComponent == 1 || bitsPerComponent == 2 || bitsPerComponent == 4 || bitsPerComponent == 8 || bitsPerComponent == 16) &&
                               size_t(stride) * height <= size_t(imageData.getData().size()) &&
                               size_t(stride) * 8 >= size_t(width) * components * bitsPerComponent;

    PDFDictionaryBuilder dictionary;
    dictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    dictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
    dictionary.setEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(width));
    dictionary.setEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(height));
    for (const char* key : { "Interpolate", "Intent", "OC" })
    {
        if (sourceDictionary->hasKey(key))
        {
            dictionary.setEntry(PDFInplaceOrMemoryString(key), m_owner->copier.copy(sourceDictionary->get(key)));
        }
    }

    QByteArray encoded;
    QByteArray encodedFilter;

    if (rawDataUsable)
    {
        QByteArray samples = imageData.getData();
        const std::vector<PDFReal> decode = loader.readNumberArrayFromDictionary(sourceDictionary, "Decode");

        bool value = false;
        if (isImageMask)
        {
            // Covered pixels are not painted (sample 1 for the default decode array)
            value = !(decode.size() >= 2 && decode[0] > decode[1]);
        }
        overwritePixels(samples, stride, width, height, components * bitsPerComponent, coverage, value);

        dictionary.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(bitsPerComponent));
        if (isImageMask)
        {
            dictionary.setEntry(PDFInplaceOrMemoryString("ImageMask"), PDFObject::createBool(true));
        }
        else
        {
            dictionary.setEntry(PDFInplaceOrMemoryString("ColorSpace"), colorSpace);
        }
        if (!decode.empty())
        {
            PDFArrayBuilder decodeArray;
            for (PDFReal value : decode)
            {
                decodeArray.appendItem(PDFObject::createReal(value));
            }
            dictionary.setEntry(PDFInplaceOrMemoryString("Decode"), PDFObject::createArray(std::move(decodeArray)));
        }

        // JPEG images with 1 or 3 components are encoded as JPEG again (photos would be
        // too big otherwise), other images are compressed losslessly
        if (isJPEG && bitsPerComponent == 8 && (components == 1 || components == 3) && !isImageMask)
        {
            QImage jpegImage(reinterpret_cast<const uchar*>(samples.constData()), int(width), int(height), int(stride),
                             components == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
            QBuffer buffer(&encoded);
            buffer.open(QIODevice::WriteOnly);
            if (jpegImage.save(&buffer, "JPEG", 95))
            {
                encodedFilter = "DCTDecode";
            }
            else
            {
                encoded.clear();
            }
        }

        if (encodedFilter.isEmpty())
        {
            const unsigned int packedStride = (width * components * bitsPerComponent + 7) / 8;
            QByteArray packed;
            packed.reserve(int(packedStride * height));
            for (unsigned int y = 0; y < height; ++y)
            {
                packed.append(samples.constData() + size_t(y) * stride, int(packedStride));
            }
            encoded = PDFFlateDecodeFilter::compress(packed);
            encodedFilter = "FlateDecode";
        }
    }
    else
    {
        // The image is converted to RGB (JPEG 2000 images, unusual color spaces)
        if (isImageMask)
        {
            result.status = ImageResult::Status::Fail;
            result.error = PDFTranslationContext::tr("An image mask in a redacted area can't be read.");
            return result;
        }

        QImage converted;
        try
        {
            converted = image.getImage(m_owner->cms, &m_dummyReporter, nullptr);
        }
        catch (const PDFException&)
        {
            converted = QImage();
        }

        if (converted.isNull() || converted.width() != int(width) || converted.height() != int(height))
        {
            result.status = ImageResult::Status::Fail;
            result.error = PDFTranslationContext::tr("An image in a redacted area can't be decoded.");
            return result;
        }

        converted = converted.convertToFormat(QImage::Format_ARGB32);
        QByteArray rgb;
        QByteArray alpha;
        rgb.resize(int(width * height * 3));
        alpha.resize(int(width * height));
        bool hasAlpha = false;
        for (unsigned int y = 0; y < height; ++y)
        {
            const QRgb* row = reinterpret_cast<const QRgb*>(converted.constScanLine(int(y)));
            for (unsigned int x = 0; x < width; ++x)
            {
                QRgb pixel = row[x];
                if (coverage[size_t(y) * width + x])
                {
                    pixel = qRgba(0, 0, 0, 255);
                }
                const size_t index = size_t(y) * width + x;
                rgb[int(index * 3)] = char(qRed(pixel));
                rgb[int(index * 3 + 1)] = char(qGreen(pixel));
                rgb[int(index * 3 + 2)] = char(qBlue(pixel));
                alpha[int(index)] = char(qAlpha(pixel));
                hasAlpha = hasAlpha || qAlpha(pixel) != 255;
            }
        }

        dictionary.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
        dictionary.setEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceRGB"));
        encoded = PDFFlateDecodeFilter::compress(rgb);
        encodedFilter = "FlateDecode";

        if (hasAlpha)
        {
            PDFDictionaryBuilder softMask;
            softMask.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
            softMask.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
            softMask.setEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(width));
            softMask.setEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(height));
            softMask.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
            softMask.setEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceGray"));
            dictionary.setEntry(PDFInplaceOrMemoryString("SMask"), PDFObject::createReference(m_owner->builder->addObject(createFlateStream(std::move(softMask), alpha))));
        }

        dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName(encodedFilter));
        result.status = ImageResult::Status::Ok;
        result.image = PDFObject::createStream(PDFStream(std::move(dictionary), std::move(encoded)));
        return result;
    }

    // Masks of the image (they are images as well, they are redacted too)
    if (!isImageMask)
    {
        const PDFObject& maskObject = getDocument()->getObject(sourceDictionary->get("Mask"));
        if (maskObject.isArray())
        {
            dictionary.setEntry(PDFInplaceOrMemoryString("Mask"), m_owner->copier.copy(maskObject));
        }
        else if (maskObject.isStream())
        {
            QString error;
            PDFObject mask = redactMaskStream(maskObject.getStream(), false, ctm, &error);
            if (mask.isNull())
            {
                result.status = ImageResult::Status::Fail;
                result.error = error;
                return result;
            }
            dictionary.setEntry(PDFInplaceOrMemoryString("Mask"), mask);
        }

        const PDFObject& softMaskObject = getDocument()->getObject(sourceDictionary->get("SMask"));
        if (softMaskObject.isStream())
        {
            QString error;
            PDFObject softMask = redactMaskStream(softMaskObject.getStream(), true, ctm, &error);
            if (softMask.isNull())
            {
                result.status = ImageResult::Status::Fail;
                result.error = error;
                return result;
            }
            dictionary.setEntry(PDFInplaceOrMemoryString("SMask"), softMask);
        }
    }

    dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName(encodedFilter));
    result.status = ImageResult::Status::Ok;
    result.image = PDFObject::createStream(PDFStream(std::move(dictionary), std::move(encoded)));
    return result;
}

}   // namespace

// ---------------------------------------------------------------------------------------
// PDFFireRedaction
// ---------------------------------------------------------------------------------------

PDFFireRedaction::PDFFireRedaction(const PDFDocument* document,
                                   const PDFFontCache* fontCache,
                                   const PDFCMS* cms,
                                   const PDFMeshQualitySettings* meshQualitySettings,
                                   PDFDocumentBuilder* builder) :
    m_impl(std::make_unique<Impl>(document, fontCache, cms, meshQualitySettings, builder))
{

}

PDFFireRedaction::~PDFFireRedaction() = default;

QPainterPath PDFFireRedaction::getRedactionArea(const PDFDocument* document, const PDFPage* page)
{
    // The regions of the redact annotations are united. Every quadrilateral is a separate
    // polygon, the union is computed with the winding rule, so overlapping regions (boxes
    // of two adjacent text lines, for example) do not cancel each other.
    QPainterPath area;
    area.setFillRule(Qt::WindingFill);

    for (const PDFObjectReference& annotationReference : page->getAnnotations())
    {
        PDFAnnotationPtr annotation = PDFAnnotation::parse(&document->getStorage(), annotationReference);
        if (!annotation || annotation->getType() != AnnotationType::Redact)
        {
            continue;
        }

        const PDFRedactAnnotation* redactAnnotation = dynamic_cast<const PDFRedactAnnotation*>(annotation.get());
        if (!redactAnnotation)
        {
            continue;
        }

        const QList<QPolygonF> polygons = redactAnnotation->getRedactionRegion().getPath().toSubpathPolygons();
        for (const QPolygonF& polygon : polygons)
        {
            QPainterPath subpath;
            subpath.addPolygon(polygon);
            subpath.closeSubpath();
            subpath.setFillRule(Qt::WindingFill);
            area = PDFPathBoolean::unite(area, subpath);
        }
    }

    return area;
}

bool PDFFireRedaction::writePageContent(size_t pageIndex, const QPainterPath& area, PDFObjectReference newPage, QColor fillColor, QString* failureReason)
{
    const PDFPage* page = m_impl->document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        if (failureReason)
        {
            *failureReason = PDFTranslationContext::tr("Page not found.");
        }
        return false;
    }

    RedactionFilter filter(page, m_impl.get(), area);
    if (!filter.run(failureReason))
    {
        return false;
    }

    const RedactionFilter::Context* root = filter.getRootContext();
    QByteArray content = "q\n";
    content += root->content;
    content += RedactionFilter::getClosingOperators(*root);
    content += "\nQ\n";

    if (fillColor.isValid() && !area.isEmpty())
    {
        content += "q\n";
        content += formatNumber(fillColor.redF()) + ' ' + formatNumber(fillColor.greenF()) + ' ' + formatNumber(fillColor.blueF()) + " rg\n";
        writePath(content, area);
        content += area.fillRule() == Qt::OddEvenFill ? "f*\n" : "f\n";
        content += "Q\n";
    }

    PDFDictionaryBuilder streamDictionary;
    const PDFObjectReference contentReference = m_impl->builder->addObject(createFlateStream(std::move(streamDictionary), content));

    const PDFDictionary* pageDictionary = m_impl->builder->getDictionaryFromObject(m_impl->builder->getObjectByReference(newPage));
    if (!pageDictionary)
    {
        if (failureReason)
        {
            *failureReason = PDFTranslationContext::tr("Page not found.");
        }
        return false;
    }

    PDFDictionaryBuilder newPageDictionary(*pageDictionary);
    newPageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(contentReference));
    newPageDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), RedactionFilter::createResources(*root));

    // Transparency group of the page affects the rendering of the content
    const PDFDictionary* sourcePageDictionary = m_impl->document->getDictionaryFromObject(m_impl->document->getObjectByReference(page->getPageReference()));
    if (sourcePageDictionary)
    {
        for (const char* key : { "Group", "UserUnit" })
        {
            if (sourcePageDictionary->hasKey(key))
            {
                newPageDictionary.setEntry(PDFInplaceOrMemoryString(key), m_impl->copier.copy(sourcePageDictionary->get(key)));
            }
        }
    }

    m_impl->builder->setObject(newPage, PDFObject::createDictionary(std::move(newPageDictionary)));
    return true;
}

void PDFFireRedaction::Impl::appendContentToPage(PDFObjectReference page, const QByteArray& content, const std::map<QByteArray, PDFObject>& xobjects)
{
    const PDFDictionary* pageDictionary = builder->getDictionaryFromObject(builder->getObjectByReference(page));
    if (!pageDictionary)
    {
        return;
    }

    PDFDictionaryBuilder newPageDictionary(*pageDictionary);

    // Contents
    PDFArrayBuilder contents;
    const PDFObject& oldContents = pageDictionary->get("Contents");
    if (oldContents.isReference())
    {
        const PDFObject& dereferenced = builder->getObject(oldContents);
        if (dereferenced.isArray())
        {
            for (const PDFObject& item : *dereferenced.getArray())
            {
                contents.appendItem(item);
            }
        }
        else
        {
            contents.appendItem(oldContents);
        }
    }
    else if (oldContents.isArray())
    {
        for (const PDFObject& item : *oldContents.getArray())
        {
            contents.appendItem(item);
        }
    }

    PDFDictionaryBuilder streamDictionary;
    contents.appendItem(PDFObject::createReference(builder->addObject(createFlateStream(std::move(streamDictionary), content))));
    newPageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createArray(std::move(contents)));

    // Resources
    PDFDictionaryBuilder resources;
    if (const PDFDictionary* oldResources = builder->getDictionaryFromObject(pageDictionary->get("Resources")))
    {
        resources = PDFDictionaryBuilder(*oldResources);
    }

    PDFDictionaryBuilder xobjectDictionary;
    if (const PDFDictionary* oldXObjects = builder->getDictionaryFromObject(resources.get("XObject")))
    {
        xobjectDictionary = PDFDictionaryBuilder(*oldXObjects);
    }
    for (const auto& [name, object] : xobjects)
    {
        xobjectDictionary.setEntry(PDFInplaceOrMemoryString(name), object);
    }
    resources.setEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::move(xobjectDictionary)));
    newPageDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resources)));

    builder->setObject(page, PDFObject::createDictionary(std::move(newPageDictionary)));
}

void PDFFireRedaction::writeAnnotations(size_t pageIndex,
                                        const QPainterPath& area,
                                        PDFObjectReference newPage,
                                        const std::map<PDFObjectReference, PDFObjectReference>& pageMapping)
{
    const PDFDocument* document = m_impl->document;
    const PDFPage* page = document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        return;
    }

    const QRectF areaBounds = area.boundingRect();
    PDFDocumentDataLoaderDecorator loader(document);

    PDFArrayBuilder annotations;
    QByteArray flattenedContent;
    std::map<QByteArray, PDFObject> flattenedXObjects;

    static const std::set<QByteArray> markupKeys = {
        "Type", "Subtype", "Rect", "Contents", "NM", "M", "F", "AP", "AS", "Border", "C", "CA",
        "T", "CreationDate", "Subj", "RC", "IC", "BS", "BE", "LE", "L", "QuadPoints", "Vertices",
        "InkList", "DA", "DS", "Q", "RD", "Name", "Open", "CL", "IT", "LLE", "LL", "LLO", "Cap",
        "CP", "CO", "BM", "Lang", "OC", "StateModel", "State"
    };
    static const std::set<QByteArray> linkKeys = { "Type", "Subtype", "Rect", "Border", "C", "H", "BS", "F", "QuadPoints" };

    for (const PDFObjectReference& annotationReference : page->getAnnotations())
    {
        PDFAnnotationPtr annotation = PDFAnnotation::parse(&document->getStorage(), annotationReference);
        const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(annotationReference));
        if (!annotation || !dictionary)
        {
            continue;
        }

        const QRectF rectangle = annotation->getRectangle().normalized();
        if (!area.isEmpty())
        {
            QPainterPath rectanglePath;
            rectanglePath.addRect(rectangle);
            if (hitsArea(area, areaBounds, rectanglePath))
            {
                // Annotation touches the redacted area - it is removed
                continue;
            }
        }

        switch (annotation->getType())
        {
            case AnnotationType::Text:
            case AnnotationType::FreeText:
            case AnnotationType::Line:
            case AnnotationType::Square:
            case AnnotationType::Circle:
            case AnnotationType::Polygon:
            case AnnotationType::Polyline:
            case AnnotationType::Highlight:
            case AnnotationType::Underline:
            case AnnotationType::Squiggly:
            case AnnotationType::StrikeOut:
            case AnnotationType::Stamp:
            case AnnotationType::Caret:
            case AnnotationType::Ink:
            {
                PDFDictionaryBuilder copiedAnnotation;
                for (size_t i = 0; i < dictionary->getCount(); ++i)
                {
                    const QByteArray key = dictionary->getKey(i).getString();
                    if (markupKeys.count(key))
                    {
                        copiedAnnotation.addEntry(PDFInplaceOrMemoryString(key), m_impl->copier.copy(dictionary->getValue(i)));
                    }
                }
                copiedAnnotation.setEntry(PDFInplaceOrMemoryString("P"), PDFObject::createReference(newPage));
                annotations.appendItem(PDFObject::createReference(m_impl->builder->addObject(PDFObject::createDictionary(std::move(copiedAnnotation)))));
                break;
            }

            case AnnotationType::Link:
            {
                // Destination of the link (directly, or by GoTo action), or URI action
                PDFObject destinationObject = dictionary->get("Dest");
                PDFObject uri;
                if (const PDFDictionary* action = document->getDictionaryFromObject(dictionary->get("A")))
                {
                    const QByteArray actionType = loader.readNameFromDictionary(action, "S");
                    if (actionType == "URI")
                    {
                        const PDFObject& uriObject = document->getObject(action->get("URI"));
                        if (uriObject.isString())
                        {
                            uri = uriObject;
                        }
                    }
                    else if (actionType == "GoTo")
                    {
                        destinationObject = action->get("D");
                    }
                }

                PDFObject newDestination;
                if (!document->getObject(destinationObject).isNull())
                {
                    try
                    {
                        PDFDestination destination = PDFDestination::parse(&document->getStorage(), destinationObject);
                        if (destination.isNamedDestination())
                        {
                            const PDFDestination* namedDestination = document->getCatalog()->getNamedDestination(destination.getName());
                            destination = namedDestination ? *namedDestination : PDFDestination();
                        }

                        auto it = pageMapping.find(destination.getPageReference());
                        if (it != pageMapping.cend() && destination.getDestinationType() != DestinationType::Invalid)
                        {
                            destination.setPageReference(it->second);
                            PDFObjectFactory factory;
                            factory << destination;
                            newDestination = factory.takeObject();
                        }
                    }
                    catch (const PDFException&)
                    {
                        newDestination = PDFObject();
                    }
                }

                if (uri.isNull() && newDestination.isNull())
                {
                    // Link to elsewhere (other file, JavaScript...) is not copied
                    break;
                }

                PDFDictionaryBuilder copiedLink;
                for (size_t i = 0; i < dictionary->getCount(); ++i)
                {
                    const QByteArray key = dictionary->getKey(i).getString();
                    if (linkKeys.count(key))
                    {
                        copiedLink.addEntry(PDFInplaceOrMemoryString(key), m_impl->copier.copy(dictionary->getValue(i)));
                    }
                }
                if (!uri.isNull())
                {
                    PDFDictionaryBuilder action;
                    action.setEntry(PDFInplaceOrMemoryString("S"), PDFObject::createName("URI"));
                    action.setEntry(PDFInplaceOrMemoryString("URI"), uri);
                    copiedLink.setEntry(PDFInplaceOrMemoryString("A"), PDFObject::createDictionary(std::move(action)));
                }
                else
                {
                    copiedLink.setEntry(PDFInplaceOrMemoryString("Dest"), newDestination);
                }
                copiedLink.setEntry(PDFInplaceOrMemoryString("P"), PDFObject::createReference(newPage));
                annotations.appendItem(PDFObject::createReference(m_impl->builder->addObject(PDFObject::createDictionary(std::move(copiedLink)))));
                break;
            }

            case AnnotationType::Widget:
            {
                // Form field - its appearance is drawn into the page (the form itself is not
                // copied, its values of other fields could tell the redacted content)
                if (annotation->getFlags().testFlag(PDFAnnotation::Hidden) || annotation->getFlags().testFlag(PDFAnnotation::NoView))
                {
                    break;
                }

                const PDFDictionary* appearance = document->getDictionaryFromObject(dictionary->get("AP"));
                if (!appearance)
                {
                    break;
                }

                PDFObject normalAppearance = appearance->get("N");
                const PDFObject& dereferencedAppearance = document->getObject(normalAppearance);
                if (dereferencedAppearance.isDictionary())
                {
                    const PDFObject& state = document->getObject(dictionary->get("AS"));
                    if (!state.isName())
                    {
                        break;
                    }
                    normalAppearance = dereferencedAppearance.getDictionary()->get(state.getString());
                }

                const PDFObject& formObject = document->getObject(normalAppearance);
                if (!formObject.isStream())
                {
                    break;
                }

                const PDFDictionary* formDictionary = formObject.getStream()->getDictionary();
                const QRectF boundingBox = loader.readRectangle(formDictionary->get("BBox"), QRectF()).normalized();
                const QTransform formMatrix = loader.readMatrixFromDictionary(formDictionary, "Matrix", QTransform());
                const QRectF transformedBox = formMatrix.mapRect(boundingBox);
                if (transformedBox.width() <= 0.0 || transformedBox.height() <= 0.0 || rectangle.isEmpty())
                {
                    break;
                }

                const QTransform matrix = QTransform::fromTranslate(-transformedBox.left(), -transformedBox.top()) *
                                          QTransform::fromScale(rectangle.width() / transformedBox.width(), rectangle.height() / transformedBox.height()) *
                                          QTransform::fromTranslate(rectangle.left(), rectangle.top());

                const QByteArray name = "PFRap" + QByteArray::number(++m_impl->nameCounter);
                flattenedXObjects[name] = m_impl->copier.copy(normalAppearance);
                flattenedContent += "q\n" + writeMatrix(matrix) + " cm\n";
                writeName(flattenedContent, name);
                flattenedContent += " Do\nQ\n";
                break;
            }

            default:
                // Redact annotations, popups, file attachments, multimedia, 3D... are not copied
                break;
        }
    }

    if (!flattenedContent.isEmpty())
    {
        m_impl->appendContentToPage(newPage, flattenedContent, flattenedXObjects);
    }

    if (!annotations.isEmpty())
    {
        PDFDictionaryBuilder pageUpdate;
        pageUpdate.setEntry(PDFInplaceOrMemoryString("Annots"), PDFObject::createArray(std::move(annotations)));
        m_impl->builder->mergeTo(newPage, PDFObject::createDictionary(std::move(pageUpdate)));
    }
}

void PDFFireRedaction::copyOptionalContentProperties()
{
    const PDFDocument* document = m_impl->document;
    const PDFDictionary* catalog = document->getDictionaryFromObject(document->getTrailerDictionary()->get("Root"));
    if (!catalog || !catalog->hasKey("OCProperties"))
    {
        return;
    }

    PDFObject properties = m_impl->copier.copy(catalog->get("OCProperties"));
    if (properties.isReference())
    {
        m_impl->builder->setCatalogOptionalContentProperties(properties.getReference());
    }
    else if (properties.isDictionary())
    {
        m_impl->builder->setCatalogOptionalContentProperties(m_impl->builder->addObject(properties));
    }
}

}   // namespace pdf
