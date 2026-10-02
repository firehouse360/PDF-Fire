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

#include "pdffireformfields.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocument.h"
#include "pdfencoding.h"
#include "pdfannotationmanipulator.h"
#include "pdfglobal.h"
#include "pdfannotation.h"

#include <QRegularExpression>

#include <algorithm>
#include <functional>
#include <limits>
#include <set>

namespace pdf
{

namespace
{

// Field flags (PDF 32000-1:2008, 12.7.3.1, 12.7.4.2.1, 12.7.4.3, 12.7.4.4)
constexpr PDFInteger FLAG_READ_ONLY = 1 << 0;
constexpr PDFInteger FLAG_REQUIRED = 1 << 1;
constexpr PDFInteger FLAG_MULTILINE = 1 << 12;
constexpr PDFInteger FLAG_PASSWORD = 1 << 13;
constexpr PDFInteger FLAG_DO_NOT_SPELL_CHECK = 1 << 22;
constexpr PDFInteger FLAG_DO_NOT_SCROLL = 1 << 23;
constexpr PDFInteger FLAG_COMB = 1 << 24;
constexpr PDFInteger FLAG_NO_TOGGLE_TO_OFF = 1 << 14;
constexpr PDFInteger FLAG_RADIO = 1 << 15;
constexpr PDFInteger FLAG_PUSHBUTTON = 1 << 16;
constexpr PDFInteger FLAG_COMBO = 1 << 17;
constexpr PDFInteger FLAG_EDIT = 1 << 18;

// Annotation flag Print
constexpr PDFInteger ANNOTATION_PRINT = 4;

// Padding of the text in a field
constexpr PDFReal TEXT_PADDING = 2.0;

// Widths of the characters 32-126 of the standard font Helvetica (AFM, 1/1000 of the font size)
constexpr int HELVETICA_WIDTHS[] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,     // ' ' - '/'
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,     // '0' - '?'
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,    // '@' - 'O'
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,     // 'P' - '_'
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,     // '`' - 'o'
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584           // 'p' - '~'
};

// PDF Fire: widths of the characters 32-126 of the other standard fonts (AFM of the
// URW base 35 fonts, which have the metrics of the standard fonts; the quote and the
// grave accent are those of WinAnsiEncoding). Courier has all characters 600 wide.
constexpr int HELVETICA_BOLD_WIDTHS[] = {
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611,
    975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556,
    333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
    611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584
};

constexpr int TIMES_ROMAN_WIDTHS[] = {
    250, 333, 408, 500, 500, 833, 778, 180, 333, 333, 500, 564, 250, 333, 250, 278,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 278, 278, 564, 564, 564, 444,
    921, 722, 667, 667, 722, 611, 556, 722, 722, 333, 389, 722, 611, 889, 722, 722,
    556, 722, 667, 556, 611, 722, 722, 944, 722, 722, 611, 333, 278, 333, 469, 500,
    333, 444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500, 278, 778, 500, 500,
    500, 500, 333, 389, 278, 500, 500, 722, 500, 500, 444, 480, 200, 480, 541
};

constexpr int TIMES_BOLD_WIDTHS[] = {
    250, 333, 555, 500, 500, 1000, 833, 278, 333, 333, 500, 570, 250, 333, 250, 278,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333, 570, 570, 570, 500,
    930, 722, 667, 722, 722, 667, 611, 778, 778, 389, 500, 778, 667, 944, 722, 778,
    611, 778, 722, 556, 667, 722, 722, 1000, 722, 722, 667, 333, 278, 333, 581, 500,
    333, 500, 556, 444, 556, 444, 333, 500, 556, 278, 333, 556, 278, 833, 556, 500,
    556, 556, 444, 389, 333, 556, 500, 722, 500, 500, 444, 394, 220, 394, 520
};

QByteArray formatNumber(PDFReal value)
{
    if (!qIsFinite(value))
    {
        return QByteArray("0");
    }

    QByteArray text = QByteArray::number(value, 'f', 3);
    while (text.contains('.') && (text.endsWith('0') || text.endsWith('.')))
    {
        text.chop(1);
    }
    return (text.isEmpty() || text == "-0") ? QByteArray("0") : text;
}

/// Text written as a string of the standard font (WinAnsi), escaped. A character,
/// which WinAnsi does not have, is written as "?".
QByteArray toPDFString(const QString& text)
{
    QByteArray result = "(";
    for (QChar character : text)
    {
        const QString characterString(character);
        char byte = '?';
        if (character.unicode() >= 32 && PDFEncoding::canConvertToEncoding(characterString, PDFEncoding::Encoding::WinAnsi, nullptr))
        {
            const QByteArray encoded = PDFEncoding::convertToEncoding(characterString, PDFEncoding::Encoding::WinAnsi);
            byte = encoded.isEmpty() ? '?' : encoded.front();
        }
        if (byte == '(' || byte == ')' || byte == '\\')
        {
            result.append('\\');
        }
        result.append(byte);
    }
    result.append(')');
    return result;
}

QByteArray colorOperator(const QColor& color, bool stroke)
{
    return formatNumber(color.redF()) + " " + formatNumber(color.greenF()) + " " + formatNumber(color.blueF()) + (stroke ? " RG\n" : " rg\n");
}

PDFObject createTextStringObject(const QString& text)
{
    PDFObjectFactory factory;
    factory << text;
    return factory.takeObject();
}

PDFObject createColorObject(const QColor& color)
{
    PDFArrayBuilder array;
    array.appendItem(PDFObject::createReal(color.redF()));
    array.appendItem(PDFObject::createReal(color.greenF()));
    array.appendItem(PDFObject::createReal(color.blueF()));
    return PDFObject::createArray(std::move(array));
}

PDFObject createRectangleObject(const QRectF& rect)
{
    PDFArrayBuilder array;
    array.appendItem(PDFObject::createReal(rect.left()));
    array.appendItem(PDFObject::createReal(rect.top()));
    array.appendItem(PDFObject::createReal(rect.right()));
    array.appendItem(PDFObject::createReal(rect.bottom()));
    return PDFObject::createArray(std::move(array));
}

/// The array, if the object is an array (or a reference to it), otherwise nullptr
const PDFArray* getArray(const PDFObjectStorage* storage, const PDFObject& object)
{
    const PDFObject& dereferenced = storage->getObject(object);
    return dereferenced.isArray() ? dereferenced.getArray() : nullptr;
}

/// The references of an array (or of a reference to an array). The items, which are
/// not references (a null left by a deleted object, a direct dictionary), are skipped
/// - the loader of PDF4QT returns no references at all in that case, and a page would
/// lose all its annotations, when its array is written back.
std::vector<PDFObjectReference> readReferences(const PDFObjectStorage* storage, const PDFObject& object)
{
    std::vector<PDFObjectReference> references;
    if (const PDFArray* array = getArray(storage, object))
    {
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            if (array->getItem(i).isReference())
            {
                references.push_back(array->getItem(i).getReference());
            }
        }
    }
    return references;
}

std::vector<PDFObjectReference> readReferences(const PDFObjectStorage* storage, const PDFDictionary* dictionary, const char* key)
{
    return dictionary ? readReferences(storage, dictionary->get(key)) : std::vector<PDFObjectReference>();
}

PDFDictionaryBuilder copyDictionary(const PDFObjectStorage* storage, PDFObjectReference reference);

/// The page of the widget: the page of its entry P, when the widget is in the
/// annotations of that page (read tolerantly), otherwise the page found in the tree
PDFObjectReference findWidgetPage(const PDFObjectStorage* storage, PDFObjectReference widget)
{
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(widget));
    if (dictionary && dictionary->get("P").isReference())
    {
        const PDFObjectReference page = dictionary->get("P").getReference();
        const PDFDictionary* pageDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(page));
        const std::vector<PDFObjectReference> annotations = readReferences(storage, pageDictionary, "Annots");
        if (std::find(annotations.cbegin(), annotations.cend(), widget) != annotations.cend())
        {
            return page;
        }
    }
    return PDFAnnotationManipulator::findAnnotationPage(storage, widget);
}

/// Removes the reference from the array in the entry of the dictionary (the array can
/// be indirect - then it is changed in its place). The other items are kept as they are.
void removeFromArrayEntry(PDFDocumentBuilder* builder, PDFObjectReference owner, const char* key, PDFObjectReference reference)
{
    const PDFObjectStorage* storage = builder->getStorage();
    PDFDictionaryBuilder dictionary = copyDictionary(storage, owner);
    const PDFObject arrayObject = dictionary.get(key);
    const PDFArray* array = getArray(storage, arrayObject);
    if (!array)
    {
        return;
    }

    PDFArrayBuilder items;
    for (size_t i = 0; i < array->getCount(); ++i)
    {
        const PDFObject& item = array->getItem(i);
        if (!(item.isReference() && item.getReference() == reference))
        {
            items.appendItem(item);
        }
    }

    if (arrayObject.isReference())
    {
        builder->setObject(arrayObject.getReference(), PDFObject::createArray(std::move(items)));
    }
    else
    {
        dictionary.setEntry(PDFInplaceOrMemoryString(key), PDFObject::createArray(std::move(items)));
        builder->setObject(owner, PDFObject::createDictionary(std::move(dictionary)));
    }
}

PDFDictionaryBuilder copyDictionary(const PDFObjectStorage* storage, PDFObjectReference reference)
{
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(reference));
    return dictionary ? PDFDictionaryBuilder(*dictionary) : PDFDictionaryBuilder();
}

PDFDictionaryBuilder copyDictionary(const PDFObjectStorage* storage, const PDFObject& object)
{
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(object);
    return dictionary ? PDFDictionaryBuilder(*dictionary) : PDFDictionaryBuilder();
}

void setEntry(PDFDictionaryBuilder& dictionary, const char* key, PDFObject value)
{
    if (value.isNull())
    {
        dictionary.removeEntry(key);
    }
    else
    {
        dictionary.setEntry(PDFInplaceOrMemoryString(key), std::move(value));
    }
}

QColor readColor(const PDFObjectStorage* storage, const PDFObject& object)
{
    PDFDocumentDataLoaderDecorator loader(storage);
    const std::vector<PDFReal> values = loader.readNumberArray(object);
    switch (values.size())
    {
        case 1:
            return QColor::fromRgbF(values[0], values[0], values[0]);
        case 3:
            return QColor::fromRgbF(values[0], values[1], values[2]);
        case 4:
            return QColor::fromCmykF(values[0], values[1], values[2], values[3]).toRgb();
        default:
            return QColor();
    }
}

/// Rotation of the page (the inheritable entry Rotate), normalized to 0, 90, 180 or 270
int getPageRotation(const PDFObjectStorage* storage, PDFObjectReference page)
{
    PDFDocumentDataLoaderDecorator loader(storage);
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(page));
    std::set<const PDFDictionary*> visited;
    while (dictionary && visited.insert(dictionary).second)
    {
        if (dictionary->hasKey("Rotate"))
        {
            const PDFInteger rotation = loader.readInteger(dictionary->get("Rotate"), 0);
            return int(((rotation % 360) + 360) % 360 / 90 * 90);
        }
        dictionary = storage->getDictionaryFromObject(dictionary->get("Parent"));
    }
    return 0;
}

PDFObject createStandardFont(const QByteArray& resourceName, const QByteArray& baseFont)
{
    PDFDictionaryBuilder font;
    font.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Font"));
    font.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Type1"));
    font.setEntry(PDFInplaceOrMemoryString("BaseFont"), PDFObject::createName(baseFont));
    font.setEntry(PDFInplaceOrMemoryString("Encoding"), PDFObject::createName("WinAnsiEncoding"));
    font.setEntry(PDFInplaceOrMemoryString("Name"), PDFObject::createName(resourceName));
    return PDFObject::createDictionary(std::move(font));
}

PDFObject createHelveticaFont()
{
    return createStandardFont("Helv", "Helvetica");
}

/// The font of the field's text: one of the standard fonts (an unknown name - Helvetica)
const PDFFireFormFields::FontInfo& findFont(const QByteArray& fontName)
{
    const std::vector<PDFFireFormFields::FontInfo>& fonts = PDFFireFormFields::getFonts();
    for (const PDFFireFormFields::FontInfo& font : fonts)
    {
        if (font.resourceName == fontName || font.baseFont == fontName)
        {
            return font;
        }
    }
    return fonts.front();
}

PDFObject createZapfDingbatsFont()
{
    PDFDictionaryBuilder font;
    font.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Font"));
    font.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Type1"));
    font.setEntry(PDFInplaceOrMemoryString("BaseFont"), PDFObject::createName("ZapfDingbats"));
    font.setEntry(PDFInplaceOrMemoryString("Name"), PDFObject::createName("ZaDb"));
    return PDFObject::createDictionary(std::move(font));
}

bool isTextType(PDFFireFormFields::Type type)
{
    return type == PDFFireFormFields::Type::Text || type == PDFFireFormFields::Type::MultilineText || type == PDFFireFormFields::Type::Date;
}

bool isButtonType(PDFFireFormFields::Type type)
{
    return type == PDFFireFormFields::Type::CheckBox || type == PDFFireFormFields::Type::RadioButton;
}

/// The words of the text wrapped to lines of the given width (in the units of the font size)
QStringList wrapText(const QString& text, PDFReal width, const QByteArray& fontName)
{
    QStringList lines;
    for (const QString& paragraph : text.split(QChar('\n')))
    {
        QString line;
        for (const QString& word : paragraph.split(QChar(' ')))
        {
            const QString candidate = line.isEmpty() ? word : line + QChar(' ') + word;
            if (!line.isEmpty() && PDFFireFormFields::getTextWidth(fontName, candidate) > width)
            {
                lines << line;
                line = word;
            }
            else
            {
                line = candidate;
            }
        }
        lines << line;
    }
    return lines;
}

/// A circle as Bezier curves (for radio buttons)
QByteArray circlePath(QPointF center, PDFReal radius)
{
    const PDFReal k = radius * 0.5523;
    const PDFReal x = center.x();
    const PDFReal y = center.y();
    QByteArray path;
    path += formatNumber(x + radius) + " " + formatNumber(y) + " m\n";
    path += formatNumber(x + radius) + " " + formatNumber(y + k) + " " + formatNumber(x + k) + " " + formatNumber(y + radius) + " " + formatNumber(x) + " " + formatNumber(y + radius) + " c\n";
    path += formatNumber(x - k) + " " + formatNumber(y + radius) + " " + formatNumber(x - radius) + " " + formatNumber(y + k) + " " + formatNumber(x - radius) + " " + formatNumber(y) + " c\n";
    path += formatNumber(x - radius) + " " + formatNumber(y - k) + " " + formatNumber(x - k) + " " + formatNumber(y - radius) + " " + formatNumber(x) + " " + formatNumber(y - radius) + " c\n";
    path += formatNumber(x + k) + " " + formatNumber(y - radius) + " " + formatNumber(x + radius) + " " + formatNumber(y - k) + " " + formatNumber(x + radius) + " " + formatNumber(y) + " c\n";
    return path;
}

}   // namespace

PDFFireFormFields::Settings PDFFireFormFields::getDefaultSettings(Type type)
{
    Settings settings;
    settings.type = type;
    // PDF Fire: no outline by default (Mike, 2026-10-02) - the form usually has its own
    // lines and boxes, and on the screen the fields are highlighted. The outline is
    // switched on in the properties or in the menu of the field.
    settings.borderColor = QColor();
    settings.backgroundColor = QColor();
    settings.borderWidth = 1.0;

    switch (type)
    {
        case Type::Date:
            settings.dateFormat = QStringLiteral("mm/dd/yyyy");
            break;

        case Type::CheckBox:
            // PDF Fire: a check box prints only its check mark - no outline (the form
            // usually has its own box, ☐, and on the screen the field is highlighted)
            settings.exportValue = QStringLiteral("Yes");
            settings.borderColor = QColor();
            break;

        case Type::RadioButton:
            settings.exportValue = QStringLiteral("Yes");
            break;

        case Type::Signature:
            // A signature line: no box, a line along the bottom (see the appearance)
            settings.borderColor = QColor();
            break;

        default:
            break;
    }

    return settings;
}

QSizeF PDFFireFormFields::getDefaultSize(Type type)
{
    switch (type)
    {
        case Type::Text:
        case Type::Date:
        case Type::ComboBox:
            return QSizeF(150.0, 20.0);
        case Type::MultilineText:
        case Type::ListBox:
            return QSizeF(200.0, 60.0);
        case Type::CheckBox:
        case Type::RadioButton:
            return QSizeF(14.0, 14.0);
        case Type::Signature:
            return QSizeF(200.0, 36.0);
    }
    return QSizeF(150.0, 20.0);
}

QString PDFFireFormFields::getTypeName(Type type)
{
    switch (type)
    {
        case Type::Text:
            return PDFTranslationContext::tr("Text Field");
        case Type::MultilineText:
            return PDFTranslationContext::tr("Text Box (several lines)");
        case Type::Date:
            return PDFTranslationContext::tr("Date Field");
        case Type::CheckBox:
            return PDFTranslationContext::tr("Check Box");
        case Type::RadioButton:
            return PDFTranslationContext::tr("Radio Button");
        case Type::ComboBox:
            return PDFTranslationContext::tr("Drop-down List");
        case Type::ListBox:
            return PDFTranslationContext::tr("List Box");
        case Type::Signature:
            return PDFTranslationContext::tr("Signature Field");
    }
    return QString();
}

PDFReal PDFFireFormFields::getHelveticaTextWidth(const QString& text)
{
    return getTextWidth("Helv", text);
}

const std::vector<PDFFireFormFields::FontInfo>& PDFFireFormFields::getFonts()
{
    static const std::vector<FontInfo> fonts = {
        { "Helv", "Helvetica", QStringLiteral("Helvetica (Arial)") },
        { "HeBo", "Helvetica-Bold", QStringLiteral("Helvetica Bold") },
        { "TiRo", "Times-Roman", QStringLiteral("Times") },
        { "TiBo", "Times-Bold", QStringLiteral("Times Bold") },
        { "Cour", "Courier", QStringLiteral("Courier") },
        { "CoBo", "Courier-Bold", QStringLiteral("Courier Bold") }
    };
    return fonts;
}

QFont PDFFireFormFields::createSystemFont(const QByteArray& fontName)
{
    // The standard fonts by their names in the form's resources, as Acrobat writes
    // them; any other name is a family name of the system
    static const std::pair<const char*, std::pair<const char*, bool>> mapping[] = {
        { "Helv", { "Helvetica", false } }, { "HeBo", { "Helvetica", true } },
        { "Helvetica", { "Helvetica", false } }, { "Helvetica-Bold", { "Helvetica", true } },
        { "TiRo", { "Times", false } }, { "TiBo", { "Times", true } },
        { "Times-Roman", { "Times", false } }, { "Times-Bold", { "Times", true } },
        { "Cour", { "Courier", false } }, { "CoBo", { "Courier", true } },
        { "Courier", { "Courier", false } }, { "Courier-Bold", { "Courier", true } }
    };
    for (const auto& [name, family] : mapping)
    {
        if (fontName == name)
        {
            QFont font(QString::fromLatin1(family.first));
            font.setBold(family.second);
            return font;
        }
    }
    return QFont(QString::fromLatin1(fontName));
}

PDFReal PDFFireFormFields::getTextWidth(const QByteArray& fontName, const QString& text)
{
    const QByteArray baseFont = findFont(fontName).baseFont;
    const int* widths = HELVETICA_WIDTHS;
    if (baseFont == "Helvetica-Bold")
    {
        widths = HELVETICA_BOLD_WIDTHS;
    }
    else if (baseFont == "Times-Roman")
    {
        widths = TIMES_ROMAN_WIDTHS;
    }
    else if (baseFont == "Times-Bold")
    {
        widths = TIMES_BOLD_WIDTHS;
    }
    else if (baseFont.startsWith("Courier"))
    {
        return text.size() * 0.6;
    }

    PDFReal width = 0.0;
    for (QChar character : text)
    {
        const char16_t unicode = character.unicode();
        width += (unicode >= 32 && unicode <= 126) ? widths[unicode - 32] : 556;
    }
    return width / 1000.0;
}

QByteArray PDFFireFormFields::createAppearanceContent(const Settings& settings, QSizeF size, bool on)
{
    const PDFReal width = size.width();
    const PDFReal height = size.height();
    const PDFReal borderWidth = settings.borderColor.isValid() ? qMax(settings.borderWidth, 0.0) : 0.0;

    QByteArray content;

    if (settings.type == Type::RadioButton)
    {
        const QPointF center(width * 0.5, height * 0.5);
        const PDFReal radius = qMin(width, height) * 0.5;

        if (settings.backgroundColor.isValid())
        {
            content += colorOperator(settings.backgroundColor, false);
            content += circlePath(center, radius) + "f\n";
        }
        if (borderWidth > 0.0)
        {
            content += colorOperator(settings.borderColor, true);
            content += formatNumber(borderWidth) + " w\n";
            if (settings.borderStyle == BorderStyle::Dashed)
            {
                content += "[3] 0 d\n";
            }
            content += circlePath(center, radius - borderWidth * 0.5) + "S\n";
            content += "[] 0 d\n";
        }
        if (on)
        {
            content += colorOperator(settings.textColor.isValid() ? settings.textColor : QColor(Qt::black), false);
            content += circlePath(center, radius * 0.45) + "f\n";
        }
        return content;
    }

    if (settings.backgroundColor.isValid())
    {
        content += colorOperator(settings.backgroundColor, false);
        content += "0 0 " + formatNumber(width) + " " + formatNumber(height) + " re f\n";
    }

    if (borderWidth > 0.0)
    {
        content += colorOperator(settings.borderColor, true);
        content += formatNumber(borderWidth) + " w\n";
        switch (settings.borderStyle)
        {
            case BorderStyle::Underline:
                // Only a line along the bottom - a line to write on
                content += "0 " + formatNumber(borderWidth * 0.5) + " m " + formatNumber(width) + " " + formatNumber(borderWidth * 0.5) + " l S\n";
                break;

            case BorderStyle::Dashed:
                content += "[3] 0 d\n";
                content += formatNumber(borderWidth * 0.5) + " " + formatNumber(borderWidth * 0.5) + " " + formatNumber(width - borderWidth) + " " + formatNumber(height - borderWidth) + " re S\n";
                content += "[] 0 d\n";
                break;

            case BorderStyle::Solid:
                content += formatNumber(borderWidth * 0.5) + " " + formatNumber(borderWidth * 0.5) + " " + formatNumber(width - borderWidth) + " " + formatNumber(height - borderWidth) + " re S\n";
                break;
        }
    }

    const QColor textColor = settings.textColor.isValid() ? settings.textColor : QColor(Qt::black);
    const QByteArray fontName = findFont(settings.fontName).resourceName;

    switch (settings.type)
    {
        case Type::CheckBox:
        {
            if (on)
            {
                // The check mark of the ZapfDingbats font (character "4"). PDF Fire: it is
                // centred by its ink (the glyph a20 is 0.686 x 0.705 of the font size,
                // 0.035 from its origin), and fills 80 % of the box
                const PDFReal fontSize = qMax(1.0, 0.8 * qMin(width / 0.686, height / 0.705));
                content += "q\nBT\n" + colorOperator(textColor, false) + "/ZaDb " + formatNumber(fontSize) + " Tf\n";
                content += formatNumber((width - 0.686 * fontSize) * 0.5 - 0.035 * fontSize) + " " + formatNumber((height - 0.705 * fontSize) * 0.5) + " Td\n(4) Tj\nET\nQ\n";
            }
            break;
        }

        case Type::Signature:
        {
            // A signature line: a line along the bottom and an "X" before it, so the
            // place for the signature is clear also on a printed, blank form
            const PDFReal lineY = qMin(4.0, height * 0.2);
            const PDFReal fontSize = qBound(6.0, height * 0.4, 14.0);
            content += "q\n0.25 g\n0.25 G\n0.75 w\n";
            content += "2 " + formatNumber(lineY) + " m " + formatNumber(width - 2.0) + " " + formatNumber(lineY) + " l S\n";
            content += "BT\n/Helv " + formatNumber(fontSize) + " Tf\n3 " + formatNumber(lineY + 2.0) + " Td\n(X) Tj\nET\nQ\n";
            break;
        }

        case Type::Text:
        case Type::MultilineText:
        case Type::Date:
        case Type::ComboBox:
        case Type::ListBox:
        {
            QStringList lines;
            int selectedLine = -1;
            const bool isMultiline = settings.type == Type::MultilineText || settings.type == Type::ListBox;
            const PDFReal innerWidth = qMax(1.0, width - 2.0 * (TEXT_PADDING + borderWidth));

            PDFReal fontSize = settings.fontSize;
            if (fontSize <= 0.0)
            {
                // The automatic size: the size follows the height of a one-line field,
                // the text of a several-line field is written in the size 10
                fontSize = isMultiline ? 10.0 : qBound(4.0, (height - 2.0 * borderWidth) * 0.66, 12.0);
            }

            if (settings.type == Type::ListBox)
            {
                lines = settings.options;
                selectedLine = int(settings.options.indexOf(settings.value));
            }
            else if (settings.type == Type::MultilineText)
            {
                lines = wrapText(settings.password ? QString(settings.value.size(), QChar('*')) : settings.value, innerWidth / fontSize, fontName);
            }
            else if (!settings.value.isEmpty())
            {
                // A password is never written into the appearance
                const QString shownValue = settings.password ? QString(settings.value.size(), QChar('*')) : settings.value;
                lines << shownValue;

                // A text longer than the field is made smaller (automatic size only)
                const PDFReal textWidth = getTextWidth(fontName, shownValue);
                if (settings.fontSize <= 0.0 && textWidth * fontSize > innerWidth)
                {
                    fontSize = qMax(4.0, innerWidth / textWidth);
                }
            }

            content += "/Tx BMC\nq\n";
            content += formatNumber(borderWidth + 1.0) + " " + formatNumber(borderWidth + 1.0) + " " + formatNumber(qMax(0.0, width - 2.0 * borderWidth - 2.0)) + " " + formatNumber(qMax(0.0, height - 2.0 * borderWidth - 2.0)) + " re W n\n";

            if (!lines.isEmpty())
            {
                const PDFReal lineHeight = fontSize * 1.15;

                if (selectedLine >= 0)
                {
                    const PDFReal top = height - borderWidth - TEXT_PADDING - lineHeight * selectedLine;
                    content += "0.6 0.75 0.85 rg\n";
                    content += formatNumber(borderWidth + 1.0) + " " + formatNumber(top - lineHeight) + " " + formatNumber(width - 2.0 * borderWidth - 2.0) + " " + formatNumber(lineHeight) + " re f\n";
                }

                content += "BT\n/" + fontName + " " + formatNumber(fontSize) + " Tf\n" + colorOperator(textColor, false);

                PDFReal baseline = isMultiline ? (height - borderWidth - TEXT_PADDING - fontSize * 0.9) : ((height - 0.718 * fontSize) * 0.5);

                // A comb: each character in the middle of its own box (Maximal length boxes)
                const bool isComb = settings.comb && settings.type == Type::Text && settings.maxLength > 0 && !settings.password;
                if (isComb)
                {
                    const PDFReal cellWidth = width / settings.maxLength;
                    const QString text = lines.front().left(settings.maxLength);
                    for (qsizetype i = 0; i < text.size(); ++i)
                    {
                        const PDFReal characterWidth = getTextWidth(fontName, text.mid(i, 1)) * fontSize;
                        content += "1 0 0 1 " + formatNumber(cellWidth * (i + 0.5) - characterWidth * 0.5) + " " + formatNumber(baseline) + " Tm\n";
                        content += toPDFString(text.mid(i, 1)) + " Tj\n";
                    }
                    lines.clear();
                }

                for (const QString& line : lines)
                {
                    const PDFReal textWidth = getTextWidth(fontName, line) * fontSize;
                    PDFReal x = borderWidth + TEXT_PADDING;
                    if (settings.alignment == 1)
                    {
                        x = (width - textWidth) * 0.5;
                    }
                    else if (settings.alignment == 2)
                    {
                        x = width - borderWidth - TEXT_PADDING - textWidth;
                    }

                    content += "1 0 0 1 " + formatNumber(x) + " " + formatNumber(baseline) + " Tm\n";
                    content += toPDFString(line) + " Tj\n";
                    baseline -= lineHeight;
                }

                content += "ET\n";
            }

            content += "Q\nEMC\n";
            break;
        }

        case Type::RadioButton:
            Q_ASSERT(false);
            break;
    }

    return content;
}

PDFObjectReference PDFFireFormFields::getFieldOfWidget(const PDFObjectStorage* storage, PDFObjectReference widget)
{
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(widget));
    if (!dictionary)
    {
        return PDFObjectReference();
    }

    // A widget merged with its field has the name of the field; a widget, which is
    // a kid of a field (a radio button of a group), has only the parent
    if (dictionary->hasKey("T") || !dictionary->get("Parent").isReference())
    {
        return widget;
    }

    return dictionary->get("Parent").getReference();
}

QByteArray PDFFireFormFields::getOnStateName(const PDFObjectStorage* storage, PDFObjectReference widget)
{
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(widget));
    if (!dictionary)
    {
        return QByteArray();
    }

    // The name of the on state is the name of the appearance, which is not Off
    const PDFDictionary* appearance = storage->getDictionaryFromObject(dictionary->get("AP"));
    if (appearance)
    {
        for (const char* key : { "N", "D" })
        {
            if (const PDFDictionary* states = storage->getDictionaryFromObject(appearance->get(key)))
            {
                for (size_t i = 0; i < states->getCount(); ++i)
                {
                    const QByteArray name = states->getKey(i).getString();
                    if (name != "Off")
                    {
                        return name;
                    }
                }
            }
        }
    }

    return QByteArray();
}

std::optional<PDFFireFormFields::Settings> PDFFireFormFields::readField(const PDFObjectStorage* storage, PDFObjectReference widget)
{
    const PDFDictionary* widgetDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(widget));
    const PDFObjectReference field = getFieldOfWidget(storage, widget);
    const PDFDictionary* fieldDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(field));
    if (!widgetDictionary || !fieldDictionary)
    {
        return std::nullopt;
    }

    PDFDocumentDataLoaderDecorator loader(storage);

    // Inheritable entries are searched in the parents too
    auto getInherited = [storage, fieldDictionary](const char* key) -> PDFObject
    {
        const PDFDictionary* dictionary = fieldDictionary;
        std::set<const PDFDictionary*> visited;
        while (dictionary && visited.insert(dictionary).second && visited.size() <= 64)
        {
            if (dictionary->hasKey(key))
            {
                return storage->getObject(dictionary->get(key));
            }
            dictionary = storage->getDictionaryFromObject(dictionary->get("Parent"));
        }
        return PDFObject();
    };

    const QByteArray fieldType = loader.readName(getInherited("FT"));
    const PDFInteger flags = loader.readInteger(getInherited("Ff"), 0);
    const QByteArray defaultAppearance = loader.readString(getInherited("DA"));

    Settings settings;
    if (fieldType == "Tx")
    {
        settings.type = (flags & FLAG_MULTILINE) ? Type::MultilineText : Type::Text;

        // A date field is a text field, which is formatted as a date
        if (const PDFDictionary* actions = storage->getDictionaryFromObject(fieldDictionary->get("AA")))
        {
            if (const PDFDictionary* format = storage->getDictionaryFromObject(actions->get("F")))
            {
                const PDFObject& script = storage->getObject(format->get("JS"));
                const QString javaScript = script.isString() ? PDFEncoding::convertTextString(script.getString())
                                                             : (script.isStream() ? QString::fromLatin1(storage->getDecodedStream(script.getStream())) : QString());
                const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("AFDate_FormatEx\\s*\\(\\s*\"([^\"]*)\"")).match(javaScript);
                if (match.hasMatch())
                {
                    settings.type = Type::Date;
                    settings.dateFormat = match.captured(1);
                }
            }
        }
    }
    else if (fieldType == "Btn")
    {
        if (flags & FLAG_PUSHBUTTON)
        {
            return std::nullopt;
        }
        settings.type = (flags & FLAG_RADIO) ? Type::RadioButton : Type::CheckBox;
    }
    else if (fieldType == "Ch")
    {
        settings.type = (flags & FLAG_COMBO) ? Type::ComboBox : Type::ListBox;
        settings.editable = (flags & FLAG_EDIT);
    }
    else if (fieldType == "Sig")
    {
        settings.type = Type::Signature;
    }
    else
    {
        return std::nullopt;
    }

    settings.name = loader.readTextStringFromDictionary(fieldDictionary, "T", QString());
    settings.toolTip = loader.readTextStringFromDictionary(fieldDictionary, "TU", QString());
    settings.readOnly = (flags & FLAG_READ_ONLY);
    settings.required = (flags & FLAG_REQUIRED);
    settings.alignment = int(loader.readInteger(getInherited("Q"), 0));
    settings.maxLength = int(loader.readInteger(getInherited("MaxLen"), 0));

    const QRegularExpressionMatch fontSizeMatch = QRegularExpression(QStringLiteral("([0-9.]+)\\s+Tf")).match(QString::fromLatin1(defaultAppearance));
    settings.fontSize = fontSizeMatch.hasMatch() ? fontSizeMatch.captured(1).toDouble() : 0.0;

    // PDF Fire: the font and the colour of the text, the text options
    const PDFAnnotationDefaultAppearance appearance = PDFAnnotationDefaultAppearance::parse(defaultAppearance);
    const QByteArray appearanceFontName = appearance.getFontName();
    settings.fontName = (!appearanceFontName.isEmpty() && appearanceFontName != "ZaDb") ? findFont(appearanceFontName).resourceName : QByteArray("Helv");
    settings.textColor = appearance.getFontColor().isValid() ? appearance.getFontColor() : QColor(Qt::black);
    settings.password = (flags & FLAG_PASSWORD) && isTextType(settings.type);
    settings.doNotSpellCheck = (flags & FLAG_DO_NOT_SPELL_CHECK);
    settings.doNotScroll = (flags & FLAG_DO_NOT_SCROLL) && isTextType(settings.type);
    settings.comb = (flags & FLAG_COMB) && isTextType(settings.type);

    const PDFObject value = getInherited("V");
    if (isButtonType(settings.type))
    {
        settings.exportValue = QString::fromLatin1(getOnStateName(storage, widget));
        const QByteArray state = loader.readNameFromDictionary(widgetDictionary, "AS");
        settings.checked = !state.isEmpty() && state != "Off";
        settings.fontSize = 0.0;
    }
    else if (value.isString())
    {
        settings.value = PDFEncoding::convertTextString(value.getString());
    }
    else if (value.isArray() && value.getArray()->getCount() > 0)
    {
        settings.value = loader.readTextString(value.getArray()->getItem(0), QString());
    }

    if (const PDFArray* options = getArray(storage, getInherited("Opt")))
    {
        bool hasPairs = false;
        QStringList exportValues;
        for (size_t i = 0; i < options->getCount(); ++i)
        {
            const PDFObject& option = storage->getObject(options->getItem(i));
            if (option.isArray() && option.getArray()->getCount() >= 2)
            {
                // A pair [export value, displayed text]
                hasPairs = true;
                exportValues << loader.readTextString(option.getArray()->getItem(0), QString());
                settings.options << loader.readTextString(option.getArray()->getItem(1), QString());
            }
            else
            {
                const QString text = loader.readTextString(option, QString());
                exportValues << text;
                settings.options << text;
            }
        }

        if (hasPairs)
        {
            // The value is an export value - it is shown as its displayed text
            settings.exportValues = exportValues;
            const qsizetype index = exportValues.indexOf(settings.value);
            if (index >= 0)
            {
                settings.value = settings.options[index];
            }
        }
    }

    const PDFDictionary* appearanceCharacteristics = storage->getDictionaryFromObject(widgetDictionary->get("MK"));
    settings.borderColor = appearanceCharacteristics ? readColor(storage, appearanceCharacteristics->get("BC")) : QColor();
    settings.backgroundColor = appearanceCharacteristics ? readColor(storage, appearanceCharacteristics->get("BG")) : QColor();

    settings.borderWidth = 1.0;
    if (const PDFDictionary* borderStyle = storage->getDictionaryFromObject(widgetDictionary->get("BS")))
    {
        settings.borderWidth = loader.readNumberFromDictionary(borderStyle, "W", 1.0);

        // A field without a border has the width 0 - the border, when it is switched
        // on, has the usual width
        if (settings.borderWidth <= 0.0)
        {
            settings.borderWidth = 1.0;
        }
        const QByteArray style = loader.readNameFromDictionary(borderStyle, "S");
        settings.borderStyle = (style == "D") ? BorderStyle::Dashed : ((style == "U") ? BorderStyle::Underline : BorderStyle::Solid);
    }

    return settings;
}

void PDFFireFormFields::ensureAcroForm(PDFDocumentBuilder* builder)
{
    const PDFObjectStorage* storage = builder->getStorage();
    const PDFDictionary* catalog = storage->getDictionaryFromObject(storage->getObjectByReference(builder->getCatalogReference()));
    const PDFObject acroFormObject = catalog ? catalog->get("AcroForm") : PDFObject();

    PDFDictionaryBuilder form = copyDictionary(storage, acroFormObject);

    if (!form.hasKey("Fields"))
    {
        form.setEntry(PDFInplaceOrMemoryString("Fields"), PDFObject::createArray(PDFArrayBuilder()));
    }
    if (!form.hasKey("DA"))
    {
        form.setEntry(PDFInplaceOrMemoryString("DA"), PDFObject::createString("/Helv 0 Tf 0 g"));
    }

    // An XFA form would be shown instead of the fields by some readers - the
    // fields are the form now
    form.removeEntry("XFA");

    // The fonts of the fields in the default resources
    PDFDictionaryBuilder resources = copyDictionary(storage, form.get("DR"));
    PDFDictionaryBuilder fonts = copyDictionary(storage, resources.get("Font"));
    for (const FontInfo& font : getFonts())
    {
        if (!fonts.hasKey(font.resourceName))
        {
            fonts.setEntry(PDFInplaceOrMemoryString(font.resourceName), PDFObject::createReference(builder->addObject(createStandardFont(font.resourceName, font.baseFont))));
        }
    }
    if (!fonts.hasKey("ZaDb"))
    {
        fonts.setEntry(PDFInplaceOrMemoryString("ZaDb"), PDFObject::createReference(builder->addObject(createZapfDingbatsFont())));
    }

    // The font dictionary can be indirect - then it is updated in its place
    if (resources.get("Font").isReference())
    {
        builder->setObject(resources.get("Font").getReference(), PDFObject::createDictionary(std::move(fonts)));
    }
    else
    {
        resources.setEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));
    }

    if (form.get("DR").isReference())
    {
        builder->setObject(form.get("DR").getReference(), PDFObject::createDictionary(std::move(resources)));
    }
    else
    {
        form.setEntry(PDFInplaceOrMemoryString("DR"), PDFObject::createDictionary(std::move(resources)));
    }

    if (acroFormObject.isReference())
    {
        builder->setObject(acroFormObject.getReference(), PDFObject::createDictionary(std::move(form)));
    }
    else
    {
        builder->setCatalogAcroForm(builder->addObject(PDFObject::createDictionary(std::move(form))));
    }
}

void PDFFireFormFields::addToAcroFormFields(PDFDocumentBuilder* builder, PDFObjectReference field)
{
    const PDFObjectStorage* storage = builder->getStorage();
    const PDFDictionary* catalog = storage->getDictionaryFromObject(storage->getObjectByReference(builder->getCatalogReference()));
    const PDFObject acroFormObject = catalog->get("AcroForm");
    Q_ASSERT(acroFormObject.isReference());

    PDFDictionaryBuilder form = copyDictionary(storage, acroFormObject);
    const PDFObject fieldsObject = form.get("Fields");
    const PDFArray* existingFields = getArray(storage, fieldsObject);
    PDFArrayBuilder fields = existingFields ? PDFArrayBuilder(*existingFields) : PDFArrayBuilder();
    fields.appendItem(PDFObject::createReference(field));

    if (fieldsObject.isReference())
    {
        builder->setObject(fieldsObject.getReference(), PDFObject::createArray(std::move(fields)));
    }
    else
    {
        form.setEntry(PDFInplaceOrMemoryString("Fields"), PDFObject::createArray(std::move(fields)));
        builder->setObject(acroFormObject.getReference(), PDFObject::createDictionary(std::move(form)));
    }
}

void PDFFireFormFields::removeFromAcroFormFields(PDFDocumentBuilder* builder, PDFObjectReference field)
{
    const PDFObjectStorage* storage = builder->getStorage();
    const PDFDictionary* catalog = storage->getDictionaryFromObject(storage->getObjectByReference(builder->getCatalogReference()));
    const PDFObject acroFormObject = catalog ? catalog->get("AcroForm") : PDFObject();
    PDFDictionaryBuilder form = copyDictionary(storage, acroFormObject);
    const PDFObject fieldsObject = form.get("Fields");
    const PDFArray* existingFields = getArray(storage, fieldsObject);
    if (!existingFields)
    {
        return;
    }

    PDFArrayBuilder fields;
    for (size_t i = 0; i < existingFields->getCount(); ++i)
    {
        const PDFObject& item = existingFields->getItem(i);
        if (!(item.isReference() && item.getReference() == field))
        {
            fields.appendItem(item);
        }
    }

    if (fieldsObject.isReference())
    {
        builder->setObject(fieldsObject.getReference(), PDFObject::createArray(std::move(fields)));
    }
    else if (acroFormObject.isReference())
    {
        form.setEntry(PDFInplaceOrMemoryString("Fields"), PDFObject::createArray(std::move(fields)));
        builder->setObject(acroFormObject.getReference(), PDFObject::createDictionary(std::move(form)));
    }
}

void PDFFireFormFields::addToPageAnnotations(PDFDocumentBuilder* builder, PDFObjectReference page, PDFObjectReference widget)
{
    const PDFObjectStorage* storage = builder->getStorage();
    PDFDictionaryBuilder pageDictionary = copyDictionary(storage, page);
    const PDFObject annotationsObject = pageDictionary.get("Annots");
    const PDFArray* existingAnnotations = getArray(storage, annotationsObject);
    PDFArrayBuilder annotations = existingAnnotations ? PDFArrayBuilder(*existingAnnotations) : PDFArrayBuilder();
    annotations.appendItem(PDFObject::createReference(widget));

    // An indirect array of the annotations is updated in its place (it can be
    // shared, and it must not be replaced by an array with the new widget only)
    if (annotationsObject.isReference())
    {
        builder->setObject(annotationsObject.getReference(), PDFObject::createArray(std::move(annotations)));
    }
    else
    {
        pageDictionary.setEntry(PDFInplaceOrMemoryString("Annots"), PDFObject::createArray(std::move(annotations)));
        builder->setObject(page, PDFObject::createDictionary(std::move(pageDictionary)));
    }
}

void PDFFireFormFields::writeFieldEntries(PDFDocumentBuilder* builder, PDFObjectReference field, const Settings& settings, bool writeValue)
{
    const PDFObjectStorage* storage = builder->getStorage();
    PDFDictionaryBuilder dictionary = copyDictionary(storage, field);

    PDFInteger flags = 0;
    if (const PDFObject& existingFlags = storage->getObject(dictionary.get("Ff")); existingFlags.isInt())
    {
        // Flags, which the form maker does not set, are kept
        flags = existingFlags.getInteger() & ~(FLAG_READ_ONLY | FLAG_REQUIRED | FLAG_MULTILINE | FLAG_EDIT |
                                               FLAG_PASSWORD | FLAG_DO_NOT_SPELL_CHECK | FLAG_DO_NOT_SCROLL | FLAG_COMB);
    }
    flags |= settings.readOnly ? FLAG_READ_ONLY : 0;
    flags |= settings.required ? FLAG_REQUIRED : 0;

    const QByteArray fontSize = formatNumber(qMax(0.0, settings.fontSize));
    const QColor textColor = settings.textColor.isValid() ? settings.textColor : QColor(Qt::black);
    const QByteArray textColorOperator = (textColor.red() == textColor.green() && textColor.green() == textColor.blue())
                                         ? formatNumber(textColor.redF()) + " g"
                                         : colorOperator(textColor, false).trimmed();
    const QByteArray textAppearance = "/" + findFont(settings.fontName).resourceName + " " + fontSize + " Tf " + textColorOperator;
    const QByteArray buttonAppearance = "/ZaDb 0 Tf " + textColorOperator;

    switch (settings.type)
    {
        case Type::Text:
        case Type::MultilineText:
        case Type::Date:
            setEntry(dictionary, "FT", PDFObject::createName("Tx"));
            flags |= (settings.type == Type::MultilineText) ? FLAG_MULTILINE : 0;
            flags |= settings.doNotScroll ? FLAG_DO_NOT_SCROLL : 0;
            flags |= settings.doNotSpellCheck ? FLAG_DO_NOT_SPELL_CHECK : 0;
            if (settings.type != Type::Date)
            {
                flags |= settings.password ? FLAG_PASSWORD : 0;

                // A comb needs the maximal length, and it is one line of plain text
                const bool isComb = settings.comb && settings.type == Type::Text && settings.maxLength > 0 && !settings.password;
                flags |= isComb ? FLAG_COMB : 0;
            }
            setEntry(dictionary, "DA", PDFObject::createString(textAppearance));
            setEntry(dictionary, "Q", settings.alignment ? PDFObject::createInteger(settings.alignment) : PDFObject());
            setEntry(dictionary, "MaxLen", settings.maxLength > 0 ? PDFObject::createInteger(settings.maxLength) : PDFObject());
            if (writeValue)
            {
                setEntry(dictionary, "V", createTextStringObject(settings.value));
                setEntry(dictionary, "DV", createTextStringObject(settings.value));
            }
            break;

        case Type::CheckBox:
            setEntry(dictionary, "FT", PDFObject::createName("Btn"));
            setEntry(dictionary, "DA", PDFObject::createString(buttonAppearance));
            break;

        case Type::RadioButton:
            setEntry(dictionary, "FT", PDFObject::createName("Btn"));
            flags |= FLAG_RADIO | FLAG_NO_TOGGLE_TO_OFF;
            setEntry(dictionary, "DA", PDFObject::createString(buttonAppearance));
            break;

        case Type::ComboBox:
        case Type::ListBox:
        {
            setEntry(dictionary, "FT", PDFObject::createName("Ch"));
            if (settings.type == Type::ComboBox)
            {
                flags |= FLAG_COMBO | (settings.editable ? FLAG_EDIT : 0);
                flags |= (settings.editable && settings.doNotSpellCheck) ? FLAG_DO_NOT_SPELL_CHECK : 0;
            }
            setEntry(dictionary, "DA", PDFObject::createString(textAppearance));
            setEntry(dictionary, "Q", settings.alignment ? PDFObject::createInteger(settings.alignment) : PDFObject());

            // Items with an export value different from the displayed text are written
            // as pairs [export value, displayed text], and the value is the export value
            const bool hasExportValues = settings.exportValues.size() == settings.options.size() && settings.exportValues != settings.options;
            PDFArrayBuilder options;
            for (qsizetype i = 0; i < settings.options.size(); ++i)
            {
                if (hasExportValues)
                {
                    PDFArrayBuilder pair;
                    pair.appendItem(createTextStringObject(settings.exportValues[i]));
                    pair.appendItem(createTextStringObject(settings.options[i]));
                    options.appendItem(PDFObject::createArray(std::move(pair)));
                }
                else
                {
                    options.appendItem(createTextStringObject(settings.options[i]));
                }
            }
            setEntry(dictionary, "Opt", PDFObject::createArray(std::move(options)));

            if (writeValue)
            {
                QString value = settings.value;
                const qsizetype index = settings.options.indexOf(value);
                if (hasExportValues && index >= 0)
                {
                    value = settings.exportValues[index];
                }
                setEntry(dictionary, "V", value.isEmpty() ? PDFObject() : createTextStringObject(value));
                setEntry(dictionary, "DV", value.isEmpty() ? PDFObject() : createTextStringObject(value));
            }
            break;
        }

        case Type::Signature:
            setEntry(dictionary, "FT", PDFObject::createName("Sig"));
            setEntry(dictionary, "DA", PDFObject::createString("/Helv 0 Tf 0 g"));
            break;
    }

    if (settings.type == Type::Date)
    {
        // The formatting of a date the way of Acrobat (the readers, which do not
        // run JavaScript, show the field as an ordinary text field)
        const QByteArray format = settings.dateFormat.toLatin1().replace('"', QByteArray()).replace('\\', QByteArray());
        PDFDictionaryBuilder formatAction;
        formatAction.setEntry(PDFInplaceOrMemoryString("S"), PDFObject::createName("JavaScript"));
        formatAction.setEntry(PDFInplaceOrMemoryString("JS"), PDFObject::createString("AFDate_FormatEx(\"" + format + "\");"));
        PDFDictionaryBuilder keystrokeAction;
        keystrokeAction.setEntry(PDFInplaceOrMemoryString("S"), PDFObject::createName("JavaScript"));
        keystrokeAction.setEntry(PDFInplaceOrMemoryString("JS"), PDFObject::createString("AFDate_KeystrokeEx(\"" + format + "\");"));
        PDFDictionaryBuilder actions = copyDictionary(storage, dictionary.get("AA"));
        actions.setEntry(PDFInplaceOrMemoryString("F"), PDFObject::createDictionary(std::move(formatAction)));
        actions.setEntry(PDFInplaceOrMemoryString("K"), PDFObject::createDictionary(std::move(keystrokeAction)));
        dictionary.setEntry(PDFInplaceOrMemoryString("AA"), PDFObject::createDictionary(std::move(actions)));
    }

    setEntry(dictionary, "Ff", PDFObject::createInteger(flags));
    setEntry(dictionary, "T", createTextStringObject(settings.name));
    setEntry(dictionary, "TU", settings.toolTip.isEmpty() ? PDFObject() : createTextStringObject(settings.toolTip));

    builder->setObject(field, PDFObject::createDictionary(std::move(dictionary)));
}

void PDFFireFormFields::writeWidgetAppearance(PDFDocumentBuilder* builder, PDFObjectReference widget, const Settings& settings, QRectF rect, const QByteArray& onStateName)
{
    const PDFObjectStorage* storage = builder->getStorage();
    PDFDictionaryBuilder dictionary = copyDictionary(storage, widget);

    // On a rotated page the field is rotated with the page, so its text is upright,
    // when the page is shown (the readers do the same, when they create appearances)
    PDFObjectReference page = dictionary.get("P").isReference() ? dictionary.get("P").getReference() : PDFObjectReference();
    if (!page.isValid())
    {
        page = findWidgetPage(storage, widget);
    }
    const int rotation = page.isValid() ? getPageRotation(storage, page) : 0;
    const QSizeF appearanceSize = (rotation == 90 || rotation == 270) ? rect.size().transposed() : rect.size();

    setEntry(dictionary, "Type", PDFObject::createName("Annot"));
    setEntry(dictionary, "Subtype", PDFObject::createName("Widget"));
    setEntry(dictionary, "Rect", createRectangleObject(rect));

    // Appearance characteristics: colors and the caption of a button
    PDFDictionaryBuilder characteristics = copyDictionary(storage, dictionary.get("MK"));
    setEntry(characteristics, "BC", settings.borderColor.isValid() ? createColorObject(settings.borderColor) : PDFObject());
    setEntry(characteristics, "BG", settings.backgroundColor.isValid() ? createColorObject(settings.backgroundColor) : PDFObject());
    if (settings.type == Type::CheckBox)
    {
        characteristics.setEntry(PDFInplaceOrMemoryString("CA"), PDFObject::createString("4"));
    }
    else if (settings.type == Type::RadioButton)
    {
        characteristics.setEntry(PDFInplaceOrMemoryString("CA"), PDFObject::createString("l"));
    }
    setEntry(characteristics, "R", rotation ? PDFObject::createInteger(rotation) : PDFObject());
    dictionary.setEntry(PDFInplaceOrMemoryString("MK"), PDFObject::createDictionary(std::move(characteristics)));

    PDFDictionaryBuilder borderStyle;
    borderStyle.setEntry(PDFInplaceOrMemoryString("W"), PDFObject::createReal(settings.borderColor.isValid() ? settings.borderWidth : 0.0));
    switch (settings.borderStyle)
    {
        case BorderStyle::Dashed:
        {
            borderStyle.setEntry(PDFInplaceOrMemoryString("S"), PDFObject::createName("D"));
            PDFArrayBuilder dashes;
            dashes.appendItem(PDFObject::createInteger(3));
            borderStyle.setEntry(PDFInplaceOrMemoryString("D"), PDFObject::createArray(std::move(dashes)));
            break;
        }
        case BorderStyle::Underline:
            borderStyle.setEntry(PDFInplaceOrMemoryString("S"), PDFObject::createName("U"));
            break;
        case BorderStyle::Solid:
            borderStyle.setEntry(PDFInplaceOrMemoryString("S"), PDFObject::createName("S"));
            break;
    }
    dictionary.setEntry(PDFInplaceOrMemoryString("BS"), PDFObject::createDictionary(std::move(borderStyle)));

    // The appearance streams
    auto createStream = [&](bool on) -> PDFObjectReference
    {
        PDFDictionaryBuilder fonts;
        fonts.setEntry(PDFInplaceOrMemoryString("Helv"), createHelveticaFont());
        fonts.setEntry(PDFInplaceOrMemoryString("ZaDb"), createZapfDingbatsFont());
        const FontInfo& textFont = findFont(settings.fontName);
        if (textFont.resourceName != "Helv")
        {
            fonts.setEntry(PDFInplaceOrMemoryString(textFont.resourceName), createStandardFont(textFont.resourceName, textFont.baseFont));
        }
        PDFDictionaryBuilder resources;
        resources.setEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));

        PDFArrayBuilder boundingBox;
        boundingBox.appendItem(PDFObject::createInteger(0));
        boundingBox.appendItem(PDFObject::createInteger(0));
        boundingBox.appendItem(PDFObject::createReal(appearanceSize.width()));
        boundingBox.appendItem(PDFObject::createReal(appearanceSize.height()));

        PDFDictionaryBuilder streamDictionary;
        streamDictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
        streamDictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
        streamDictionary.setEntry(PDFInplaceOrMemoryString("BBox"), PDFObject::createArray(std::move(boundingBox)));
        streamDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resources)));

        if (rotation)
        {
            // The matrix turns the appearance (drawn upright) by the rotation counterclockwise,
            // into the rectangle of the widget
            PDFReal matrix[6] = { 1, 0, 0, 1, 0, 0 };
            switch (rotation)
            {
                case 90: { const PDFReal values[6] = { 0, 1, -1, 0, appearanceSize.height(), 0 }; std::copy(values, values + 6, matrix); break; }
                case 180: { const PDFReal values[6] = { -1, 0, 0, -1, appearanceSize.width(), appearanceSize.height() }; std::copy(values, values + 6, matrix); break; }
                case 270: { const PDFReal values[6] = { 0, -1, 1, 0, 0, appearanceSize.width() }; std::copy(values, values + 6, matrix); break; }
                default: break;
            }
            PDFArrayBuilder matrixArray;
            for (PDFReal value : matrix)
            {
                matrixArray.appendItem(PDFObject::createReal(value));
            }
            streamDictionary.setEntry(PDFInplaceOrMemoryString("Matrix"), PDFObject::createArray(std::move(matrixArray)));
        }

        QByteArray content = createAppearanceContent(settings, appearanceSize, on);
        streamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
        return builder->addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), std::move(content))));
    };

    PDFDictionaryBuilder appearance;
    if (isButtonType(settings.type))
    {
        PDFDictionaryBuilder states;
        states.setEntry(PDFInplaceOrMemoryString(onStateName), PDFObject::createReference(createStream(true)));
        states.setEntry(PDFInplaceOrMemoryString("Off"), PDFObject::createReference(createStream(false)));
        appearance.setEntry(PDFInplaceOrMemoryString("N"), PDFObject::createDictionary(std::move(states)));
        dictionary.setEntry(PDFInplaceOrMemoryString("AS"), PDFObject::createName(settings.checked ? onStateName : QByteArray("Off")));
    }
    else
    {
        appearance.setEntry(PDFInplaceOrMemoryString("N"), PDFObject::createReference(createStream(false)));
    }
    dictionary.setEntry(PDFInplaceOrMemoryString("AP"), PDFObject::createDictionary(std::move(appearance)));

    builder->setObject(widget, PDFObject::createDictionary(std::move(dictionary)));
}

PDFObjectReference PDFFireFormFields::createField(PDFDocumentBuilder* builder, PDFObjectReference page, QRectF rect, const Settings& settings)
{
    rect = rect.normalized();
    if (!qIsFinite(rect.left()) || !qIsFinite(rect.top()) || !qIsFinite(rect.width()) || !qIsFinite(rect.height()) || rect.isEmpty())
    {
        return PDFObjectReference();
    }

    ensureAcroForm(builder);
    const PDFObjectStorage* storage = builder->getStorage();

    auto createWidgetDictionary = [&]() -> PDFDictionaryBuilder
    {
        PDFDictionaryBuilder dictionary;
        dictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Annot"));
        dictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Widget"));
        dictionary.setEntry(PDFInplaceOrMemoryString("P"), PDFObject::createReference(page));
        dictionary.setEntry(PDFInplaceOrMemoryString("F"), PDFObject::createInteger(ANNOTATION_PRINT));
        return dictionary;
    };

    if (settings.type == Type::RadioButton)
    {
        // The group: the field of the same name, which is a group of radio buttons
        PDFObjectReference group;
        const PDFDictionary* catalog = storage->getDictionaryFromObject(storage->getObjectByReference(builder->getCatalogReference()));
        const PDFDictionary* form = storage->getDictionaryFromObject(catalog->get("AcroForm"));
        PDFDocumentDataLoaderDecorator loader(storage);
        for (PDFObjectReference field : readReferences(storage, form, "Fields"))
        {
            const PDFDictionary* fieldDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(field));
            if (fieldDictionary &&
                loader.readTextStringFromDictionary(fieldDictionary, "T", QString()) == settings.name &&
                loader.readNameFromDictionary(fieldDictionary, "FT") == "Btn" &&
                (loader.readIntegerFromDictionary(fieldDictionary, "Ff", 0) & FLAG_RADIO))
            {
                group = field;
                break;
            }
        }

        std::vector<PDFObjectReference> kids;
        if (!group.isValid())
        {
            PDFDictionaryBuilder groupDictionary;
            groupDictionary.setEntry(PDFInplaceOrMemoryString("V"), PDFObject::createName("Off"));
            group = builder->addObject(PDFObject::createDictionary(std::move(groupDictionary)));
            writeFieldEntries(builder, group, settings, true);
            addToAcroFormFields(builder, group);
        }
        else
        {
            const PDFDictionary* groupDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(group));
            kids = readReferences(storage, groupDictionary, "Kids");
        }

        // The value of the new button must differ from the values of the other buttons
        std::set<QByteArray> usedStates;
        for (PDFObjectReference kid : kids)
        {
            usedStates.insert(getOnStateName(storage, kid));
        }
        QByteArray onStateName = settings.exportValue.toLatin1();
        if (onStateName.isEmpty() || onStateName == "Off" || usedStates.count(onStateName))
        {
            int index = 1;
            while (usedStates.count("Choice" + QByteArray::number(index)))
            {
                ++index;
            }
            onStateName = "Choice" + QByteArray::number(index);
        }

        PDFDictionaryBuilder widgetDictionary = createWidgetDictionary();
        widgetDictionary.setEntry(PDFInplaceOrMemoryString("Parent"), PDFObject::createReference(group));
        const PDFObjectReference widget = builder->addObject(PDFObject::createDictionary(std::move(widgetDictionary)));

        Settings widgetSettings = settings;
        widgetSettings.exportValue = QString::fromLatin1(onStateName);
        writeWidgetAppearance(builder, widget, widgetSettings, rect, onStateName);

        kids.push_back(widget);
        PDFDictionaryBuilder groupDictionary = copyDictionary(storage, group);
        PDFArrayBuilder kidsArray;
        for (PDFObjectReference kid : kids)
        {
            kidsArray.appendItem(PDFObject::createReference(kid));
        }
        groupDictionary.setEntry(PDFInplaceOrMemoryString("Kids"), PDFObject::createArray(std::move(kidsArray)));
        builder->setObject(group, PDFObject::createDictionary(std::move(groupDictionary)));

        if (settings.checked)
        {
            updateField(builder, widget, widgetSettings);
        }

        addToPageAnnotations(builder, page, widget);
        return widget;
    }

    const PDFObjectReference widget = builder->addObject(PDFObject::createDictionary(createWidgetDictionary()));
    writeFieldEntries(builder, widget, settings, true);

    QByteArray onStateName = settings.exportValue.toLatin1();
    if (onStateName.isEmpty() || onStateName == "Off")
    {
        onStateName = "Yes";
    }
    writeWidgetAppearance(builder, widget, settings, rect, onStateName);

    if (settings.type == Type::CheckBox)
    {
        PDFDictionaryBuilder dictionary = copyDictionary(storage, widget);
        dictionary.setEntry(PDFInplaceOrMemoryString("V"), PDFObject::createName(settings.checked ? onStateName : QByteArray("Off")));
        builder->setObject(widget, PDFObject::createDictionary(std::move(dictionary)));
    }

    addToAcroFormFields(builder, widget);
    addToPageAnnotations(builder, page, widget);
    return widget;
}

void PDFFireFormFields::updateField(PDFDocumentBuilder* builder, PDFObjectReference widget, const Settings& newSettings)
{
    const PDFObjectStorage* storage = builder->getStorage();
    const std::optional<Settings> oldSettings = readField(storage, widget);
    if (!oldSettings)
    {
        return;
    }

    // The type of a field is not changed - except a text field, which can be switched
    // between one line and several lines (Multi-line, the word wrap)
    Settings settings = newSettings;
    const bool isLineSwitch = (oldSettings->type == Type::Text || oldSettings->type == Type::MultilineText) &&
                              (newSettings.type == Type::Text || newSettings.type == Type::MultilineText);
    if (!isLineSwitch)
    {
        settings.type = oldSettings->type;
    }

    // The fonts of the fields in the form's resources (an older form has Helvetica only)
    ensureAcroForm(builder);

    const PDFObjectReference field = getFieldOfWidget(storage, widget);

    // The value is written only, when it was changed - a value, which the dialog
    // does not show exactly (a rich text), is kept
    const bool isValueChanged = (settings.value != oldSettings->value) || (settings.options != oldSettings->options);
    writeFieldEntries(builder, field, settings, isValueChanged);

    QByteArray onStateName = settings.exportValue.toLatin1();
    if (onStateName.isEmpty() || onStateName == "Off")
    {
        onStateName = oldSettings->exportValue.toLatin1();
    }

    // Buttons of one field must have different values, otherwise they switch together
    if (field != widget && isButtonType(settings.type))
    {
        const PDFDictionary* fieldDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(field));
        for (PDFObjectReference kid : readReferences(storage, fieldDictionary, "Kids"))
        {
            if (kid != widget && getOnStateName(storage, kid) == onStateName)
            {
                onStateName = oldSettings->exportValue.toLatin1();
                break;
            }
        }
    }

    if (onStateName.isEmpty())
    {
        onStateName = "Yes";
    }

    writeWidgetAppearance(builder, widget, settings, getWidgetRect(storage, widget), onStateName);

    if (settings.type == Type::CheckBox)
    {
        // The value belongs to the field; the other widgets of the field are unchecked
        PDFDictionaryBuilder dictionary = copyDictionary(storage, field);
        dictionary.setEntry(PDFInplaceOrMemoryString("V"), PDFObject::createName(settings.checked ? onStateName : QByteArray("Off")));
        builder->setObject(field, PDFObject::createDictionary(std::move(dictionary)));

        if (field != widget && settings.checked)
        {
            const PDFDictionary* fieldDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(field));
            for (PDFObjectReference kid : readReferences(storage, fieldDictionary, "Kids"))
            {
                if (kid != widget)
                {
                    PDFDictionaryBuilder kidDictionary = copyDictionary(storage, kid);
                    kidDictionary.setEntry(PDFInplaceOrMemoryString("AS"), PDFObject::createName("Off"));
                    builder->setObject(kid, PDFObject::createDictionary(std::move(kidDictionary)));
                }
            }
        }
    }
    else if (settings.type == Type::RadioButton)
    {
        // Only one button of the group is on
        PDFDocumentDataLoaderDecorator loader(storage);
        const PDFDictionary* groupDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(field));
        const QByteArray groupValue = loader.readNameFromDictionary(groupDictionary, "V");

        if (settings.checked)
        {
            for (PDFObjectReference kid : readReferences(storage, groupDictionary, "Kids"))
            {
                if (kid != widget)
                {
                    PDFDictionaryBuilder kidDictionary = copyDictionary(storage, kid);
                    kidDictionary.setEntry(PDFInplaceOrMemoryString("AS"), PDFObject::createName("Off"));
                    builder->setObject(kid, PDFObject::createDictionary(std::move(kidDictionary)));
                }
            }
        }

        PDFDictionaryBuilder group = copyDictionary(storage, field);
        if (settings.checked)
        {
            group.setEntry(PDFInplaceOrMemoryString("V"), PDFObject::createName(onStateName));
        }
        else if (groupValue == oldSettings->exportValue.toLatin1())
        {
            group.setEntry(PDFInplaceOrMemoryString("V"), PDFObject::createName("Off"));
        }
        builder->setObject(field, PDFObject::createDictionary(std::move(group)));
    }
}

void PDFFireFormFields::setWidgetRect(PDFDocumentBuilder* builder, PDFObjectReference widget, QRectF rect)
{
    const PDFObjectStorage* storage = builder->getStorage();
    const std::optional<Settings> settings = readField(storage, widget);
    if (!settings)
    {
        return;
    }

    QByteArray onStateName = getOnStateName(storage, widget);
    if (onStateName.isEmpty())
    {
        onStateName = "Yes";
    }

    writeWidgetAppearance(builder, widget, *settings, rect.normalized(), onStateName);
}

PDFObjectReference PDFFireFormFields::duplicateField(PDFDocumentBuilder* builder, PDFObjectReference widget, QRectF rect)
{
    const PDFObjectStorage* storage = builder->getStorage();
    std::optional<Settings> settings = readField(storage, widget);
    if (!settings)
    {
        return PDFObjectReference();
    }

    PDFObjectReference page = findWidgetPage(storage, widget);
    if (!page.isValid())
    {
        return PDFObjectReference();
    }

    if (settings->type == Type::RadioButton)
    {
        // A copy of a radio button is another button of the same group
        settings->exportValue.clear();
    }
    else
    {
        QString base = settings->name;
        while (!base.isEmpty() && base.back().isDigit())
        {
            base.chop(1);
        }
        settings->name = createUniqueName(storage, base.trimmed().isEmpty() ? QStringLiteral("Field") : base);
    }
    settings->checked = false;
    return createField(builder, page, rect, *settings);
}

void PDFFireFormFields::removeWidget(PDFDocumentBuilder* builder, PDFObjectReference widget)
{
    const PDFObjectStorage* storage = builder->getStorage();
    const PDFObjectReference field = getFieldOfWidget(storage, widget);
    const PDFObjectReference page = findWidgetPage(storage, widget);
    const QByteArray onStateName = getOnStateName(storage, widget);
    PDFDocumentDataLoaderDecorator loader(storage);

    // The parent of the widget (a group of radio buttons, or the parent of a field)
    const PDFDictionary* widgetDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(widget));
    PDFObjectReference parent = widgetDictionary ? loader.readReferenceFromDictionary(widgetDictionary, "Parent") : PDFObjectReference();

    if (page.isValid())
    {
        removeFromArrayEntry(builder, page, "Annots", widget);
    }
    builder->setObject(widget, PDFObject());

    if (field == widget)
    {
        removeFromAcroFormFields(builder, widget);
    }

    // The widget is removed from the kids of its parent; a parent left without kids
    // is removed too (from its own parent, or from the form), up the tree
    PDFObjectReference child = widget;
    for (int depth = 0; parent.isValid() && depth < 64; ++depth)
    {
        removeFromArrayEntry(builder, parent, "Kids", child);

        const PDFDictionary* parentDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(parent));
        if (!readReferences(storage, parentDictionary, "Kids").empty())
        {
            // A group of buttons, whose chosen button was removed, has nothing chosen
            if (!onStateName.isEmpty() && loader.readNameFromDictionary(parentDictionary, "V") == onStateName)
            {
                PDFDictionaryBuilder group = copyDictionary(storage, parent);
                group.setEntry(PDFInplaceOrMemoryString("V"), PDFObject::createName("Off"));
                builder->setObject(parent, PDFObject::createDictionary(std::move(group)));
            }
            break;
        }

        const PDFObjectReference grandparent = parentDictionary ? loader.readReferenceFromDictionary(parentDictionary, "Parent") : PDFObjectReference();
        removeFromAcroFormFields(builder, parent);
        builder->setObject(parent, PDFObject());
        child = parent;
        parent = grandparent;
    }
}

QRectF PDFFireFormFields::getWidgetRect(const PDFObjectStorage* storage, PDFObjectReference widget)
{
    PDFDocumentDataLoaderDecorator loader(storage);
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(widget));
    return dictionary ? loader.readRectangle(dictionary->get("Rect"), QRectF()).normalized() : QRectF();
}

std::vector<PDFObjectReference> PDFFireFormFields::getWidgets(const PDFObjectStorage* storage, PDFObjectReference page)
{
    std::vector<PDFObjectReference> widgets;
    PDFDocumentDataLoaderDecorator loader(storage);
    const PDFDictionary* pageDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(page));
    for (PDFObjectReference annotation : readReferences(storage, pageDictionary, "Annots"))
    {
        const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(annotation));
        if (dictionary && loader.readNameFromDictionary(dictionary, "Subtype") == "Widget")
        {
            widgets.push_back(annotation);
        }
    }
    return widgets;
}

QStringList PDFFireFormFields::getFieldNames(const PDFObjectStorage* storage)
{
    QStringList names;
    PDFDocumentDataLoaderDecorator loader(storage);
    const PDFDictionary* trailer = storage->getDictionaryFromObject(storage->getTrailerDictionary());
    const PDFDictionary* catalog = trailer ? storage->getDictionaryFromObject(trailer->get("Root")) : nullptr;
    const PDFDictionary* form = catalog ? storage->getDictionaryFromObject(catalog->get("AcroForm")) : nullptr;
    if (!form)
    {
        return names;
    }

    // The tree of the fields is walked without recursion, with limits - a damaged or
    // malicious document can have a very deep tree, or a cycle
    std::set<PDFObjectReference> visited;
    std::vector<std::tuple<PDFObjectReference, QString, int>> stack;
    const std::vector<PDFObjectReference> fields = readReferences(storage, form, "Fields");
    for (auto it = fields.crbegin(); it != fields.crend(); ++it)
    {
        stack.emplace_back(*it, QString(), 0);
    }

    while (!stack.empty() && visited.size() < 100000)
    {
        const auto [field, prefix, depth] = stack.back();
        stack.pop_back();

        if (depth > 64 || !visited.insert(field).second)
        {
            continue;
        }

        const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(field));
        if (!dictionary)
        {
            continue;
        }

        const QString name = loader.readTextStringFromDictionary(dictionary, "T", QString());
        const QString fullName = name.isEmpty() ? prefix : (prefix.isEmpty() ? name : prefix + QChar('.') + name);
        if (!name.isEmpty())
        {
            names << fullName;
        }

        const std::vector<PDFObjectReference> kids = readReferences(storage, dictionary, "Kids");
        for (auto it = kids.crbegin(); it != kids.crend(); ++it)
        {
            stack.emplace_back(*it, fullName, depth + 1);
        }
    }

    return names;
}

QString PDFFireFormFields::createUniqueName(const PDFObjectStorage* storage, QString base)
{
    base = base.trimmed();
    if (base.isEmpty())
    {
        base = QStringLiteral("Field");
    }

    const QStringList names = getFieldNames(storage);
    for (int index = 1;; ++index)
    {
        const QString name = base + QString::number(index);
        if (!names.contains(name, Qt::CaseInsensitive))
        {
            return name;
        }
    }
}

void PDFFireFormFields::sortTabOrderByRows(PDFDocumentBuilder* builder, PDFObjectReference page)
{
    const PDFObjectStorage* storage = builder->getStorage();
    PDFDocumentDataLoaderDecorator loader(storage);
    PDFDictionaryBuilder pageDictionary = copyDictionary(storage, page);
    const PDFObject annotationsObject = pageDictionary.get("Annots");
    const PDFArray* annotations = getArray(storage, annotationsObject);
    if (!annotations)
    {
        return;
    }

    struct Item
    {
        PDFObjectReference reference;
        QRectF rect;
    };

    // The items, which are not widgets (other annotations, and anything, which is
    // not a reference), are kept as they are, before the widgets
    std::vector<PDFObject> others;
    std::vector<Item> widgets;
    for (size_t i = 0; i < annotations->getCount(); ++i)
    {
        const PDFObject& item = annotations->getItem(i);
        const PDFDictionary* dictionary = item.isReference() ? storage->getDictionaryFromObject(storage->getObjectByReference(item.getReference())) : nullptr;
        if (dictionary && loader.readNameFromDictionary(dictionary, "Subtype") == "Widget")
        {
            widgets.push_back(Item{ item.getReference(), getWidgetRect(storage, item.getReference()) });
        }
        else
        {
            others.push_back(item);
        }
    }

    // Rows: a widget belongs to the row, if its middle is within the row (the
    // y axis of a page goes up)
    std::stable_sort(widgets.begin(), widgets.end(), [](const Item& left, const Item& right) { return left.rect.bottom() > right.rect.bottom(); });
    std::vector<std::vector<Item>> rows;
    for (const Item& item : widgets)
    {
        const PDFReal middle = item.rect.center().y();
        if (!rows.empty())
        {
            const QRectF& rowRect = rows.back().front().rect;
            if (middle >= rowRect.top() && middle <= rowRect.bottom())
            {
                rows.back().push_back(item);
                continue;
            }
        }
        rows.push_back({ item });
    }

    PDFArrayBuilder sortedAnnotations;
    for (const PDFObject& item : others)
    {
        sortedAnnotations.appendItem(item);
    }
    for (std::vector<Item>& row : rows)
    {
        std::stable_sort(row.begin(), row.end(), [](const Item& left, const Item& right) { return left.rect.left() < right.rect.left(); });
        for (const Item& item : row)
        {
            sortedAnnotations.appendItem(PDFObject::createReference(item.reference));
        }
    }

    // The readers following the order of the rows themselves are told so
    pageDictionary.setEntry(PDFInplaceOrMemoryString("Tabs"), PDFObject::createName("R"));
    if (annotationsObject.isReference())
    {
        builder->setObject(annotationsObject.getReference(), PDFObject::createArray(std::move(sortedAnnotations)));
    }
    else
    {
        pageDictionary.setEntry(PDFInplaceOrMemoryString("Annots"), PDFObject::createArray(std::move(sortedAnnotations)));
    }
    builder->setObject(page, PDFObject::createDictionary(std::move(pageDictionary)));
}

QString PDFFireFormFields::createNameFromLabel(QString label)
{
    label.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N} \\-]")), QStringLiteral(" "));
    label = label.simplified();
    if (label.size() > 40)
    {
        label = label.left(40).trimmed();
    }
    return label.isEmpty() ? QStringLiteral("Field") : label;
}

std::vector<PDFFireFormFields::DetectedField> PDFFireFormFields::detectFields(const std::vector<TextRun>& runs,
                                                                              const std::vector<QRectF>& horizontalLines,
                                                                              const std::vector<QRectF>& squares,
                                                                              const std::vector<QRectF>& existingWidgets)
{
    std::vector<DetectedField> fields;

    auto overlaps = [](const QRectF& a, const QRectF& b)
    {
        const QRectF intersection = a.intersected(b);
        if (intersection.isEmpty())
        {
            return false;
        }
        const PDFReal smallerArea = qMin(a.width() * a.height(), b.width() * b.height());
        return smallerArea <= 0.0 || intersection.width() * intersection.height() > smallerArea * 0.3;
    };

    auto addField = [&](DetectedField field)
    {
        for (const QRectF& widget : existingWidgets)
        {
            if (overlaps(widget, field.rect))
            {
                return;
            }
        }
        for (const DetectedField& existingField : fields)
        {
            if (overlaps(existingField.rect, field.rect))
            {
                return;
            }
        }

        // A signature or a date is recognized by its label
        static const QRegularExpression signatureExpression(QStringLiteral("\\bsign(ature|ed|ed by)?\\b"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression dateExpression(QStringLiteral("\\bdate\\b"), QRegularExpression::CaseInsensitiveOption);
        if (field.type == Type::Text)
        {
            if (signatureExpression.match(field.label).hasMatch())
            {
                field.type = Type::Signature;
            }
            else if (dateExpression.match(field.label).hasMatch())
            {
                field.type = Type::Date;
            }
        }
        fields.push_back(std::move(field));
    };

    // The label of a place: the text before it on the line (the last part of
    // the text, separated by a wide gap or by another place)
    auto labelBefore = [](const TextRun& run, qsizetype end) -> QString
    {
        // PDF Fire: the label starts after a check box symbol (☐ Yes ☐ No  Received by:),
        // after another blank (Name: ____ Date: ____) or after a wide gap - the words of
        // the check boxes or of the previous field are not a part of it
        static const QString separators = QStringLiteral("☐□▢◻◽❏❐❑❒⬜_");
        QString label = run.text.left(end);
        while (!label.isEmpty() && (label.back() == QChar('_') || label.back().isSpace()))
        {
            label.chop(1);
        }
        qsizetype start = 0;
        for (qsizetype k = label.size() - 1; k >= 0; --k)
        {
            if (separators.contains(label[k]))
            {
                start = k + 1;
                break;
            }
        }
        // A wide gap between the words separates the label from the text before
        // it (the spaces of the text don't tell it - they are put back by the gaps)
        const PDFReal fontSize = run.fontSize > 0.0 ? run.fontSize : 10.0;
        PDFReal previousLeft = std::numeric_limits<PDFReal>::max();
        for (qsizetype k = label.size() - 1; k >= start && size_t(k) < run.characters.size(); --k)
        {
            if (label[k].isSpace())
            {
                continue;
            }
            if (run.characters[size_t(k)].right() < previousLeft - fontSize * 0.9 && previousLeft != std::numeric_limits<PDFReal>::max())
            {
                start = k + 1;
                break;
            }
            previousLeft = run.characters[size_t(k)].left();
        }
        label = label.mid(start).trimmed();
        const qsizetype gap = label.lastIndexOf(QStringLiteral("   "));
        if (gap >= 0)
        {
            label = label.mid(gap).trimmed();
        }
        while (label.endsWith(QChar(':')))
        {
            label.chop(1);
        }

        // A long label - its last words (not cut inside a word)
        if (label.size() > 40)
        {
            label = label.right(40);
            const qsizetype space = label.indexOf(QChar(' '));
            if (space > 0 && space < label.size() - 1)
            {
                label = label.mid(space + 1);
            }
        }
        return label.trimmed();
    };

    // PDF Fire: the extent of the text - a drawn line without a label, as wide as the
    // text, is a rule separating the parts of the page (under a letterhead, above a
    // footer), not a place to write
    PDFReal textLeft = std::numeric_limits<PDFReal>::max();
    PDFReal textRight = std::numeric_limits<PDFReal>::lowest();
    for (const TextRun& run : runs)
    {
        for (size_t k = 0; k < run.characters.size() && k < size_t(run.text.size()); ++k)
        {
            if (!run.text[qsizetype(k)].isSpace())
            {
                textLeft = qMin(textLeft, run.characters[k].left());
                textRight = qMax(textRight, run.characters[k].right());
            }
        }
    }
    const PDFReal textWidth = textRight > textLeft ? textRight - textLeft : 0.0;

    // 1. Runs of underscores ("Name: ________")
    for (const TextRun& run : runs)
    {
        if (run.characters.size() != size_t(run.text.size()))
        {
            continue;
        }

        qsizetype i = 0;
        while (i < run.text.size())
        {
            if (run.text[i] != QChar('_'))
            {
                ++i;
                continue;
            }

            qsizetype j = i;
            while (j < run.text.size() && run.text[j] == QChar('_'))
            {
                ++j;
            }

            if (j - i >= 3)
            {
                const PDFReal left = run.characters[i].left();
                const PDFReal right = run.characters[j - 1].right();
                const PDFReal fontSize = run.fontSize > 0.0 ? run.fontSize : 10.0;

                DetectedField field;
                field.rect = QRectF(QPointF(left, run.baseline - fontSize * 0.25), QPointF(right, run.baseline + fontSize * 1.1)).normalized();
                field.label = labelBefore(run, i);
                addField(std::move(field));
            }
            i = j;
        }
    }

    // 2. Empty check box glyphs ("☐ Yes")
    static const QString boxCharacters = QStringLiteral("☐□▢◻◽❏❐❑❒⬜");
    for (const TextRun& run : runs)
    {
        if (run.characters.size() != size_t(run.text.size()))
        {
            continue;
        }

        for (qsizetype i = 0; i < run.text.size(); ++i)
        {
            if (boxCharacters.contains(run.text[i]))
            {
                const QRectF box = run.characters[i];
                const PDFReal side = qMax(box.width(), box.height());

                DetectedField field;
                field.type = Type::CheckBox;
                field.rect = QRectF(box.center() - QPointF(side * 0.5, side * 0.5), QSizeF(side, side));
                // PDF Fire: the label is the text after the box, until the next box, a
                // colon, a blank or a wide gap ("☐ Partial    Received by/date:")
                const PDFReal fontSize = run.fontSize > 0.0 ? run.fontSize : 10.0;
                PDFReal lastRight = box.right();
                for (qsizetype k = i + 1; k < run.text.size(); ++k)
                {
                    const QChar character = run.text[k];
                    if (boxCharacters.contains(character) || character == QChar(':') || character == QChar('_'))
                    {
                        break;
                    }
                    if (!character.isSpace())
                    {
                        if (!field.label.isEmpty() && run.characters[size_t(k)].left() - lastRight > fontSize * 0.9)
                        {
                            break;
                        }
                        lastRight = run.characters[size_t(k)].right();
                    }
                    field.label += character;
                }
                field.label = field.label.simplified();
                addField(std::move(field));
            }
        }
    }

    // The runs sorted by their baselines - only the runs near a line or a square are
    // examined (a drawing can have thousands of lines and characters)
    std::vector<const TextRun*> runsByBaseline;
    for (const TextRun& run : runs)
    {
        runsByBaseline.push_back(&run);
    }
    std::sort(runsByBaseline.begin(), runsByBaseline.end(), [](const TextRun* left, const TextRun* right) { return left->baseline < right->baseline; });
    auto getRunsNear = [&runsByBaseline](PDFReal low, PDFReal high)
    {
        auto it = std::lower_bound(runsByBaseline.cbegin(), runsByBaseline.cend(), low, [](const TextRun* run, PDFReal value) { return run->baseline < value; });
        std::vector<const TextRun*> result;
        for (; it != runsByBaseline.cend() && (*it)->baseline <= high; ++it)
        {
            result.push_back(*it);
        }
        return result;
    };

    // 3. Drawn squares (check boxes) - the label is the text after the square
    for (const QRectF& square : squares)
    {
        DetectedField field;
        field.type = Type::CheckBox;
        field.rect = square;

        for (const TextRun* runPointer : getRunsNear(square.center().y() - square.height(), square.center().y() + square.height()))
        {
            const TextRun& run = *runPointer;
            if (run.characters.empty() || qAbs(run.baseline - square.center().y()) > square.height())
            {
                continue;
            }
            for (size_t k = 0; k < run.characters.size(); ++k)
            {
                if (run.characters[k].left() >= square.right() - 1.0 && run.characters[k].left() <= square.right() + 20.0)
                {
                    field.label = run.text.mid(qsizetype(k)).section(QStringLiteral("  "), 0, 0).trimmed();
                    break;
                }
            }
            if (!field.label.isEmpty())
            {
                break;
            }
        }
        addField(std::move(field));
    }

    // 4. Drawn horizontal lines ("Name: ____" drawn as a line). A line under
    // a text is an underline (or a border of a filled table cell) - not a field.
    for (const QRectF& line : horizontalLines)
    {
        if (line.width() < 36.0)
        {
            continue;
        }

        const PDFReal lineY = line.center().y();
        bool isUnderline = false;
        QString label;
        PDFReal labelDistance = std::numeric_limits<PDFReal>::infinity();
        PDFReal fontSize = 10.0;

        for (const TextRun* runPointer : getRunsNear(lineY - 3.0, lineY + 48.0))
        {
            const TextRun& run = *runPointer;
            if (run.characters.size() != size_t(run.text.size()))
            {
                continue;
            }

            for (qsizetype k = 0; k < run.text.size(); ++k)
            {
                const QRectF& character = run.characters[k];
                if (run.text[k].isSpace() || run.text[k] == QChar('_'))
                {
                    continue;
                }

                // A character above the line, within its width: the line underlines a text
                if (character.right() > line.left() + 1.0 && character.left() < line.right() - 1.0 &&
                    run.baseline >= lineY - 2.0 && run.baseline <= lineY + run.fontSize * 1.2)
                {
                    isUnderline = true;
                    break;
                }
            }

            // The label: the text on the same level, ending before the line
            if (!isUnderline && run.baseline >= lineY - 3.0 && run.baseline <= lineY + qMax(run.fontSize, 6.0))
            {
                qsizetype end = -1;
                for (qsizetype k = 0; k < run.text.size(); ++k)
                {
                    if (run.characters[k].right() <= line.left() + 2.0 && !run.text[k].isSpace())
                    {
                        end = k + 1;
                    }
                }
                if (end > 0)
                {
                    const PDFReal distance = line.left() - run.characters[end - 1].right();
                    if (distance < labelDistance && distance < 150.0)
                    {
                        labelDistance = distance;
                        label = labelBefore(run, end);
                        fontSize = run.fontSize > 0.0 ? run.fontSize : fontSize;
                    }
                }
            }

            if (isUnderline)
            {
                break;
            }
        }

        if (isUnderline)
        {
            continue;
        }

        // PDF Fire: a rule - no label, nearly as wide as the text, and text right
        // below it (a heading after a letterhead, a footer)
        if (label.isEmpty() && textWidth > 0.0 && line.width() >= textWidth * 0.9)
        {
            bool hasTextBelow = false;
            for (const TextRun* runPointer : getRunsNear(lineY - 48.0, lineY - 0.5))
            {
                const TextRun& run = *runPointer;
                if (run.baseline < lineY - 0.5 && run.baseline >= lineY - qMax(run.fontSize, 6.0) * 2.2 && !run.text.trimmed().isEmpty() &&
                    !run.text.trimmed().startsWith(QChar('_')))
                {
                    hasTextBelow = true;
                    break;
                }
            }
            if (hasTextBelow)
            {
                continue;
            }
        }

        DetectedField field;
        field.rect = QRectF(QPointF(line.left(), lineY + 0.5), QPointF(line.right(), lineY + qBound(12.0, fontSize * 1.5, 24.0))).normalized();
        field.label = label;
        addField(std::move(field));
    }

    // PDF Fire: a line without a label, as wide as the text, is a place to write only
    // under a question or a prompt ("Describe what happened:") - a line under the
    // letterhead or above the footer is a rule. The nearest text above it (other than
    // other such lines) must end with a colon or a question mark.
    auto isAnswerLine = [&runs](const QRectF& rect)
    {
        const PDFReal bottom = rect.top();
        const TextRun* nearest = nullptr;
        for (const TextRun& run : runs)
        {
            const QString text = run.text.trimmed();
            if (text.isEmpty() || text.count(QChar('_')) * 2 > text.size() || run.baseline <= bottom + 1.0 || run.baseline > bottom + 72.0)
            {
                continue;
            }
            if (!nearest || run.baseline < nearest->baseline)
            {
                nearest = &run;
            }
        }
        if (!nearest)
        {
            return false;
        }
        QString text = nearest->text.trimmed();
        while (text.endsWith(QChar('_')) || text.endsWith(QChar(' ')))
        {
            text.chop(1);
        }
        return text.endsWith(QChar(':')) || text.endsWith(QChar('?')) || nearest->text.contains(QChar('_'));
    };
    fields.erase(std::remove_if(fields.begin(), fields.end(), [&](const DetectedField& field)
    {
        return field.type == Type::Text && field.label.isEmpty() && textWidth > 0.0 && field.rect.width() >= textWidth * 0.8 && !isAnswerLine(field.rect);
    }), fields.end());

    return fields;
}

PDFFireFormFieldScanner::Result PDFFireFormFieldScanner::scan()
{
    processContents();
    const PDFTextLayout textLayout = createTextLayout();

    Result result;
    result.horizontalLines = std::move(m_horizontalLines);
    result.squares = std::move(m_squares);

    for (const PDFTextBlock& block : textLayout.getTextBlocks())
    {
        for (const PDFTextLine& line : block.getLines())
        {
            const TextCharacters& characters = line.getCharacters();
            if (characters.empty())
            {
                continue;
            }

            PDFFireFormFields::TextRun run;
            run.fontSize = characters.front().fontSize;
            run.baseline = characters.front().position.y();

            // The spaces are not a part of the text layout - they are put back by
            // the gaps between the characters (a wide gap separates columns)
            for (size_t i = 0; i < characters.size(); ++i)
            {
                const TextCharacter& character = characters[i];
                const QRectF rect = character.boundingBox.boundingRect();
                if (i > 0)
                {
                    const QRectF previous = run.characters.back();
                    const PDFReal gap = rect.left() - previous.right();
                    const PDFReal fontSize = qMax(character.fontSize, 1.0);
                    if (gap > fontSize * 0.2)
                    {
                        const int count = gap > fontSize * 1.5 ? 3 : 1;
                        for (int k = 0; k < count; ++k)
                        {
                            run.text += QChar(' ');
                            run.characters.push_back(QRectF(QPointF(previous.right(), rect.top()), QPointF(rect.left(), rect.bottom())).normalized());
                        }
                    }
                }
                run.text += character.character;
                run.characters.push_back(rect.normalized());
            }

            result.runs.push_back(std::move(run));
        }
    }

    return result;
}

bool PDFFireFormFieldScanner::isContentKindSuppressed(ContentKind kind) const
{
    // The shapes are needed (lines and boxes of the form)
    return kind == ContentKind::Shapes ? false : BaseClass::isContentKindSuppressed(kind);
}

void PDFFireFormFieldScanner::performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule)
{
    Q_UNUSED(fillRule);

    if (text || isContentSuppressed() || (!stroke && !fill))
    {
        return;
    }

    // The path is in the coordinates of the content stream - it is mapped to the
    // coordinates of the page (the text is there already)
    const QPainterPath mappedPath = getCurrentWorldMatrix().map(path);
    for (const QPolygonF& polygon : mappedPath.toSubpathPolygons())
    {
        // Limits for drawings with many paths (maps, plans) - they are not forms
        if (m_horizontalLines.size() >= 5000 || m_squares.size() >= 5000)
        {
            break;
        }

        const QRectF rect = polygon.boundingRect();

        // A thin horizontal line (stroked, or a thin filled rectangle)
        if (rect.height() <= 3.0 && rect.width() >= 36.0 && rect.width() <= 2000.0)
        {
            m_horizontalLines.push_back(rect);
        }
        // A small empty square (a check box drawn on the page)
        else if (stroke && rect.width() >= 5.0 && rect.width() <= 24.0 && qAbs(rect.width() - rect.height()) <= 2.0)
        {
            m_squares.push_back(rect);
        }
    }
}

}   // namespace pdf
