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

#include "pagemarks.h"

#include "pdfdocumentbuilder.h"
#include "pdfencoding.h"
#include "pdfexception.h"
#include "pdfparser.h"
#include "pdfstreamfilters.h"
#include "pdfutils.h"

#include <QImage>
#include <QRegularExpression>
#include <QTransform>
#include <QtMath>

#include <algorithm>
#include <map>
#include <set>

namespace pdfplugin
{

using namespace pdf;

namespace
{

/// Private data of our marks (PieceInfo of the form), Acrobat's private data, and the
/// key of the content streams written by us (so an emptied stream can be dropped)
constexpr const char* PRIVATE_KEY = "PDFFire_PageMarks";
constexpr const char* ACROBAT_KEY = "ADBE_CompoundType";
constexpr const char* STREAM_KEY = "PDFFire_Mark";
constexpr const char* NAME_PREFIX = "PFMark";

// Widths (1/1000 em) of the WinAnsi codes 32..255 of the standard fonts. The values
// are those of the Adobe Core 14 font metrics (read from the metric-compatible URW
// base35 metrics); the codes WinAnsi does not define have the width 0 and are never
// written - such a character becomes '?'. Courier is 600 everywhere.
constexpr std::array<quint16, 224> WIDTHS_HELVETICA = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584, 350,
    556, 0, 222, 556, 333, 1000, 556, 556, 333, 1000, 667, 333, 1000, 0, 611, 0,
    0, 222, 222, 333, 333, 350, 556, 1000, 333, 1000, 500, 333, 944, 0, 500, 667,
    278, 333, 556, 556, 556, 556, 260, 556, 333, 737, 370, 556, 584, 333, 737, 333,
    400, 584, 333, 333, 333, 556, 537, 278, 333, 333, 365, 556, 834, 834, 834, 611,
    667, 667, 667, 667, 667, 667, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
    722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
    556, 556, 556, 556, 556, 556, 889, 500, 556, 556, 556, 556, 278, 278, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 584, 611, 556, 556, 556, 556, 500, 556, 500,
};
constexpr std::array<quint16, 224> WIDTHS_HELVETICA_BOLD = {
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611,
    975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556,
    333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
    611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584, 350,
    556, 0, 278, 556, 500, 1000, 556, 556, 333, 1000, 667, 333, 1000, 0, 611, 0,
    0, 278, 278, 500, 500, 350, 556, 1000, 333, 1000, 556, 333, 944, 0, 500, 667,
    278, 333, 556, 556, 556, 556, 280, 556, 333, 737, 370, 556, 584, 333, 737, 333,
    400, 584, 333, 333, 333, 611, 556, 278, 333, 333, 365, 556, 834, 834, 834, 611,
    722, 722, 722, 722, 722, 722, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
    722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
    556, 556, 556, 556, 556, 556, 889, 556, 556, 556, 556, 556, 278, 278, 278, 278,
    611, 611, 611, 611, 611, 611, 611, 584, 611, 611, 611, 611, 611, 556, 611, 556,
};
constexpr std::array<quint16, 224> WIDTHS_HELVETICA_OBLIQUE = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584, 350,
    556, 0, 222, 556, 333, 1000, 556, 556, 333, 1000, 667, 333, 1000, 0, 611, 0,
    0, 222, 222, 333, 333, 350, 556, 1000, 333, 1000, 500, 333, 944, 0, 500, 667,
    278, 333, 556, 556, 556, 556, 260, 556, 333, 737, 370, 556, 584, 333, 737, 333,
    400, 584, 333, 333, 333, 556, 537, 278, 333, 333, 365, 556, 834, 834, 834, 611,
    667, 667, 667, 667, 667, 667, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
    722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
    556, 556, 556, 556, 556, 556, 889, 500, 556, 556, 556, 556, 278, 278, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 584, 611, 556, 556, 556, 556, 500, 556, 500,
};
constexpr std::array<quint16, 224> WIDTHS_HELVETICA_BOLDOBLIQUE = {
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611,
    975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556,
    333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
    611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584, 350,
    556, 0, 278, 556, 500, 1000, 556, 556, 333, 1000, 667, 333, 1000, 0, 611, 0,
    0, 278, 278, 500, 500, 350, 556, 1000, 333, 1000, 556, 333, 944, 0, 500, 667,
    278, 333, 556, 556, 556, 556, 280, 556, 333, 737, 370, 556, 584, 333, 737, 333,
    400, 584, 333, 333, 333, 611, 556, 278, 333, 333, 365, 556, 834, 834, 834, 611,
    722, 722, 722, 722, 722, 722, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
    722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
    556, 556, 556, 556, 556, 556, 889, 556, 556, 556, 556, 556, 278, 278, 278, 278,
    611, 611, 611, 611, 611, 611, 611, 584, 611, 611, 611, 611, 611, 556, 611, 556,
};
constexpr std::array<quint16, 224> WIDTHS_TIMES_ROMAN = {
    250, 333, 408, 500, 500, 833, 778, 180, 333, 333, 500, 564, 250, 333, 250, 278,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 278, 278, 564, 564, 564, 444,
    921, 722, 667, 667, 722, 611, 556, 722, 722, 333, 389, 722, 611, 889, 722, 722,
    556, 722, 667, 556, 611, 722, 722, 944, 722, 722, 611, 333, 278, 333, 469, 500,
    333, 444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500, 278, 778, 500, 500,
    500, 500, 333, 389, 278, 500, 500, 722, 500, 500, 444, 480, 200, 480, 541, 350,
    500, 0, 333, 500, 444, 1000, 500, 500, 333, 1000, 556, 333, 889, 0, 611, 0,
    0, 333, 333, 444, 444, 350, 500, 1000, 333, 980, 389, 333, 722, 0, 444, 722,
    250, 333, 500, 500, 500, 500, 200, 500, 333, 760, 276, 500, 564, 333, 760, 333,
    400, 564, 300, 300, 333, 500, 453, 250, 333, 300, 310, 500, 750, 750, 750, 444,
    722, 722, 722, 722, 722, 722, 889, 667, 611, 611, 611, 611, 333, 333, 333, 333,
    722, 722, 722, 722, 722, 722, 722, 564, 722, 722, 722, 722, 722, 722, 556, 500,
    444, 444, 444, 444, 444, 444, 667, 444, 444, 444, 444, 444, 278, 278, 278, 278,
    500, 500, 500, 500, 500, 500, 500, 564, 500, 500, 500, 500, 500, 500, 500, 500,
};
constexpr std::array<quint16, 224> WIDTHS_TIMES_BOLD = {
    250, 333, 555, 500, 500, 1000, 833, 278, 333, 333, 500, 570, 250, 333, 250, 278,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333, 570, 570, 570, 500,
    930, 722, 667, 722, 722, 667, 611, 778, 778, 389, 500, 778, 667, 944, 722, 778,
    611, 778, 722, 556, 667, 722, 722, 1000, 722, 722, 667, 333, 278, 333, 581, 500,
    333, 500, 556, 444, 556, 444, 333, 500, 556, 278, 333, 556, 278, 833, 556, 500,
    556, 556, 444, 389, 333, 556, 500, 722, 500, 500, 444, 394, 220, 394, 520, 350,
    500, 0, 333, 500, 500, 1000, 500, 500, 333, 1000, 556, 333, 1000, 0, 667, 0,
    0, 333, 333, 500, 500, 350, 500, 1000, 333, 1000, 389, 333, 722, 0, 444, 722,
    250, 333, 500, 500, 500, 500, 220, 500, 333, 747, 300, 500, 570, 333, 747, 333,
    400, 570, 300, 300, 333, 556, 540, 250, 333, 300, 330, 500, 750, 750, 750, 500,
    722, 722, 722, 722, 722, 722, 1000, 722, 667, 667, 667, 667, 389, 389, 389, 389,
    722, 722, 778, 778, 778, 778, 778, 570, 778, 722, 722, 722, 722, 722, 611, 556,
    500, 500, 500, 500, 500, 500, 722, 444, 444, 444, 444, 444, 278, 278, 278, 278,
    500, 556, 500, 500, 500, 500, 500, 570, 500, 556, 556, 556, 556, 500, 556, 500,
};
constexpr std::array<quint16, 224> WIDTHS_TIMES_ITALIC = {
    250, 333, 420, 500, 500, 833, 778, 214, 333, 333, 500, 675, 250, 333, 250, 278,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333, 675, 675, 675, 500,
    920, 611, 611, 667, 722, 611, 611, 722, 722, 333, 444, 667, 556, 833, 667, 722,
    611, 722, 611, 500, 556, 722, 611, 833, 611, 556, 556, 389, 278, 389, 422, 500,
    333, 500, 500, 444, 500, 444, 278, 500, 500, 278, 278, 444, 278, 722, 500, 500,
    500, 500, 389, 389, 278, 500, 444, 667, 444, 444, 389, 400, 275, 400, 541, 350,
    500, 0, 333, 500, 556, 889, 500, 500, 333, 1000, 500, 333, 944, 0, 556, 0,
    0, 333, 333, 556, 556, 350, 500, 889, 333, 980, 389, 333, 667, 0, 389, 556,
    250, 389, 500, 500, 500, 500, 275, 500, 333, 760, 276, 500, 675, 333, 760, 333,
    400, 675, 300, 300, 333, 500, 523, 250, 333, 300, 310, 500, 750, 750, 750, 500,
    611, 611, 611, 611, 611, 611, 889, 667, 611, 611, 611, 611, 333, 333, 333, 333,
    722, 667, 722, 722, 722, 722, 722, 675, 722, 722, 722, 722, 722, 556, 611, 500,
    500, 500, 500, 500, 500, 500, 667, 444, 444, 444, 444, 444, 278, 278, 278, 278,
    500, 500, 500, 500, 500, 500, 500, 675, 500, 500, 500, 500, 500, 444, 500, 444,
};
constexpr std::array<quint16, 224> WIDTHS_TIMES_BOLDITALIC = {
    250, 389, 555, 500, 500, 833, 778, 278, 333, 333, 500, 570, 250, 333, 250, 278,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333, 570, 570, 570, 500,
    832, 667, 667, 667, 722, 667, 667, 722, 778, 389, 500, 667, 611, 889, 722, 722,
    611, 722, 667, 556, 611, 722, 667, 889, 667, 611, 611, 333, 278, 333, 570, 500,
    333, 500, 500, 444, 500, 444, 333, 500, 556, 278, 278, 500, 278, 778, 556, 500,
    500, 500, 389, 389, 278, 556, 444, 667, 500, 444, 389, 348, 220, 348, 570, 350,
    500, 0, 333, 500, 500, 1000, 500, 500, 333, 1000, 556, 333, 944, 0, 611, 0,
    0, 333, 333, 500, 500, 350, 500, 1000, 333, 1000, 389, 333, 722, 0, 389, 611,
    250, 389, 500, 500, 500, 500, 220, 500, 333, 747, 266, 500, 606, 333, 747, 333,
    400, 570, 300, 300, 333, 576, 500, 250, 333, 300, 300, 500, 750, 750, 750, 500,
    667, 667, 667, 667, 667, 667, 944, 667, 667, 667, 667, 667, 389, 389, 389, 389,
    722, 722, 722, 722, 722, 722, 722, 570, 722, 722, 722, 722, 722, 611, 611, 500,
    500, 500, 500, 500, 500, 500, 722, 444, 444, 444, 444, 444, 278, 278, 278, 278,
    500, 556, 500, 500, 500, 500, 500, 570, 500, 556, 556, 556, 556, 444, 500, 444,
};

/// Unicode of the WinAnsi codes 128..159 (0 = not defined)
constexpr char16_t WINANSI_128_159[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
};

const std::array<quint16, 224>* getWidthTable(PageMarks::Font font)
{
    switch (font)
    {
        case PageMarks::Font::Helvetica: return &WIDTHS_HELVETICA;
        case PageMarks::Font::HelveticaBold: return &WIDTHS_HELVETICA_BOLD;
        case PageMarks::Font::HelveticaOblique: return &WIDTHS_HELVETICA_OBLIQUE;
        case PageMarks::Font::HelveticaBoldOblique: return &WIDTHS_HELVETICA_BOLDOBLIQUE;
        case PageMarks::Font::TimesRoman: return &WIDTHS_TIMES_ROMAN;
        case PageMarks::Font::TimesBold: return &WIDTHS_TIMES_BOLD;
        case PageMarks::Font::TimesItalic: return &WIDTHS_TIMES_ITALIC;
        case PageMarks::Font::TimesBoldItalic: return &WIDTHS_TIMES_BOLDITALIC;
        default: return nullptr;
    }
}

/// Cap height and descent (of 1 em) of the font family - the text is placed by its
/// capitals (a header is at its margin by the top of its capitals, a watermark is
/// centered by them), and the descent keeps the descenders inside a footer margin
struct FontMetrics
{
    double capHeight;
    double descent;
};

FontMetrics getFontMetrics(PageMarks::Font font)
{
    if (font >= PageMarks::Font::Courier)
    {
        return { 0.562, 0.157 };
    }
    if (font >= PageMarks::Font::TimesRoman)
    {
        return { 0.662, 0.217 };
    }
    return { 0.718, 0.207 };
}

QByteArray formatNumber(double value)
{
    if (!qIsFinite(value))
    {
        return QByteArray("0");
    }

    QByteArray text = QByteArray::number(value, 'f', 4);
    while (text.contains('.') && (text.endsWith('0') || text.endsWith('.')))
    {
        text.chop(1);
    }
    return (text.isEmpty() || text == "-0") ? QByteArray("0") : text;
}

QByteArray formatMatrix(const QTransform& matrix)
{
    return formatNumber(matrix.m11()) + " " + formatNumber(matrix.m12()) + " " + formatNumber(matrix.m21()) + " " +
           formatNumber(matrix.m22()) + " " + formatNumber(matrix.dx()) + " " + formatNumber(matrix.dy());
}

/// A literal string of the content stream; the parentheses and the backslash are
/// escaped, the other bytes are written as they are (the encoding has no controls)
QByteArray toLiteralString(const QByteArray& encoded)
{
    QByteArray result = "(";
    for (char byte : encoded)
    {
        if (byte == '(' || byte == ')' || byte == '\\')
        {
            result.append('\\');
        }
        result.append(byte);
    }
    result.append(')');
    return result;
}

QByteArray fillColor(const QColor& color)
{
    const QColor rgb = color.toRgb();
    return formatNumber(rgb.redF()) + " " + formatNumber(rgb.greenF()) + " " + formatNumber(rgb.blueF()) + " rg\n";
}

PDFObject createRealArray(std::initializer_list<double> values)
{
    PDFArrayBuilder array;
    for (double value : values)
    {
        array.appendItem(PDFObject::createReal(value));
    }
    return PDFObject::createArray(std::move(array));
}

PDFObject createNameArray(std::initializer_list<const char*> names)
{
    PDFArrayBuilder array;
    for (const char* name : names)
    {
        array.appendItem(PDFObject::createName(QByteArray(name)));
    }
    return PDFObject::createArray(std::move(array));
}

const PDFDictionary* getDictionary(const PDFObjectStorage* storage, const PDFObject& object)
{
    return storage->getDictionaryFromObject(object);
}

/// The entry of the page, or of its ancestor in the page tree (Resources are inherited)
PDFObject getInheritedEntry(const PDFObjectStorage* storage, PDFObjectReference page, const char* key)
{
    const PDFDictionary* dictionary = getDictionary(storage, storage->getObject(page));
    for (int depth = 0; dictionary && depth < 64; ++depth)
    {
        if (dictionary->hasKey(key))
        {
            return dictionary->get(key);
        }
        dictionary = getDictionary(storage, dictionary->get("Parent"));
    }
    return PDFObject();
}

/// Items of the page contents (a stream, or an array of streams, which can be indirect)
std::vector<PDFObject> getContentItems(const PDFObjectStorage* storage, const PDFDictionary* pageDictionary)
{
    std::vector<PDFObject> items;
    const PDFObject& contents = pageDictionary->get("Contents");
    const PDFObject& dereferenced = storage->getObject(contents);
    if (dereferenced.isArray())
    {
        const PDFArray* array = dereferenced.getArray();
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            items.push_back(array->getItem(i));
        }
    }
    else if (dereferenced.isStream())
    {
        items.push_back(contents);
    }
    return items;
}

QByteArray getKindName(int kind)
{
    switch (kind)
    {
        case PageMarks::HeaderFooter: return "HeaderFooter";
        case PageMarks::Bates: return "Bates";
        case PageMarks::Background: return "Background";
        default: return "Watermark";
    }
}

/// The kind of the mark drawn by the form XObject, or 0, if the form is not a mark
/// (ours or Acrobat's). Acrobat's marks are told by the subtype of the artifact.
int getFormKind(const PDFObjectStorage* storage, const PDFObject& formObject, const QByteArray& subtype)
{
    const PDFObject& form = storage->getObject(formObject);
    if (!form.isStream())
    {
        return 0;
    }

    const PDFDictionary* pieceInfo = getDictionary(storage, form.getStream()->getDictionary()->get("PieceInfo"));
    if (!pieceInfo)
    {
        return 0;
    }

    if (pieceInfo->hasKey(PRIVATE_KEY))
    {
        const PDFDictionary* data = getDictionary(storage, pieceInfo->get(PRIVATE_KEY));
        const PDFDictionary* privateData = data ? getDictionary(storage, data->get("Private")) : nullptr;
        const PDFObject& kind = privateData ? storage->getObject(privateData->get("Kind")) : PDFObject();
        if (kind.isName())
        {
            const QByteArray name = kind.getString();
            if (name == "HeaderFooter")
            {
                return PageMarks::HeaderFooter;
            }
            if (name == "Bates")
            {
                return PageMarks::Bates;
            }
            // PDF Fire: a background is a background artifact only (never a pagination one)
            if (name == "Background")
            {
                return subtype == "Background" ? int(PageMarks::Background) : 0;
            }
        }
        if (subtype == "Background")
        {
            return 0;
        }
        return subtype == "Watermark" ? int(PageMarks::Watermark) : int(PageMarks::HeaderFooter);
    }

    // PDF Fire: only the backgrounds added by PDF Fire are removed, not Acrobat's
    if (pieceInfo->hasKey(ACROBAT_KEY) && subtype != "Background")
    {
        return subtype == "Watermark" ? int(PageMarks::Watermark) : int(PageMarks::HeaderFooter);
    }

    return 0;
}

/// A mark found in a content stream: the bytes from the /Artifact operand to the end
/// of its EMC, the kind, and the name of the form XObject it draws
struct FoundMark
{
    qint64 begin = 0;
    qint64 end = 0;
    int kind = 0;
    QByteArray formName;
};

/// Finds the marks in the content stream. A mark is a pagination artifact (watermark,
/// header, footer) which draws a form XObject marked as a mark - a pagination artifact
/// drawing anything else (a header of a word processor) is not a mark.
std::vector<FoundMark> findMarks(const QByteArray& content, const PDFObjectStorage* storage, const PDFDictionary* xobjects)
{
    struct Operand
    {
        PDFLexicalAnalyzer::Token token;
        qint64 position;
    };

    struct Level
    {
        bool candidate = false;
        qint64 begin = 0;
        QByteArray subtype;
        int kind = 0;
        QByteArray formName;
    };

    std::vector<FoundMark> marks;
    std::vector<Operand> operands;
    std::vector<Level> levels;

    try
    {
        PDFLexicalAnalyzer lexer(content.constData(), content.constData() + content.size());
        while (true)
        {
            lexer.skipWhitespaceAndComments();
            const qint64 position = lexer.pos();
            PDFLexicalAnalyzer::Token token = lexer.fetch();
            if (token.type == PDFLexicalAnalyzer::TokenType::EndOfFile)
            {
                break;
            }

            if (token.type != PDFLexicalAnalyzer::TokenType::Command)
            {
                operands.push_back({ std::move(token), position });
                continue;
            }

            const QByteArray command = token.data.toByteArray();
            if (command == "BDC")
            {
                Level level;
                level.begin = operands.empty() ? position : operands.front().position;

                if (operands.size() >= 2 &&
                    operands[0].token.type == PDFLexicalAnalyzer::TokenType::Name && operands[0].token.data.toByteArray() == "Artifact" &&
                    operands[1].token.type == PDFLexicalAnalyzer::TokenType::DictionaryStart)
                {
                    // The entries of the property list; a value, which is an array or
                    // a dictionary (/Attached [/Top]), is skipped
                    QByteArray type;
                    bool expectKey = true;
                    QByteArray key;
                    for (size_t i = 2; i < operands.size(); ++i)
                    {
                        const PDFLexicalAnalyzer::Token& item = operands[i].token;
                        if (expectKey)
                        {
                            if (item.type != PDFLexicalAnalyzer::TokenType::Name)
                            {
                                break;
                            }
                            key = item.data.toByteArray();
                            expectKey = false;
                            continue;
                        }

                        if (item.type == PDFLexicalAnalyzer::TokenType::ArrayStart || item.type == PDFLexicalAnalyzer::TokenType::DictionaryStart)
                        {
                            int depth = 1;
                            while (depth > 0 && ++i < operands.size())
                            {
                                const PDFLexicalAnalyzer::TokenType nestedType = operands[i].token.type;
                                if (nestedType == PDFLexicalAnalyzer::TokenType::ArrayStart || nestedType == PDFLexicalAnalyzer::TokenType::DictionaryStart)
                                {
                                    ++depth;
                                }
                                else if (nestedType == PDFLexicalAnalyzer::TokenType::ArrayEnd || nestedType == PDFLexicalAnalyzer::TokenType::DictionaryEnd)
                                {
                                    --depth;
                                }
                            }
                        }
                        else if (item.type == PDFLexicalAnalyzer::TokenType::Name)
                        {
                            if (key == "Type")
                            {
                                type = item.data.toByteArray();
                            }
                            else if (key == "Subtype")
                            {
                                level.subtype = item.data.toByteArray();
                            }
                        }
                        expectKey = true;
                    }

                    level.candidate = type == "Pagination" &&
                                      (level.subtype == "Watermark" || level.subtype == "Header" || level.subtype == "Footer");

                    // PDF Fire: a background artifact (it has no subtype) can be our background
                    if (type == "Background")
                    {
                        level.subtype = "Background";
                        level.candidate = true;
                    }
                }
                levels.push_back(std::move(level));
            }
            else if (command == "BMC")
            {
                Level level;
                level.begin = position;
                levels.push_back(std::move(level));
            }
            else if (command == "EMC")
            {
                if (!levels.empty())
                {
                    Level level = std::move(levels.back());
                    levels.pop_back();

                    // A mark inside a mark is a part of the outer one
                    const bool insideMark = std::any_of(levels.cbegin(), levels.cend(), [](const Level& outer) { return outer.candidate && outer.kind; });
                    if (level.candidate && level.kind && !insideMark)
                    {
                        marks.push_back({ level.begin, lexer.pos(), level.kind, level.formName });
                    }
                }
            }
            else if (command == "Do")
            {
                if (operands.size() == 1 && operands[0].token.type == PDFLexicalAnalyzer::TokenType::Name && xobjects)
                {
                    for (auto it = levels.rbegin(); it != levels.rend(); ++it)
                    {
                        if (it->candidate)
                        {
                            const QByteArray name = operands[0].token.data.toByteArray();
                            if (const int kind = getFormKind(storage, xobjects->get(name), it->subtype))
                            {
                                it->kind = kind;
                                it->formName = name;
                            }
                            break;
                        }
                    }
                }
            }
            else if (command == "ID")
            {
                // The data of an inline image is binary - it is skipped up to its "EI"
                // (white space, EI, white space or the end)
                qint64 index = lexer.pos() + 1;
                while (index + 1 < content.size())
                {
                    if (content[index] == 'E' && content[index + 1] == 'I' &&
                        PDFLexicalAnalyzer::isWhitespace(content[index - 1]) &&
                        (index + 2 == content.size() || PDFLexicalAnalyzer::isWhitespace(content[index + 2])))
                    {
                        break;
                    }
                    ++index;
                }
                lexer.seek(std::min<qint64>(index + 2, content.size()));
            }

            operands.clear();
        }
    }
    catch (const PDFException&)
    {
        // A damaged content stream: the marks found before the damage are valid
    }

    return marks;
}

/// Writes the marks of one command into the document: the fonts and the image are
/// shared by all pages, a form with the same content (a watermark on pages of the
/// same size) is written once
class MarkWriter
{
public:
    explicit MarkWriter(PDFDocumentBuilder* builder, int kind) :
        m_builder(builder),
        m_kind(kind),
        m_lastModified(PDFEncoding::convertDateTimeToString(QDateTime::currentDateTime()))
    {
    }

    struct Placement
    {
        PDFObjectReference form;
        QByteArray subtype;     ///< Watermark, Header, Footer (or Background)
        bool behind = false;
        QByteArray properties;  ///< PDF Fire: the property list of the artifact, when it is not a pagination artifact
    };

    QByteArray getFontResourceName(PageMarks::Font font)
    {
        m_usedFonts.insert(font);
        return "F" + QByteArray::number(int(font));
    }

    /// The form of the mark (content in the space of the page as it is shown)
    PDFObjectReference createForm(const QByteArray& content, const PageMarks::PageGeometry& geometry,
                                  std::optional<double> opacity, PDFObjectReference image, const QByteArray& imageName = "Im0")
    {
        QByteArray key = content;
        key += '|' + formatNumber(geometry.width) + ' ' + formatNumber(geometry.height);
        for (double value : geometry.matrix)
        {
            key += ' ' + formatNumber(value);
        }

        auto it = m_forms.find(key);
        if (it != m_forms.cend())
        {
            m_usedFonts.clear();
            return it->second;
        }

        PDFDictionaryBuilder resources;
        if (!m_usedFonts.empty())
        {
            PDFDictionaryBuilder fonts;
            for (PageMarks::Font font : m_usedFonts)
            {
                fonts.setEntry(PDFInplaceOrMemoryString("F" + QByteArray::number(int(font))), PDFObject::createReference(getFont(font)));
            }
            resources.setEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));
        }
        if (opacity)
        {
            PDFDictionaryBuilder graphicState;
            graphicState.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("ExtGState"));
            graphicState.setEntry(PDFInplaceOrMemoryString("ca"), PDFObject::createReal(*opacity));
            graphicState.setEntry(PDFInplaceOrMemoryString("CA"), PDFObject::createReal(*opacity));
            PDFDictionaryBuilder graphicStates;
            graphicStates.setEntry(PDFInplaceOrMemoryString("GS0"), PDFObject::createDictionary(std::move(graphicState)));
            resources.setEntry(PDFInplaceOrMemoryString("ExtGState"), PDFObject::createDictionary(std::move(graphicStates)));
        }
        if (image.isValid())
        {
            PDFDictionaryBuilder xobjects;
            xobjects.setEntry(PDFInplaceOrMemoryString(imageName), PDFObject::createReference(image));
            resources.setEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::move(xobjects)));
        }

        // The private data say, which command made the mark (Remove can remove the
        // watermarks without the Bates numbers); LastModified is required with PieceInfo
        PDFDictionaryBuilder privateData;
        privateData.setEntry(PDFInplaceOrMemoryString("Kind"), PDFObject::createName(getKindName(m_kind)));
        PDFDictionaryBuilder data;
        data.setEntry(PDFInplaceOrMemoryString("LastModified"), PDFObject::createString(m_lastModified));
        data.setEntry(PDFInplaceOrMemoryString("Private"), PDFObject::createDictionary(std::move(privateData)));
        PDFDictionaryBuilder pieceInfo;
        pieceInfo.setEntry(PDFInplaceOrMemoryString(PRIVATE_KEY), PDFObject::createDictionary(std::move(data)));

        const std::array<double, 6>& m = geometry.matrix;
        PDFDictionaryBuilder dictionary;
        dictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
        dictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
        dictionary.setEntry(PDFInplaceOrMemoryString("BBox"), createRealArray({ 0.0, 0.0, geometry.width, geometry.height }));
        dictionary.setEntry(PDFInplaceOrMemoryString("Matrix"), createRealArray({ m[0], m[1], m[2], m[3], m[4], m[5] }));
        dictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resources)));
        dictionary.setEntry(PDFInplaceOrMemoryString("PieceInfo"), PDFObject::createDictionary(std::move(pieceInfo)));
        dictionary.setEntry(PDFInplaceOrMemoryString("LastModified"), PDFObject::createString(m_lastModified));
        QByteArray streamContent = content;
        dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(streamContent.size()));

        const PDFObjectReference form = m_builder->addObject(PDFObject::createStream(PDFStream(std::move(dictionary), std::move(streamContent))));
        m_forms[key] = form;
        m_usedFonts.clear();
        return form;
    }

    /// Draws the forms on the page: behind = before the content of the page, in front
    /// = after it. The content of the page is enclosed in q/Q first, so a graphic
    /// state left by it (a transformation, a clip, a color) cannot move or hide a mark.
    void attach(PDFObjectReference pageReference, const std::vector<Placement>& placements)
    {
        const PDFObjectStorage* storage = m_builder->getStorage();
        const PDFDictionary* pageDictionary = getDictionary(storage, storage->getObject(pageReference));
        if (!pageDictionary || placements.empty())
        {
            return;
        }

        PDFDictionaryBuilder page(*pageDictionary);

        // The resources are copied into the page (they can be inherited from the page
        // tree, or shared with other pages) - only this page gets the new names
        const PDFDictionary* resourcesDictionary = getDictionary(storage, getInheritedEntry(storage, pageReference, "Resources"));
        PDFDictionaryBuilder resources = resourcesDictionary ? PDFDictionaryBuilder(*resourcesDictionary) : PDFDictionaryBuilder();
        const PDFDictionary* xobjectsDictionary = getDictionary(storage, resources.get("XObject"));
        PDFDictionaryBuilder xobjects = xobjectsDictionary ? PDFDictionaryBuilder(*xobjectsDictionary) : PDFDictionaryBuilder();

        std::vector<PDFObject> existing = getContentItems(storage, pageDictionary);
        for (PDFObject& item : existing)
        {
            if (!item.isReference())
            {
                // A direct stream (not allowed, but seen): it is made indirect to be an item of the array
                item = PDFObject::createReference(m_builder->addObject(item));
            }
        }

        std::vector<PDFObject> backgrounds;
        std::vector<PDFObject> behind;
        std::vector<PDFObject> front;
        int nameIndex = 0;
        for (const Placement& placement : placements)
        {
            QByteArray name;
            do
            {
                name = NAME_PREFIX + QByteArray::number(nameIndex++);
            } while (xobjects.hasKey(name));
            xobjects.setEntry(PDFInplaceOrMemoryString(name), PDFObject::createReference(placement.form));

            QByteArray properties = placement.properties;
            if (properties.isEmpty())
            {
                properties = "<</Type /Pagination /Subtype /" + placement.subtype;
                if (placement.subtype == "Header")
                {
                    properties += " /Attached [/Top]";
                }
                else if (placement.subtype == "Footer")
                {
                    properties += " /Attached [/Bottom]";
                }
                properties += ">>";
            }

            const QByteArray content = "\n/Artifact " + properties + " BDC\nq\n/" + name + " Do\nQ\nEMC\n";
            const PDFObject stream = PDFObject::createReference(createContentStream(content, placement.subtype));
            if (placement.subtype == "Background")
            {
                backgrounds.push_back(stream);
            }
            else
            {
                (placement.behind ? behind : front).push_back(stream);
            }
        }

        // PDF Fire: the backgrounds are the lowest layer - a new background goes under
        // everything, a mark behind the content goes over the backgrounds already there
        auto isBackgroundStream = [storage](const PDFObject& item)
        {
            const PDFObject& object = storage->getObject(item);
            const PDFObject& mark = object.isStream() ? object.getStream()->getDictionary()->get(STREAM_KEY) : PDFObject();
            return mark.isName() && mark.getString() == "Background";
        };
        const size_t existingBackgrounds = size_t(std::find_if_not(existing.cbegin(), existing.cend(), isBackgroundStream) - existing.cbegin());

        PDFArrayBuilder contents;
        for (const PDFObject& item : backgrounds)
        {
            contents.appendItem(item);
        }
        if (!front.empty() && !existing.empty())
        {
            if (!m_wrapBegin.isValid())
            {
                m_wrapBegin = createContentStream("q\n", "Wrap");
                m_wrapEnd = createContentStream("\nQ\n", "Wrap");
            }
            contents.appendItem(PDFObject::createReference(m_wrapBegin));
        }
        for (size_t i = 0; i < existingBackgrounds; ++i)
        {
            contents.appendItem(existing[i]);
        }
        for (const PDFObject& item : behind)
        {
            contents.appendItem(item);
        }
        for (size_t i = existingBackgrounds; i < existing.size(); ++i)
        {
            contents.appendItem(existing[i]);
        }
        if (!front.empty() && !existing.empty())
        {
            contents.appendItem(PDFObject::createReference(m_wrapEnd));
        }
        for (const PDFObject& item : front)
        {
            contents.appendItem(item);
        }

        resources.setEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::move(xobjects)));
        page.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resources)));
        page.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createArray(std::move(contents)));
        m_builder->setObject(pageReference, PDFObject::createDictionary(std::move(page)));
    }

    PDFDocumentBuilder* getBuilder() const { return m_builder; }

private:
    PDFObjectReference getFont(PageMarks::Font font)
    {
        auto it = m_fonts.find(font);
        if (it != m_fonts.cend())
        {
            return it->second;
        }

        PDFDictionaryBuilder dictionary;
        dictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Font"));
        dictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Type1"));
        dictionary.setEntry(PDFInplaceOrMemoryString("BaseFont"), PDFObject::createName(PageMarks::getFontBaseName(font)));
        dictionary.setEntry(PDFInplaceOrMemoryString("Encoding"), PDFObject::createName("WinAnsiEncoding"));
        const PDFObjectReference reference = m_builder->addObject(PDFObject::createDictionary(std::move(dictionary)));
        m_fonts[font] = reference;
        return reference;
    }

    PDFObjectReference createContentStream(QByteArray content, const QByteArray& mark)
    {
        PDFDictionaryBuilder dictionary;
        dictionary.setEntry(PDFInplaceOrMemoryString(STREAM_KEY), PDFObject::createName(mark));
        dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
        return m_builder->addObject(PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content))));
    }

    PDFDocumentBuilder* m_builder;
    int m_kind;
    QByteArray m_lastModified;
    std::set<PageMarks::Font> m_usedFonts;
    std::map<PageMarks::Font, PDFObjectReference> m_fonts;
    std::map<QByteArray, PDFObjectReference> m_forms;
    PDFObjectReference m_wrapBegin;
    PDFObjectReference m_wrapEnd;
};

/// Byte offset of the start of frame of a JPEG and its number of components (0 if
/// it is not found) - only a gray or RGB JPEG is embedded as it is
int getJpegComponents(const QByteArray& data)
{
    qint64 index = 2;
    while (index + 9 < data.size())
    {
        if (uchar(data[index]) != 0xFF)
        {
            return 0;
        }
        const uchar marker = uchar(data[index + 1]);
        if (marker == 0xFF)
        {
            ++index;
            continue;
        }
        const int length = (uchar(data[index + 2]) << 8) | uchar(data[index + 3]);
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC)
        {
            return uchar(data[index + 9]);
        }
        index += 2 + length;
    }
    return 0;
}

QByteArray adobeJpegTransform(const QByteArray& data)
{
    // An Adobe JPEG with three components can be written as plain RGB (transform 0)
    const qint64 index = data.indexOf("Adobe");
    return (index > 0 && index + 11 < data.size()) ? QByteArray(1, data[index + 11]) : QByteArray();
}

/// The image XObject of the watermark; its size at its resolution is returned too
PDFObjectReference createImageObject(PDFDocumentBuilder* builder, const QByteArray& data, QSizeF* naturalSize, QString* errorMessage)
{
    QImage image;
    if (data.isEmpty() || !image.loadFromData(data) || image.isNull())
    {
        *errorMessage = PDFTranslationContext::tr("The image of the watermark cannot be read.");
        return PDFObjectReference();
    }

    const double dpiX = image.dotsPerMeterX() > 400 ? image.dotsPerMeterX() * 0.0254 : 72.0;
    const double dpiY = image.dotsPerMeterY() > 400 ? image.dotsPerMeterY() * 0.0254 : 72.0;
    *naturalSize = QSizeF(image.width() * 72.0 / dpiX, image.height() * 72.0 / dpiY);

    PDFDictionaryBuilder dictionary;
    dictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    dictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
    dictionary.setEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(image.width()));
    dictionary.setEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(image.height()));
    dictionary.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));

    // A JPEG is kept as it is (re-compressing a photo would make it several times
    // bigger); a CMYK one is converted, as the inverted Adobe CMYK needs a Decode array
    const bool isJpeg = data.startsWith("\xFF\xD8");
    const int components = isJpeg ? getJpegComponents(data) : 0;
    if (components == 1 || (components == 3 && adobeJpegTransform(data) != QByteArray(1, '\0')))
    {
        QByteArray content = data;
        dictionary.setEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName(components == 1 ? "DeviceGray" : "DeviceRGB"));
        dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("DCTDecode"));
        dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
        return builder->addObject(PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content))));
    }

    const bool hasAlpha = image.hasAlphaChannel();
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    QByteArray pixels;
    pixels.reserve(qsizetype(rgb.width()) * rgb.height() * 3);
    for (int y = 0; y < rgb.height(); ++y)
    {
        pixels.append(reinterpret_cast<const char*>(rgb.constScanLine(y)), qsizetype(rgb.width()) * 3);
    }

    if (hasAlpha)
    {
        const QImage alpha = image.convertToFormat(QImage::Format_ARGB32);
        QByteArray alphaPixels;
        alphaPixels.reserve(qsizetype(alpha.width()) * alpha.height());
        for (int y = 0; y < alpha.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(alpha.constScanLine(y));
            for (int x = 0; x < alpha.width(); ++x)
            {
                alphaPixels.append(char(qAlpha(line[x])));
            }
        }

        QByteArray alphaContent = PDFFlateDecodeFilter::compress(alphaPixels);
        PDFDictionaryBuilder mask;
        mask.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
        mask.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
        mask.setEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(image.width()));
        mask.setEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(image.height()));
        mask.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
        mask.setEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceGray"));
        mask.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
        mask.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(alphaContent.size()));
        const PDFObjectReference maskReference = builder->addObject(PDFObject::createStream(PDFStream(std::move(mask), std::move(alphaContent))));
        dictionary.setEntry(PDFInplaceOrMemoryString("SMask"), PDFObject::createReference(maskReference));
    }

    QByteArray content = PDFFlateDecodeFilter::compress(pixels);
    dictionary.setEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceRGB"));
    dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
    dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
    return builder->addObject(PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content))));
}

/// Size of a rectangle turned by the angle (its bounding box)
QSizeF getRotatedSize(double width, double height, double degrees)
{
    const double radians = qDegreesToRadians(degrees);
    const double c = std::abs(std::cos(radians));
    const double s = std::abs(std::sin(radians));
    return QSizeF(width * c + height * s, width * s + height * c);
}

/// PDF Fire: the page of the source document as a form XObject of the builder's
/// document. Its content streams are joined into one stream of the form, its
/// (inherited) resources, and everything they refer to, are copied. The form space
/// is the space of the source page, its bounding box is the crop box of the page.
/// A transparency group makes an opacity apply to the page as a whole (overlapping
/// parts of the page do not show through each other).
PDFObjectReference importPageAsForm(PDFDocumentBuilder* builder, const PDFDocument* sourceDocument, const PDFPage* sourcePage,
                                    bool transparencyGroup, QString* errorMessage)
{
    const PDFObjectStorage& storage = sourceDocument->getStorage();

    QByteArray content;
    const PDFObject& contents = storage.getObject(sourcePage->getContents());
    std::vector<PDFObject> items;
    if (contents.isArray())
    {
        for (size_t i = 0; i < contents.getArray()->getCount(); ++i)
        {
            items.push_back(contents.getArray()->getItem(i));
        }
    }
    else if (contents.isStream())
    {
        items.push_back(contents);
    }
    for (const PDFObject& item : items)
    {
        const PDFObject& stream = storage.getObject(item);
        if (stream.isStream())
        {
            // The streams are separated by white space (a stream can end inside a token otherwise)
            content += storage.getDecodedStream(stream.getStream());
            content += '\n';
        }
    }

    QRectF box = sourcePage->getCropBox().normalized();
    if (!box.isValid() || box.isEmpty())
    {
        box = sourcePage->getMediaBox().normalized();
    }
    if (!box.isValid() || box.isEmpty())
    {
        *errorMessage = PDFTranslationContext::tr("The page of the background file has no size.");
        return PDFObjectReference();
    }

    PDFDictionaryBuilder dictionary;
    dictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    dictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
    dictionary.setEntry(PDFInplaceOrMemoryString("BBox"), createRealArray({ box.left(), box.top(), box.right(), box.bottom() }));

    const PDFObject& resources = sourcePage->getResources();
    dictionary.setEntry(PDFInplaceOrMemoryString("Resources"), storage.getObject(resources).isDictionary() ? resources : PDFObject::createDictionary(PDFDictionaryBuilder()));

    // The page's own transparency group (its blending color space) is kept
    const PDFDictionary* pageDictionary = storage.getDictionaryFromObject(storage.getObject(sourcePage->getPageReference()));
    const PDFObject group = pageDictionary ? pageDictionary->get("Group") : PDFObject();
    if (storage.getObject(group).isDictionary())
    {
        dictionary.setEntry(PDFInplaceOrMemoryString("Group"), group);
    }
    else if (transparencyGroup)
    {
        PDFDictionaryBuilder groupDictionary;
        groupDictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Group"));
        groupDictionary.setEntry(PDFInplaceOrMemoryString("S"), PDFObject::createName("Transparency"));
        dictionary.setEntry(PDFInplaceOrMemoryString("Group"), PDFObject::createDictionary(std::move(groupDictionary)));
    }

    QByteArray compressed = PDFFlateDecodeFilter::compress(content);
    dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
    dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressed.size()));
    const PDFObject form = PDFObject::createStream(PDFStream(std::move(dictionary), std::move(compressed)));

    // The references of the form (the resources of the page) are copied with it
    const std::vector<PDFObject> copied = builder->copyFrom({ form }, storage, true);
    if (copied.empty() || !copied.front().isReference())
    {
        *errorMessage = PDFTranslationContext::tr("The page of the background file cannot be copied.");
        return PDFObjectReference();
    }
    return copied.front().getReference();
}

std::vector<PDFInteger> getTargetPages(const PDFDocument* document, const QString& pageRange, QString* errorMessage)
{
    std::vector<PDFInteger> pages = PageMarks::parsePageRange(pageRange, document->getCatalog()->getPageCount(), errorMessage);
    if (pages.empty() && errorMessage->isEmpty())
    {
        *errorMessage = PDFTranslationContext::tr("No page is chosen.");
    }
    return pages;
}

}   // namespace

QByteArray PageMarks::getFontBaseName(Font font)
{
    switch (font)
    {
        case Font::Helvetica: return "Helvetica";
        case Font::HelveticaBold: return "Helvetica-Bold";
        case Font::HelveticaOblique: return "Helvetica-Oblique";
        case Font::HelveticaBoldOblique: return "Helvetica-BoldOblique";
        case Font::TimesRoman: return "Times-Roman";
        case Font::TimesBold: return "Times-Bold";
        case Font::TimesItalic: return "Times-Italic";
        case Font::TimesBoldItalic: return "Times-BoldItalic";
        case Font::Courier: return "Courier";
        case Font::CourierBold: return "Courier-Bold";
        case Font::CourierOblique: return "Courier-Oblique";
        case Font::CourierBoldOblique: return "Courier-BoldOblique";
    }
    return "Helvetica";
}

PageMarks::PageGeometry PageMarks::getPageGeometry(const PDFPage* page)
{
    QRectF box = page->getCropBox().normalized();
    if (!box.isValid() || box.isEmpty())
    {
        box = page->getMediaBox().normalized();
    }

    // The rectangles of PDF4QT have top() = the lower y of the page
    const double x0 = box.left();
    const double y0 = box.top();
    const double x1 = box.right();
    const double y1 = box.bottom();

    PageGeometry geometry;
    switch (page->getPageRotation())
    {
        case PageRotation::Rotate90:
            geometry.width = box.height();
            geometry.height = box.width();
            geometry.matrix = { 0, 1, -1, 0, x1, y0 };
            break;
        case PageRotation::Rotate180:
            geometry.width = box.width();
            geometry.height = box.height();
            geometry.matrix = { -1, 0, 0, -1, x1, y1 };
            break;
        case PageRotation::Rotate270:
            geometry.width = box.height();
            geometry.height = box.width();
            geometry.matrix = { 0, -1, 1, 0, x0, y1 };
            break;
        default:
            geometry.width = box.width();
            geometry.height = box.height();
            geometry.matrix = { 1, 0, 0, 1, x0, y0 };
            break;
    }
    return geometry;
}

std::vector<PDFInteger> PageMarks::parsePageRange(const QString& text, size_t pageCount, QString* errorMessage)
{
    errorMessage->clear();
    std::vector<PDFInteger> pages;
    const QString simplified = text.simplified().toLower();
    const PDFInteger count = PDFInteger(pageCount);

    if (simplified.isEmpty() || simplified == QStringLiteral("all"))
    {
        for (PDFInteger i = 0; i < count; ++i)
        {
            pages.push_back(i);
        }
        return pages;
    }
    if (simplified == QStringLiteral("odd") || simplified == QStringLiteral("even"))
    {
        for (PDFInteger i = simplified == QStringLiteral("odd") ? 0 : 1; i < count; i += 2)
        {
            pages.push_back(i);
        }
        return pages;
    }
    if (count == 0)
    {
        return pages;
    }

    const PDFClosedIntervalSet set = PDFClosedIntervalSet::parse(1, count, simplified, errorMessage);
    if (!errorMessage->isEmpty())
    {
        return {};
    }
    // The parser of PDF4QT does not check the bounds (and "0" or "9" of a document
    // with 4 pages would crash the writing)
    for (PDFInteger page : set.unfold())
    {
        if (page < 1 || page > count)
        {
            *errorMessage = PDFTranslationContext::tr("Page %1 does not exist - the document has %2 pages.").arg(page).arg(count);
            return {};
        }
        pages.push_back(page - 1);
    }
    return pages;
}

QByteArray PageMarks::encodeWinAnsi(const QString& text)
{
    QByteArray result;
    for (char32_t character : text.toUcs4())
    {
        if (character == U'\t')
        {
            result.append(' ');
        }
        else if ((character >= 32 && character <= 126) || (character >= 160 && character <= 255))
        {
            result.append(char(character));
        }
        else
        {
            char byte = '?';
            for (int i = 0; i < 32; ++i)
            {
                if (WINANSI_128_159[i] && char32_t(WINANSI_128_159[i]) == character)
                {
                    byte = char(128 + i);
                    break;
                }
            }
            result.append(byte);
        }
    }
    return result;
}

double PageMarks::getTextWidth(Font font, const QByteArray& encoded, double fontSize)
{
    const std::array<quint16, 224>* table = getWidthTable(font);
    double width = 0.0;
    for (char byte : encoded)
    {
        const int code = uchar(byte);
        if (!table)
        {
            width += 600;
        }
        else if (code >= 32)
        {
            width += (*table)[code - 32];
        }
    }
    return width * fontSize / 1000.0;
}

QString PageMarks::formatBates(const QString& prefix, qint64 number, int digits, const QString& suffix)
{
    return prefix + QStringLiteral("%1").arg(number, digits, 10, QChar('0')) + suffix;
}

QString PageMarks::expandTokens(const QString& text, int pageNumber, int pageCount, const QDate& date, const QString& dateFormat,
                                const QString& fileName, const QString& bates)
{
    // <<date:format>> has its own format (Qt date format: d, dd, M, MM, MMM, MMMM, yy, yyyy)
    static const QRegularExpression tokenExpression(QStringLiteral("<<\\s*(page|pages|date|filename|bates)\\s*(?::([^>]*))?>>"),
                                                    QRegularExpression::CaseInsensitiveOption);
    QString result;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = tokenExpression.globalMatch(text);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        result += text.mid(last, match.capturedStart() - last);
        const QString token = match.captured(1).toLower();
        if (token == QStringLiteral("page"))
        {
            result += QString::number(pageNumber);
        }
        else if (token == QStringLiteral("pages"))
        {
            result += QString::number(pageCount);
        }
        else if (token == QStringLiteral("date"))
        {
            const QString format = match.captured(2).trimmed();
            result += date.toString(format.isEmpty() ? dateFormat : format);
        }
        else if (token == QStringLiteral("filename"))
        {
            result += fileName;
        }
        else
        {
            result += bates;
        }
        last = match.capturedEnd();
    }
    result += text.mid(last);
    return result;
}

bool PageMarks::addWatermark(PDFDocumentBuilder* builder, const PDFDocument* document, const WatermarkSettings& settings,
                             QString* errorMessage, std::optional<PDFInteger> onlyPage)
{
    errorMessage->clear();
    const std::vector<PDFInteger> pages = getTargetPages(document, settings.pageRange, errorMessage);
    if (pages.empty())
    {
        return false;
    }

    const QStringList lines = settings.text.split(QChar('\n'));
    if (!settings.useImage && settings.text.trimmed().isEmpty())
    {
        *errorMessage = PDFTranslationContext::tr("The text of the watermark is empty.");
        return false;
    }

    MarkWriter writer(builder, Watermark);

    PDFObjectReference image;
    QSizeF imageSize;
    if (settings.useImage)
    {
        image = createImageObject(builder, settings.imageData, &imageSize, errorMessage);
        if (!image.isValid())
        {
            return false;
        }
    }

    std::vector<QByteArray> encodedLines;
    std::vector<double> unitWidths;
    double maxUnitWidth = 0.0;
    for (const QString& line : lines)
    {
        encodedLines.push_back(encodeWinAnsi(line));
        unitWidths.push_back(getTextWidth(settings.style.font, encodedLines.back(), 1.0));
        maxUnitWidth = std::max(maxUnitWidth, unitWidths.back());
    }

    const FontMetrics metrics = getFontMetrics(settings.style.font);
    constexpr double LEADING = 1.2;
    const double unitHeight = metrics.capHeight + metrics.descent + (encodedLines.size() - 1) * LEADING;
    const double opacity = qBound(0.0, settings.opacity, 1.0);

    for (PDFInteger pageIndex : pages)
    {
        if (onlyPage && *onlyPage != pageIndex)
        {
            continue;
        }

        const PDFPage* page = document->getCatalog()->getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        const PageGeometry geometry = getPageGeometry(page);
        const double angle = settings.diagonal ? qRadiansToDegrees(std::atan2(geometry.height, geometry.width)) : settings.rotation;

        // The size of the block (text or image) before it is turned
        double blockWidth = 0.0;
        double blockHeight = 0.0;
        double fontSize = settings.style.fontSize;
        if (settings.useImage)
        {
            double scale = settings.imageScale;
            if (settings.imageScaleRelativeToPage)
            {
                const QSizeF rotated = getRotatedSize(imageSize.width(), imageSize.height(), angle);
                scale *= std::min(geometry.width / rotated.width(), geometry.height / rotated.height());
            }
            blockWidth = imageSize.width() * scale;
            blockHeight = imageSize.height() * scale;
        }
        else
        {
            if (settings.fitToPage && maxUnitWidth > 0.0)
            {
                // A margin of 5 % on each side, as the text would touch the edge otherwise
                const QSizeF rotated = getRotatedSize(maxUnitWidth, unitHeight, angle);
                fontSize = std::min(0.9 * geometry.width / rotated.width(), 0.9 * geometry.height / rotated.height());
            }
            blockWidth = maxUnitWidth * fontSize;
            blockHeight = unitHeight * fontSize;
        }

        const QSizeF rotatedSize = getRotatedSize(blockWidth, blockHeight, angle);
        double centerX = geometry.width / 2.0;
        double centerY = geometry.height / 2.0;
        if (settings.alignment.testFlag(Qt::AlignLeft))
        {
            centerX = rotatedSize.width() / 2.0;
        }
        else if (settings.alignment.testFlag(Qt::AlignRight))
        {
            centerX = geometry.width - rotatedSize.width() / 2.0;
        }
        if (settings.alignment.testFlag(Qt::AlignTop))
        {
            centerY = geometry.height - rotatedSize.height() / 2.0;
        }
        else if (settings.alignment.testFlag(Qt::AlignBottom))
        {
            centerY = rotatedSize.height() / 2.0;
        }
        centerX += settings.offsetX;
        centerY += settings.offsetY;

        // Block space (origin in the lower left corner of the block) -> shown page
        QTransform blockTransform;
        blockTransform.translate(centerX, centerY);
        blockTransform.rotate(angle);
        blockTransform.translate(-blockWidth / 2.0, -blockHeight / 2.0);

        QByteArray content = "q\n";
        if (opacity < 1.0)
        {
            content += "/GS0 gs\n";
        }

        if (settings.useImage)
        {
            const QTransform imageTransform = QTransform(blockWidth, 0, 0, blockHeight, 0, 0) * blockTransform;
            content += formatMatrix(imageTransform) + " cm\n/Im0 Do\n";
        }
        else
        {
            const QByteArray fontName = writer.getFontResourceName(settings.style.font);
            content += fillColor(settings.style.color);
            content += "BT\n/" + fontName + " " + formatNumber(fontSize) + " Tf\n";
            for (size_t i = 0; i < encodedLines.size(); ++i)
            {
                if (encodedLines[i].isEmpty())
                {
                    continue;
                }

                // The lines are centered in the block; the first line has its capitals
                // at the top of the block
                const double x = (maxUnitWidth - unitWidths[i]) * fontSize / 2.0;
                const double y = blockHeight - (metrics.capHeight + i * LEADING) * fontSize;
                const QTransform lineTransform = QTransform::fromTranslate(x, y) * blockTransform;
                content += formatMatrix(lineTransform) + " Tm " + toLiteralString(encodedLines[i]) + " Tj\n";
            }
            content += "ET\n";
        }
        content += "Q\n";

        const PDFObjectReference form = writer.createForm(content, geometry, opacity < 1.0 ? std::optional<double>(opacity) : std::nullopt, image);
        writer.attach(page->getPageReference(), { MarkWriter::Placement{ form, "Watermark", settings.behind } });
    }

    return true;
}

bool PageMarks::addHeaderFooter(PDFDocumentBuilder* builder, const PDFDocument* document, const HeaderFooterSettings& settings,
                                QString* errorMessage, std::optional<PDFInteger> onlyPage)
{
    errorMessage->clear();
    std::vector<PDFInteger> pages = getTargetPages(document, settings.pageRange, errorMessage);
    if (settings.skipFirstPage)
    {
        pages.erase(std::remove(pages.begin(), pages.end(), PDFInteger(0)), pages.end());
        if (pages.empty() && errorMessage->isEmpty())
        {
            *errorMessage = PDFTranslationContext::tr("No page is chosen.");
        }
    }
    if (pages.empty())
    {
        return false;
    }

    if (std::all_of(settings.texts.cbegin(), settings.texts.cend(), [](const QString& text) { return text.trimmed().isEmpty(); }))
    {
        *errorMessage = PDFTranslationContext::tr("All the boxes of the header and footer are empty.");
        return false;
    }

    MarkWriter writer(builder, settings.bates ? Bates : HeaderFooter);
    const FontMetrics metrics = getFontMetrics(settings.style.font);
    const double fontSize = settings.style.fontSize;
    const int pageCount = int(document->getCatalog()->getPageCount());
    const int lastPageNumber = settings.startPageNumber + pageCount - 1;

    for (size_t k = 0; k < pages.size(); ++k)
    {
        const PDFInteger pageIndex = pages[k];
        if (onlyPage && *onlyPage != pageIndex)
        {
            continue;
        }

        const PDFPage* page = document->getCatalog()->getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        const PageGeometry geometry = getPageGeometry(page);

        // The page number is the number of the page in the document (the first page
        // has the start number); a Bates number grows on the marked pages only
        const int pageNumber = settings.startPageNumber + int(pageIndex);
        const QString bates = formatBates(settings.batesPrefix, settings.batesStart + qint64(k), settings.batesDigits, settings.batesSuffix);

        std::vector<MarkWriter::Placement> placements;
        for (int part = 0; part < 2; ++part)
        {
            const bool header = part == 0;
            QByteArray text;
            for (int box = 0; box < 3; ++box)
            {
                const QString expanded = expandTokens(settings.texts[(header ? HeaderLeft : FooterLeft) + box], pageNumber, lastPageNumber,
                                                      settings.date, settings.dateFormat, settings.fileName, bates);
                if (expanded.trimmed().isEmpty())
                {
                    continue;
                }

                const QByteArray encoded = encodeWinAnsi(expanded);
                const double width = getTextWidth(settings.style.font, encoded, fontSize);
                double x = settings.marginLeft;
                if (box == 1)
                {
                    x = (geometry.width - width) / 2.0;
                }
                else if (box == 2)
                {
                    x = geometry.width - settings.marginRight - width;
                }
                const double y = header ? geometry.height - settings.marginTop - metrics.capHeight * fontSize
                                        : settings.marginBottom + metrics.descent * fontSize;
                text += "1 0 0 1 " + formatNumber(x) + " " + formatNumber(y) + " Tm " + toLiteralString(encoded) + " Tj\n";
            }

            if (text.isEmpty())
            {
                continue;
            }

            const QByteArray fontName = writer.getFontResourceName(settings.style.font);
            QByteArray content = "q\n" + fillColor(settings.style.color) + "BT\n/" + fontName + " " + formatNumber(fontSize) + " Tf\n" + text + "ET\nQ\n";
            const PDFObjectReference form = writer.createForm(content, geometry, std::nullopt, PDFObjectReference());
            placements.push_back({ form, header ? QByteArray("Header") : QByteArray("Footer"), false });
        }

        writer.attach(page->getPageReference(), placements);
    }

    return true;
}

bool PageMarks::addBatesNumbers(PDFDocumentBuilder* builder, const PDFDocument* document, const BatesSettings& settings,
                                QString* errorMessage, std::optional<PDFInteger> onlyPage)
{
    HeaderFooterSettings headerFooter;
    headerFooter.texts[qBound(0, int(settings.position), int(BoxCount) - 1)] = QStringLiteral("<<bates>>");
    headerFooter.style = settings.style;
    headerFooter.marginTop = settings.marginTop;
    headerFooter.marginBottom = settings.marginBottom;
    headerFooter.marginLeft = settings.marginLeft;
    headerFooter.marginRight = settings.marginRight;
    headerFooter.pageRange = settings.pageRange;
    headerFooter.bates = true;
    headerFooter.batesPrefix = settings.prefix;
    headerFooter.batesSuffix = settings.suffix;
    headerFooter.batesStart = settings.start;
    headerFooter.batesDigits = settings.digits;
    return addHeaderFooter(builder, document, headerFooter, errorMessage, onlyPage);
}

bool PageMarks::addBackground(PDFDocumentBuilder* builder, const PDFDocument* document, const PDFDocument* sourceDocument,
                              const BackgroundSettings& settings, QString* errorMessage, std::optional<PDFInteger> onlyPage)
{
    errorMessage->clear();
    if (!sourceDocument || sourceDocument->getCatalog()->getPageCount() == 0)
    {
        *errorMessage = PDFTranslationContext::tr("Choose the PDF file of the background.");
        return false;
    }

    const PDFInteger sourcePageCount = PDFInteger(sourceDocument->getCatalog()->getPageCount());
    const PDFPage* sourcePage = (settings.sourcePageIndex >= 0 && settings.sourcePageIndex < sourcePageCount)
                                    ? sourceDocument->getCatalog()->getPage(settings.sourcePageIndex) : nullptr;
    if (!sourcePage)
    {
        *errorMessage = PDFTranslationContext::tr("Page %1 of the background file does not exist - the file has %2 pages.").arg(settings.sourcePageIndex + 1).arg(sourcePageCount);
        return false;
    }

    const std::vector<PDFInteger> pages = getTargetPages(document, settings.pageRange, errorMessage);
    if (pages.empty())
    {
        return false;
    }

    const PageGeometry sourceGeometry = getPageGeometry(sourcePage);
    if (sourceGeometry.width <= 0.0 || sourceGeometry.height <= 0.0)
    {
        *errorMessage = PDFTranslationContext::tr("The page of the background file has no size.");
        return false;
    }

    const double opacity = qBound(0.0, settings.opacity, 1.0);

    // The source page is imported once, all the pages draw the same form
    const PDFObjectReference sourceForm = importPageAsForm(builder, sourceDocument, sourcePage, opacity < 1.0, errorMessage);
    if (!sourceForm.isValid())
    {
        return false;
    }

    // Space of the source page -> the source page as it is shown (upright)
    const std::array<double, 6>& sm = sourceGeometry.matrix;
    const QTransform sourceToShown = QTransform(sm[0], sm[1], sm[2], sm[3], sm[4], sm[5]).inverted();

    MarkWriter writer(builder, Background);
    for (PDFInteger pageIndex : pages)
    {
        if (onlyPage && *onlyPage != pageIndex)
        {
            continue;
        }

        const PDFPage* page = document->getCatalog()->getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        const PageGeometry geometry = getPageGeometry(page);
        double scale = settings.scale > 0.0 ? settings.scale : 1.0;
        if (settings.fitToPage)
        {
            scale *= std::min(geometry.width / sourceGeometry.width, geometry.height / sourceGeometry.height);
        }

        const double width = sourceGeometry.width * scale;
        const double height = sourceGeometry.height * scale;
        double x = (geometry.width - width) / 2.0;
        double y = (geometry.height - height) / 2.0;
        if (settings.alignment.testFlag(Qt::AlignLeft))
        {
            x = 0.0;
        }
        else if (settings.alignment.testFlag(Qt::AlignRight))
        {
            x = geometry.width - width;
        }
        if (settings.alignment.testFlag(Qt::AlignTop))
        {
            y = geometry.height - height;
        }
        else if (settings.alignment.testFlag(Qt::AlignBottom))
        {
            y = 0.0;
        }
        x += settings.offsetX;
        y += settings.offsetY;

        // Source page -> shown source page -> scaled and placed on the shown page (the
        // form of the mark turns the shown page to the page by its matrix)
        const QTransform placement = sourceToShown * QTransform(scale, 0, 0, scale, x, y);
        QByteArray content = "q\n";
        if (opacity < 1.0)
        {
            content += "/GS0 gs\n";
        }
        content += formatMatrix(placement) + " cm\n/Bg0 Do\nQ\n";

        const PDFObjectReference form = writer.createForm(content, geometry, opacity < 1.0 ? std::optional<double>(opacity) : std::nullopt, sourceForm, "Bg0");

        // A background artifact requires its bounding box (in the space of the page)
        const QRectF shownBox = QRectF(x, y, width, height).intersected(QRectF(0, 0, geometry.width, geometry.height));
        const std::array<double, 6>& m = geometry.matrix;
        const QRectF pageBox = QTransform(m[0], m[1], m[2], m[3], m[4], m[5]).mapRect(shownBox.isValid() ? shownBox : QRectF(0, 0, geometry.width, geometry.height));
        const QByteArray properties = "<</Type /Background /BBox [" + formatNumber(pageBox.left()) + " " + formatNumber(pageBox.top()) + " " +
                                      formatNumber(pageBox.right()) + " " + formatNumber(pageBox.bottom()) + "]>>";

        writer.attach(page->getPageReference(), { MarkWriter::Placement{ form, "Background", true, properties } });
    }

    return true;
}

int PageMarks::countMarks(const PDFDocument* document, int kinds)
{
    int count = 0;
    for (int pageCount : countMarksOnPages(document, kinds))
    {
        count += pageCount;
    }
    return count;
}

int PageMarks::countMarkedPages(const PDFDocument* document, int kinds)
{
    const std::vector<int> counts = countMarksOnPages(document, kinds);
    return int(std::count_if(counts.cbegin(), counts.cend(), [](int count) { return count > 0; }));
}

std::vector<int> PageMarks::countMarksOnPages(const PDFDocument* document, int kinds)
{
    const PDFObjectStorage* storage = &document->getStorage();
    const PDFCatalog* catalog = document->getCatalog();

    std::vector<int> counts(catalog->getPageCount(), 0);
    for (size_t i = 0; i < catalog->getPageCount(); ++i)
    {
        const PDFObjectReference pageReference = catalog->getPage(i)->getPageReference();
        const PDFDictionary* pageDictionary = getDictionary(storage, storage->getObject(pageReference));
        if (!pageDictionary)
        {
            continue;
        }

        const PDFDictionary* resources = getDictionary(storage, getInheritedEntry(storage, pageReference, "Resources"));
        const PDFDictionary* xobjects = resources ? getDictionary(storage, resources->get("XObject")) : nullptr;
        for (const PDFObject& item : getContentItems(storage, pageDictionary))
        {
            const PDFObject& stream = storage->getObject(item);
            if (!stream.isStream())
            {
                continue;
            }
            for (const FoundMark& mark : findMarks(storage->getDecodedStream(stream.getStream()), storage, xobjects))
            {
                if (mark.kind & kinds)
                {
                    ++counts[i];
                }
            }
        }
    }
    return counts;
}

int PageMarks::removeMarks(PDFDocumentBuilder* builder, const PDFDocument* document, int kinds)
{
    const PDFCatalog* catalog = document->getCatalog();

    int removed = 0;
    for (size_t i = 0; i < catalog->getPageCount(); ++i)
    {
        const PDFObjectStorage* storage = builder->getStorage();
        const PDFObjectReference pageReference = catalog->getPage(i)->getPageReference();
        const PDFDictionary* pageDictionary = getDictionary(storage, storage->getObject(pageReference));
        if (!pageDictionary)
        {
            continue;
        }

        const PDFDictionary* resources = getDictionary(storage, getInheritedEntry(storage, pageReference, "Resources"));
        const PDFDictionary* xobjects = resources ? getDictionary(storage, resources->get("XObject")) : nullptr;

        std::vector<PDFObject> items;
        std::vector<QByteArray> removedForms;
        int remaining = 0;
        bool changed = false;

        for (const PDFObject& item : getContentItems(storage, pageDictionary))
        {
            const PDFObject& streamObject = storage->getObject(item);
            if (!streamObject.isStream())
            {
                items.push_back(item);
                continue;
            }

            const PDFStream* stream = streamObject.getStream();
            const QByteArray content = storage->getDecodedStream(stream);
            std::vector<FoundMark> marks = findMarks(content, storage, xobjects);

            std::vector<FoundMark> toRemove;
            for (const FoundMark& mark : marks)
            {
                if (mark.kind & kinds)
                {
                    toRemove.push_back(mark);
                }
                else
                {
                    ++remaining;
                }
            }

            if (toRemove.empty())
            {
                // A stream without the marks is kept as it is (the same object)
                items.push_back(item);
                continue;
            }

            changed = true;
            removed += int(toRemove.size());

            QByteArray newContent;
            qint64 last = 0;
            for (const FoundMark& mark : toRemove)
            {
                newContent += content.mid(last, mark.begin - last);
                last = mark.end;
                removedForms.push_back(mark.formName);
            }
            newContent += content.mid(last);

            const PDFDictionary* streamDictionary = stream->getDictionary();
            if (streamDictionary->hasKey(STREAM_KEY) && newContent.trimmed().isEmpty())
            {
                // Our own stream of the mark is dropped as a whole
                continue;
            }

            // A stream with other content (Acrobat's marks, or content merged by another
            // program) is written again without the marks, as a new object - the old
            // stream can be shared by other pages
            PDFDictionaryBuilder dictionary(*streamDictionary);
            const bool compressed = streamDictionary->hasKey("Filter");
            dictionary.removeEntry("Filter");
            dictionary.removeEntry("DecodeParms");
            dictionary.removeEntry("DL");
            if (compressed)
            {
                newContent = PDFFlateDecodeFilter::compress(newContent);
                dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
            }
            dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(newContent.size()));
            items.push_back(PDFObject::createReference(builder->addObject(PDFObject::createStream(PDFStream(std::move(dictionary), std::move(newContent))))));
        }

        if (!changed)
        {
            continue;
        }

        if (remaining == 0)
        {
            // No mark stays in front of the content: the q/Q around it is not needed
            items.erase(std::remove_if(items.begin(), items.end(), [storage](const PDFObject& item)
            {
                const PDFObject& object = storage->getObject(item);
                if (!object.isStream())
                {
                    return false;
                }
                const PDFObject& mark = object.getStream()->getDictionary()->get(STREAM_KEY);
                return mark.isName() && mark.getString() == "Wrap";
            }), items.end());
        }

        PDFDictionaryBuilder page(*pageDictionary);
        if (items.empty())
        {
            page.removeEntry("Contents");
        }
        else if (items.size() == 1 && items.front().isReference())
        {
            page.setEntry(PDFInplaceOrMemoryString("Contents"), items.front());
        }
        else
        {
            PDFArrayBuilder contents;
            for (const PDFObject& item : items)
            {
                contents.appendItem(item);
            }
            page.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createArray(std::move(contents)));
        }

        // Our names of the removed forms are removed from the resources of the page
        // (they are always in the page's own dictionary, written by us)
        const PDFObject& pageResources = pageDictionary->get("Resources");
        if (pageResources.isDictionary() && pageResources.getDictionary()->get("XObject").isDictionary())
        {
            PDFDictionaryBuilder newResources(*pageResources.getDictionary());
            PDFDictionaryBuilder newXObjects(*pageResources.getDictionary()->get("XObject").getDictionary());
            bool resourcesChanged = false;
            for (const QByteArray& name : removedForms)
            {
                if (name.startsWith(NAME_PREFIX) && newXObjects.hasKey(name))
                {
                    newXObjects.removeEntry(name.constData());
                    resourcesChanged = true;
                }
            }
            if (resourcesChanged)
            {
                newResources.setEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::move(newXObjects)));
                page.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(newResources)));
            }
        }

        builder->setObject(pageReference, PDFObject::createDictionary(std::move(page)));
    }

    return removed;
}

}   // namespace pdfplugin
