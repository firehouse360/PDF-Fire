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

// PDF Fire: tests of the redaction, which keeps the text outside of the redacted areas.
// Every test creates a PDF by hand (so the content stream is exactly known), marks areas
// by redact annotations, redacts it and then checks the result adversarially with external
// tools: the redacted strings must not be in the decompressed file (qpdf --qdf) nor in the
// extracted text (pdftotext), the other text must be extractable, qpdf --check must pass and
// the renders (pdftoppm) before and after must differ only inside of the areas.

#include "pdfredact.h"
#include "pdffireredaction.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfoptionalcontent.h"
#include "pdfmeshqualitysettings.h"
#include "pdfconstants.h"

#include <QtTest>
#include <QPainter>
#include <QBuffer>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QProcess>
#include <QRawFont>
#include <QTemporaryDir>
#include <QStandardPaths>

using namespace pdf;

namespace
{

/// Writer of a PDF file "by hand"
class RawPdf
{
public:
    int add(const QByteArray& body)
    {
        m_objects.push_back(body);
        return int(m_objects.size());
    }

    int reserve() { return add(QByteArray()); }
    void set(int number, const QByteArray& body) { m_objects[size_t(number - 1)] = body; }

    static QByteArray stream(const QByteArray& dictionary, const QByteArray& data)
    {
        return "<< " + dictionary + " /Length " + QByteArray::number(data.size()) + " >>\nstream\n" + data + "\nendstream";
    }

    static QByteArray ref(int number) { return QByteArray::number(number) + " 0 R"; }

    QByteArray build(int root) const
    {
        QByteArray result = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
        std::vector<qint64> offsets;
        for (size_t i = 0; i < m_objects.size(); ++i)
        {
            offsets.push_back(result.size());
            result += QByteArray::number(int(i + 1)) + " 0 obj\n" + m_objects[i] + "\nendobj\n";
        }
        const qint64 xref = result.size();
        result += "xref\n0 " + QByteArray::number(int(m_objects.size() + 1)) + "\n0000000000 65535 f \n";
        for (qint64 offset : offsets)
        {
            result += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
        }
        result += "trailer\n<< /Size " + QByteArray::number(int(m_objects.size() + 1)) + " /Root " + ref(root) + " >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        return result;
    }

private:
    std::vector<QByteArray> m_objects;
};

struct PageSpec
{
    QByteArray content;
    QByteArray resources;           ///< Additional entries of the resource dictionary
    QByteArray annotations;         ///< Additional annotation dictionaries
    QList<QRectF> areas;            ///< Redacted areas (PDF coordinates)
    int rotate = 0;
    QSizeF size = QSizeF(400, 300);
};

struct RedactionResult
{
    QString sourceFile;
    QString outputFile;
    QStringList messages;
    bool ok = false;
};

}   // namespace

class RedactionTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void plainText();
    void kernedTextArray();
    void textInFormXObject();
    void invisibleText();
    void wordSplitAcrossOperators();
    void overlappingAreas();
    void standardFontWithoutWidths();
    void imageHalfCovered();
    void inlineImage();
    void sharedImageOnTwoPages();
    void rotatedPage();
    void vectorGraphics();
    void clippingText();
    void markedContentText();
    void annotationsAndFields();
    void type3TextIsConvertedToOutlines();
    void outlinesModeStillWorks();
    void compositeFontFromQt();
    void hiddenOptionalContent();
    void imagesInFormAndOtherFormats();
    void tilingPatternAndSoftMaskFallBack();
    void widthsDifferentFromFontProgram();
    void inconsistentContentFallsBack();
    void clipAndUnpaintedPathsInArea();
    void axialShadingInArea();
    void labelOnBoxes();
    void redactFileFromEnvironment();

private:
    // Document creation
    int addDejaVuFont(RawPdf& pdf);
    int addHelveticaFont(RawPdf& pdf);
    QByteArray buildDocument(RawPdf& pdf, const std::vector<PageSpec>& pages, int font1, int font2 = 0, const QByteArray& catalogEntries = QByteArray());
    PDFReal width(const QByteArray& text, PDFReal fontSize) const;
    QRectF wordArea(PDFReal x, PDFReal y, const QByteArray& before, const QByteArray& word, PDFReal fontSize) const;

    // Redaction
    RedactionResult redact(const QByteArray& source, const QString& name, bool keepText = true, QColor fillColor = Qt::black,
                           const QString& label = QString(), QColor labelColor = QColor());

    // Checks
    bool hasTool(const QString& tool) const;
    QByteArray decompressed(const QString& fileName);
    bool qpdfCheck(const QString& fileName);
    QString extractText(const QString& fileName, int page = 0);
    QImage render(const QString& fileName, int page);
    int countDifferencesOutside(const QImage& before, const QImage& after, const QList<QRectF>& areas, const QSizeF& pageSize, int rotation, PDFReal margin = 1.5) const;
    static bool containsString(const QByteArray& data, const QByteArray& string);
    void checkSecretsRemoved(const RedactionResult& result, const QList<QByteArray>& secrets);
    void checkTextKept(const RedactionResult& result, const QStringList& words, int page = 0);
    void checkRendering(const RedactionResult& result, const std::vector<PageSpec>& pages);

    QTemporaryDir m_directory;
    QByteArray m_dejaVuData;
    std::vector<int> m_dejaVuWidths;  // codes 32..126
    bool m_toolsAvailable = false;
};

// ---------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------

void RedactionTest::initTestCase()
{
    QVERIFY(m_directory.isValid());
    if (qEnvironmentVariableIsSet("PDFFIRE_REDACT_KEEP"))
    {
        // The files are kept for a manual check
        m_directory.setAutoRemove(false);
        qInfo() << "Test files:" << m_directory.path();
    }

    QFile fontFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    if (fontFile.open(QIODevice::ReadOnly))
    {
        m_dejaVuData = fontFile.readAll();
    }

    if (!m_dejaVuData.isEmpty())
    {
        QRawFont rawFont(m_dejaVuData, 1000.0, QFont::PreferNoHinting);
        for (int code = 32; code <= 126; ++code)
        {
            QList<quint32> glyphs = rawFont.glyphIndexesForString(QString(QChar(code)));
            QList<QPointF> advances = rawFont.advancesForGlyphIndexes(glyphs, QRawFont::UseDesignMetrics);
            m_dejaVuWidths.push_back(advances.isEmpty() ? 0 : qRound(advances.front().x()));
        }
    }

    m_toolsAvailable = hasTool("qpdf") && hasTool("pdftotext") && hasTool("pdftoppm");
}

bool RedactionTest::hasTool(const QString& tool) const
{
    return !QStandardPaths::findExecutable(tool).isEmpty();
}

int RedactionTest::addDejaVuFont(RawPdf& pdf)
{
    if (m_dejaVuData.isEmpty())
    {
        return 0;
    }

    const int fontFile = pdf.add(RawPdf::stream("/Length1 " + QByteArray::number(m_dejaVuData.size()), m_dejaVuData));
    const int descriptor = pdf.add("<< /Type /FontDescriptor /FontName /DejaVuSans /Flags 32 /FontBBox [-1021 -463 1793 1232] "
                                   "/ItalicAngle 0 /Ascent 928 /Descent -236 /CapHeight 729 /StemV 80 /FontFile2 " + RawPdf::ref(fontFile) + " >>");
    QByteArray widths;
    for (int value : m_dejaVuWidths)
    {
        widths += QByteArray::number(value) + ' ';
    }
    return pdf.add("<< /Type /Font /Subtype /TrueType /BaseFont /DejaVuSans /FirstChar 32 /LastChar 126 /Widths [" + widths +
                   "] /Encoding /WinAnsiEncoding /FontDescriptor " + RawPdf::ref(descriptor) + " >>");
}

int RedactionTest::addHelveticaFont(RawPdf& pdf)
{
    return pdf.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
}

PDFReal RedactionTest::width(const QByteArray& text, PDFReal fontSize) const
{
    PDFReal result = 0.0;
    for (char c : text)
    {
        const int code = static_cast<unsigned char>(c);
        if (code >= 32 && code <= 126)
        {
            result += m_dejaVuWidths[size_t(code - 32)];
        }
    }
    return result * fontSize / 1000.0;
}

QRectF RedactionTest::wordArea(PDFReal x, PDFReal y, const QByteArray& before, const QByteArray& word, PDFReal fontSize) const
{
    const PDFReal start = x + width(before, fontSize);
    const PDFReal end = start + width(word, fontSize);
    return QRectF(QPointF(start - 1.0, y - 0.3 * fontSize), QPointF(end + 1.0, y + 1.0 * fontSize));
}

QByteArray RedactionTest::buildDocument(RawPdf& pdf, const std::vector<PageSpec>& pages, int font1, int font2, const QByteArray& catalogEntries)
{
    const int pagesObject = pdf.reserve();
    QByteArray kids;

    for (const PageSpec& page : pages)
    {
        const int content = pdf.add(RawPdf::stream(QByteArray(), page.content));

        QByteArray annotations;
        for (const QRectF& area : page.areas)
        {
            const int annotation = pdf.add("<< /Type /Annot /Subtype /Redact /Rect [" +
                                           QByteArray::number(area.left()) + ' ' + QByteArray::number(area.top()) + ' ' +
                                           QByteArray::number(area.right()) + ' ' + QByteArray::number(area.bottom()) + "] /IC [0 0 0] >>");
            annotations += RawPdf::ref(annotation) + ' ';
        }

        if (!page.annotations.isEmpty())
        {
            annotations += page.annotations;
        }

        QByteArray fonts = "/F1 " + RawPdf::ref(font1);
        if (font2)
        {
            fonts += " /F2 " + RawPdf::ref(font2);
        }

        const int pageObject = pdf.add("<< /Type /Page /Parent " + RawPdf::ref(pagesObject) +
                                       " /MediaBox [0 0 " + QByteArray::number(page.size.width()) + ' ' + QByteArray::number(page.size.height()) + "]" +
                                       (page.rotate ? " /Rotate " + QByteArray::number(page.rotate) : QByteArray()) +
                                       " /Resources << /Font << " + fonts + " >> " + page.resources + " >>" +
                                       " /Contents " + RawPdf::ref(content) +
                                       (annotations.isEmpty() ? QByteArray() : " /Annots [" + annotations + "]") + " >>");
        kids += RawPdf::ref(pageObject) + ' ';
    }

    pdf.set(pagesObject, "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(int(pages.size())) + " >>");
    const int catalog = pdf.add("<< /Type /Catalog /Pages " + RawPdf::ref(pagesObject) + ' ' + catalogEntries + " >>");
    return pdf.build(catalog);
}

RedactionResult RedactionTest::redact(const QByteArray& source, const QString& name, bool keepText, QColor fillColor,
                                      const QString& label, QColor labelColor)
{
    RedactionResult result;
    result.sourceFile = m_directory.filePath(name + ".pdf");
    result.outputFile = m_directory.filePath(name + "_REDACTED.pdf");

    QFile sourceFile(result.sourceFile);
    if (!sourceFile.open(QIODevice::WriteOnly))
    {
        return result;
    }
    sourceFile.write(source);
    sourceFile.close();

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(source);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        qWarning() << reader.getErrorMessage();
        return result;
    }

    PDFOptionalContentActivity optionalContentActivity(&document, OCUsage::Export, nullptr);
    PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    PDFCMSPointer cms = cmsManager.getCurrentCMS();
    PDFMeshQualitySettings meshQualitySettings;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFModifiedDocument modifiedDocument(&document, &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    PDFRedact redactor(&document, &fontCache, cms.get(), &optionalContentActivity, &meshQualitySettings, fillColor);
    redactor.setLabel(label, labelColor);
    PDFRedact::Options options = PDFRedact::CopyTitle;
    options.setFlag(PDFRedact::KeepTextSearchable, keepText);
    PDFDocument redacted = redactor.perform(options);
    result.messages = redactor.getMessages();
    fontCache.setCacheShrinkEnabled(nullptr, true);

    PDFDocumentWriter writer(nullptr);
    result.ok = bool(writer.write(result.outputFile, &redacted, false));
    return result;
}

QByteArray RedactionTest::decompressed(const QString& fileName)
{
    const QString output = fileName + ".qdf";
    QProcess::execute("qpdf", { "--qdf", "--object-streams=disable", "--decode-level=all", fileName, output });
    QFile file(output);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    return file.readAll();
}

bool RedactionTest::qpdfCheck(const QString& fileName)
{
    QProcess process;
    process.start("qpdf", { "--check", fileName });
    process.waitForFinished(60000);
    if (process.exitCode() != 0)
    {
        qWarning() << process.readAllStandardOutput() << process.readAllStandardError();
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QString RedactionTest::extractText(const QString& fileName, int page)
{
    QStringList arguments;
    if (page > 0)
    {
        arguments << "-f" << QString::number(page) << "-l" << QString::number(page);
    }
    arguments << fileName << "-";

    QProcess process;
    process.start("pdftotext", arguments);
    process.waitForFinished(60000);
    return QString::fromUtf8(process.readAllStandardOutput());
}

QImage RedactionTest::render(const QString& fileName, int page)
{
    const QString prefix = fileName + "_page" + QString::number(page);
    QProcess::execute("pdftoppm", { "-r", "72", "-gray", "-hide-annotations", "-singlefile",
                                    "-f", QString::number(page), "-l", QString::number(page), fileName, prefix });
    return QImage(prefix + ".pgm").convertToFormat(QImage::Format_Grayscale8);
}

int RedactionTest::countDifferencesOutside(const QImage& before, const QImage& after, const QList<QRectF>& areas, const QSizeF& pageSize, int rotation, PDFReal margin) const
{
    if (before.size() != after.size() || before.isNull())
    {
        return -1;
    }

    int differences = 0;
    for (int v = 0; v < before.height(); ++v)
    {
        const uchar* rowBefore = before.constScanLine(v);
        const uchar* rowAfter = after.constScanLine(v);
        for (int u = 0; u < before.width(); ++u)
        {
            const PDFReal du = u + 0.5;
            const PDFReal dv = v + 0.5;
            QPointF point;
            switch (rotation)
            {
                case 90: point = QPointF(dv, du); break;
                default: point = QPointF(du, pageSize.height() - dv); break;
            }

            bool inside = false;
            for (const QRectF& area : areas)
            {
                if (area.normalized().adjusted(-margin, -margin, margin, margin).contains(point))
                {
                    inside = true;
                    break;
                }
            }

            if (inside)
            {
                continue;
            }

            // Renderers place glyphs and antialiased edges at quantized subpixel positions, a position
            // computed in a different order can be quantized differently (a shift by a fraction of
            // a pixel). So a pixel is different only, if its value is out of the range of values of
            // the horizontally neighbouring pixels of the other image.
            auto isDifferent = [&](const uchar* row, const uchar* otherRow)
            {
                int minimum = 255;
                int maximum = 0;
                for (int du2 = -1; du2 <= 1; ++du2)
                {
                    const int uu = qBound(0, u + du2, before.width() - 1);
                    minimum = qMin(minimum, int(otherRow[uu]));
                    maximum = qMax(maximum, int(otherRow[uu]));
                }
                return int(row[u]) < minimum - 48 || int(row[u]) > maximum + 48;
            };

            if (isDifferent(rowBefore, rowAfter) || isDifferent(rowAfter, rowBefore))
            {
                ++differences;
            }
        }
    }
    return differences;
}

bool RedactionTest::containsString(const QByteArray& data, const QByteArray& string)
{
    return data.contains(string) || data.contains(string.toHex()) || data.contains(string.toHex().toUpper());
}

void RedactionTest::checkSecretsRemoved(const RedactionResult& result, const QList<QByteArray>& secrets)
{
    QVERIFY(result.ok);
    QVERIFY(qpdfCheck(result.outputFile));

    const QByteArray source = decompressed(result.sourceFile);
    const QByteArray output = decompressed(result.outputFile);
    const QString sourceText = extractText(result.sourceFile);
    const QString outputText = extractText(result.outputFile);
    QVERIFY(!output.isEmpty());

    for (const QByteArray& secret : secrets)
    {
        // The test itself must be valid - the secret is in the source
        QVERIFY2(containsString(source, secret) || sourceText.contains(QString::fromLatin1(secret)), secret.constData());
        QVERIFY2(!containsString(output, secret), (QByteArray("Secret in the file: ") + secret).constData());
        QVERIFY2(!outputText.contains(QString::fromLatin1(secret)), (QByteArray("Secret in the text: ") + secret).constData());
    }
}

void RedactionTest::checkTextKept(const RedactionResult& result, const QStringList& words, int page)
{
    const QString text = extractText(result.outputFile, page);
    for (const QString& word : words)
    {
        QVERIFY2(text.contains(word), qPrintable(QString("Missing text '%1' in '%2'").arg(word, text)));
    }
}

void RedactionTest::checkRendering(const RedactionResult& result, const std::vector<PageSpec>& pages)
{
    for (size_t i = 0; i < pages.size(); ++i)
    {
        QImage before = render(result.sourceFile, int(i + 1));
        QImage after = render(result.outputFile, int(i + 1));
        QVERIFY(!before.isNull());
        const int differences = countDifferencesOutside(before, after, pages[i].areas, pages[i].size, pages[i].rotate);
        if (differences != 0)
        {
            before.save(result.outputFile + QString("_before%1.png").arg(i + 1));
            after.save(result.outputFile + QString("_after%1.png").arg(i + 1));
        }
        QVERIFY2(differences == 0, qPrintable(QString("Page %1: %2 pixels differ outside of the areas (%3)").arg(i + 1).arg(differences).arg(result.outputFile)));
    }
}

#define REQUIRE_ENVIRONMENT() \
    if (!m_toolsAvailable || m_dejaVuData.isEmpty()) \
    { \
        QSKIP("qpdf, pdftotext, pdftoppm or DejaVu Sans font is not available"); \
    }

// ---------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------

void RedactionTest::plainText()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    PageSpec page;
    page.content = "BT /F1 14 Tf 20 250 Td (Alpha Quixbyzor Gamma) Tj ET\n"
                   "BT /F1 14 Tf 20 200 Td (Second line stays) Tj ET\n";
    page.areas << wordArea(20, 250, "Alpha ", "Quixbyzor", 14);

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "plain");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Quixbyzor" });
    checkTextKept(result, { "Alpha", "Gamma", "Second line stays" });
    checkRendering(result, pages);
}

void RedactionTest::kernedTextArray()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    // Word is split into several strings with kerning, other words are kerned as well
    PageSpec page;
    page.content = "BT /F1 14 Tf 20 250 Td 0.5 Tc 1 Tw [(Al) -40 (pha Qui) 25 (xb) -10 (yzor) 30 ( Gam) 15 (ma)] TJ ET\n";

    // Position of the word - the kerning and spacing of the words before it
    const PDFReal start = 20 + (width("Alpha ", 14) + 6 * 0.5 + 1 + 40 * 0.014);
    const PDFReal end = start + width("Quixbyzor", 14) + 9 * 0.5 - (25 + -10) * 0.014;
    page.areas << QRectF(QPointF(start - 1, 250 - 4), QPointF(end + 1, 250 + 14));

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "kerned");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Quixbyzor", "Qui", "yzor" });
    checkTextKept(result, { "Alpha", "Gamma" });
    checkRendering(result, pages);
}

void RedactionTest::textInFormXObject()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);
    const int form = pdf.add(RawPdf::stream("/Type /XObject /Subtype /Form /BBox [0 0 300 50] /Resources << /Font << /F9 " + RawPdf::ref(font) + " >> >>",
                                            "BT /F9 12 Tf 5 10 Td (Delta Vexmordak Epsilon) Tj ET"));
    // Nested form, which uses the first form
    const int outerForm = pdf.add(RawPdf::stream("/Type /XObject /Subtype /Form /BBox [0 0 400 100] /Resources << /XObject << /Inner " + RawPdf::ref(form) + " >> >>",
                                                 "q 1 0 0 1 10 40 cm /Inner Do Q"));

    PageSpec page;
    page.content = "q 1 0 0 1 40 100 cm /Fm1 Do Q\nq 1 0 0 1 0 0 cm /Outer Do Q\n";
    page.resources = "/XObject << /Fm1 " + RawPdf::ref(form) + " /Outer " + RawPdf::ref(outerForm) + " >>";
    page.areas << wordArea(40 + 5, 100 + 10, "Delta ", "Vexmordak", 12);
    page.areas << wordArea(10 + 5, 40 + 10, "Delta ", "Vexmordak", 12);

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "form");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Vexmordak" });
    checkTextKept(result, { "Delta", "Epsilon" });
    checkRendering(result, pages);
}

void RedactionTest::invisibleText()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    // OCR-like invisible text layer (rendering mode 3) with a horizontal scaling
    PageSpec page;
    page.content = "BT 3 Tr /F1 20 Tf 80 Tz 30 150 Td (Hidden Wobbletrask Layer) Tj ET\n"
                   "BT /F1 14 Tf 30 250 Td (Visible words) Tj ET\n";
    const PDFReal start = 30 + width("Hidden ", 20) * 0.8;
    const PDFReal end = start + width("Wobbletrask", 20) * 0.8;
    page.areas << QRectF(QPointF(start - 1, 150 - 5), QPointF(end + 1, 150 + 18));

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "invisible");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Wobbletrask" });
    checkTextKept(result, { "Hidden", "Layer", "Visible words" });
    checkRendering(result, pages);
}

void RedactionTest::wordSplitAcrossOperators()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    // The word is split into Tj, ' (next line) and " operators and two text objects
    PageSpec page;
    page.content = "BT /F1 14 Tf 16 TL 20 250 Td (Begin Xan) Tj (tho) Tj (porex end) Tj ET\n"
                   "BT /F1 14 Tf 16 TL 20 216 Td (Line one) Tj (Krumpelfax line two) ' 0 0.5 (Zorbiquent three) \" ET\n";
    const PDFReal start = 20 + width("Begin ", 14);
    const PDFReal end = start + width("Xanthoporex", 14);
    page.areas << QRectF(QPointF(start - 1, 250 - 4), QPointF(end + 1, 250 + 14));
    page.areas << wordArea(20, 200, "", "Krumpelfax", 14);
    page.areas << QRectF(QPointF(19, 184 - 4), QPointF(20 + width("Zorbiquent", 14) + 10 * 0.5 + 1, 184 + 14));

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "split");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Xanthoporex", "Xan", "porex", "Krumpelfax", "Zorbiquent" });
    checkTextKept(result, { "Begin", "end", "Line one", "line two", "three" });
    checkRendering(result, pages);
}

void RedactionTest::overlappingAreas()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    // Two overlapping areas - the word is exactly in their overlap. An odd-even union
    // would treat the overlap as outside of the area and keep the word.
    PageSpec page;
    page.content = "BT /F1 14 Tf 20 250 Td (Keep Overlapzed Keep2) Tj ET\n"
                   "BT /F1 14 Tf 20 230 Td (Plumbarix lower) Tj ET\n";
    const QRectF word = wordArea(20, 250, "Keep ", "Overlapzed", 14);
    page.areas << word.adjusted(0, 0, 0, 6);
    page.areas << word.adjusted(0, -2, 0, 0);
    page.areas << wordArea(20, 230, "", "Plumbarix", 14);

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "overlap");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Overlapzed", "Plumbarix" });
    checkTextKept(result, { "Keep", "Keep2", "lower" });
    checkRendering(result, pages);
}

void RedactionTest::standardFontWithoutWidths()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);
    const int helvetica = addHelveticaFont(pdf);

    // Helvetica widths (Adobe font metrics): Alpha + space
    PageSpec page;
    page.content = "BT /F2 14 Tf 20 250 Td (Alpha Quorvexil Gamma) Tj ET\n";
    const PDFReal start = 20 + (667 + 222 + 556 + 556 + 556 + 278) * 0.014;
    const PDFReal end = start + (778 + 556 + 556 + 333 + 500 + 556 + 500 + 222 + 222) * 0.014;
    page.areas << QRectF(QPointF(start - 1, 250 - 4), QPointF(end + 1, 250 + 14));

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font, helvetica), "helvetica");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Quorvexil" });
    checkTextKept(result, { "Alpha", "Gamma" });
    checkRendering(result, pages);
}

namespace
{

QByteArray createImageData(int width, int height, int seed)
{
    QByteArray data;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            // Bright image with a pattern
            data.append(char(180 + (x * 7 + seed) % 60));
            data.append(char(200 + (y * 5) % 50));
            data.append(char(190 + ((x + y) * 3) % 60));
        }
    }
    return data;
}

}   // namespace

void RedactionTest::imageHalfCovered()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);
    const int image = pdf.add(RawPdf::stream("/Type /XObject /Subtype /Image /Width 100 /Height 50 /ColorSpace /DeviceRGB /BitsPerComponent 8",
                                             createImageData(100, 50, 0)));
    // Image with a soft mask (the mask is redacted too)
    QByteArray maskData(40 * 20, char(255));
    const int softMask = pdf.add(RawPdf::stream("/Type /XObject /Subtype /Image /Width 40 /Height 20 /ColorSpace /DeviceGray /BitsPerComponent 8", maskData));
    const int maskedImage = pdf.add(RawPdf::stream("/Type /XObject /Subtype /Image /Width 40 /Height 20 /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask " + RawPdf::ref(softMask),
                                                   createImageData(40, 20, 3)));

    PageSpec page;
    page.content = "q 200 0 0 100 100 100 cm /Im1 Do Q\nq 80 0 0 40 20 20 cm /Im2 Do Q\n";
    page.resources = "/XObject << /Im1 " + RawPdf::ref(image) + " /Im2 " + RawPdf::ref(maskedImage) + " >>";
    page.areas << QRectF(QPointF(80, 90), QPointF(200, 210));      // left half of the image
    page.areas << QRectF(QPointF(10, 10), QPointF(60, 70));         // left half of the masked image

    std::vector<PageSpec> pages = { page };

    // Without the fill, the covered pixels must be overwritten (black) in the image itself
    RedactionResult noFill = redact(buildDocument(pdf, pages, font), "image_nofill", true, QColor());
    QVERIFY(noFill.ok);
    QVERIFY(noFill.messages.isEmpty());
    QVERIFY(qpdfCheck(noFill.outputFile));
    checkRendering(noFill, pages);

    const QImage after = render(noFill.outputFile, 1);
    // Pixel in the covered half of the image (PDF point 130, 150 -> pixel 130, 150)
    QVERIFY2(after.pixelColor(130, 300 - 150).value() < 30, "Covered pixels of the image are not overwritten");
    // Pixel in the uncovered half of the image
    QVERIFY(after.pixelColor(250, 300 - 150).value() > 150);
    // Masked image
    QVERIFY2(after.pixelColor(30, 300 - 40).value() < 30 || after.pixelColor(30, 300 - 40).value() > 250, "Covered pixels of the masked image are not overwritten");
    QVERIFY(after.pixelColor(80, 300 - 40).value() > 150);

    RedactionResult filled = redact(buildDocument(pdf, pages, font), "image");
    QVERIFY(filled.ok);
    checkRendering(filled, pages);
}

void RedactionTest::inlineImage()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    PageSpec page;
    page.content = "q 200 0 0 100 100 100 cm BI /W 20 /H 10 /CS /RGB /BPC 8 ID\n" + createImageData(20, 10, 1) + "\nEI Q\n"
                   "q 100 0 0 50 10 10 cm BI /W 10 /H 5 /CS /G /BPC 8 /F /AHx ID\n" + QByteArray(50, char(200)).toHex() + ">\nEI Q\n"
                   "BT /F1 14 Tf 20 250 Td (After images) Tj ET\n";
    page.areas << QRectF(QPointF(80, 90), QPointF(200, 210));

    std::vector<PageSpec> pages = { page };
    RedactionResult noFill = redact(buildDocument(pdf, pages, font), "inline_nofill", true, QColor());
    QVERIFY(noFill.ok);
    QVERIFY(noFill.messages.isEmpty());
    QVERIFY(qpdfCheck(noFill.outputFile));
    checkRendering(noFill, pages);
    checkTextKept(noFill, { "After images" });

    const QImage after = render(noFill.outputFile, 1);
    QVERIFY2(after.pixelColor(130, 300 - 150).value() < 30, "Covered pixels of the inline image are not overwritten");
    QVERIFY(after.pixelColor(250, 300 - 150).value() > 150);
    QVERIFY(after.pixelColor(50, 300 - 30).value() > 150);
}

void RedactionTest::sharedImageOnTwoPages()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);
    const int image = pdf.add(RawPdf::stream("/Type /XObject /Subtype /Image /Width 100 /Height 50 /ColorSpace /DeviceRGB /BitsPerComponent 8",
                                             createImageData(100, 50, 0)));

    PageSpec page1;
    page1.content = "q 200 0 0 100 100 100 cm /Im1 Do Q\n";
    page1.resources = "/XObject << /Im1 " + RawPdf::ref(image) + " >>";
    page1.areas << QRectF(QPointF(80, 90), QPointF(200, 210));

    PageSpec page2 = page1;
    page2.areas.clear();

    std::vector<PageSpec> pages = { page1, page2 };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "shared", true, QColor());
    QVERIFY(result.ok);
    QVERIFY(result.messages.isEmpty());
    QVERIFY(qpdfCheck(result.outputFile));
    checkRendering(result, pages);

    const QImage first = render(result.outputFile, 1);
    const QImage second = render(result.outputFile, 2);
    QVERIFY(first.pixelColor(130, 150).value() < 30);
    QVERIFY2(second.pixelColor(130, 150).value() > 150, "Image on the page without redaction was changed");
}

void RedactionTest::rotatedPage()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    PageSpec page;
    page.rotate = 90;
    page.content = "BT /F1 14 Tf 20 250 Td (Alpha Rotaquintex Gamma) Tj ET\n"
                   "BT /F1 14 Tf 0 1 -1 0 300 20 Tm (Vertical Sidewinderx stays) Tj ET\n";
    page.areas << wordArea(20, 250, "Alpha ", "Rotaquintex", 14);
    // Word in the text rotated by 90 degrees
    const PDFReal start = 20 + width("Vertical ", 14);
    const PDFReal end = start + width("Sidewinderx", 14);
    page.areas << QRectF(QPointF(300 - 14, start - 1), QPointF(300 + 5, end + 1));

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "rotated");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Rotaquintex", "Sidewinderx" });
    checkTextKept(result, { "Alpha", "Gamma", "Vertical", "stays" });
    checkRendering(result, pages);
}

void RedactionTest::vectorGraphics()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    // Filled rectangle and a thick stroked line, both crossing the area, a curve
    // (signature-like) fully inside, a dashed line and a clip passing through the area
    PageSpec page;
    page.content = "0 0 0 rg 50 100 300 40 re f\n"
                   "0 0 0 RG 6 w 50 200 m 350 200 l S\n"
                   "1 0 0 RG 2 w 160 230 m 170 260 190 220 200 250 c 210 280 220 230 235 245 c S\n"
                   "0 0 1 RG 3 w [6 4] 0 d 50 60 m 350 60 l S [] 0 d\n"
                   "q 150 40 100 40 re W n 0 0 0 rg 0 0 400 300 re f Q\n";
    page.areas << QRectF(QPointF(150, 50), QPointF(250, 270));

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "vector", true, QColor());
    QVERIFY(result.ok);
    QVERIFY(result.messages.isEmpty());
    QVERIFY(qpdfCheck(result.outputFile));
    checkRendering(result, pages);

    // Inside the area everything is removed (white), the area is not filled
    const QImage after = render(result.outputFile, 1);
    QVERIFY(after.pixelColor(200, 300 - 120).value() > 230);
    QVERIFY(after.pixelColor(200, 300 - 200).value() > 230);
    QVERIFY(after.pixelColor(190, 300 - 250).value() > 230);
    QVERIFY(after.pixelColor(100, 300 - 120).value() < 30);

    // The curve inside of the area must be gone from the file (its control points)
    const QByteArray output = decompressed(result.outputFile);
    QVERIFY(!output.contains("170 260 190 220"));
    QVERIFY(!output.contains("210 280 220 230"));
}

void RedactionTest::clippingText()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    // Clipping text (mode 7) - the glyphs are the shape of the painted rectangle
    PageSpec page;
    page.content = "q BT 7 Tr /F1 30 Tf 20 150 Td (Clipword Kept) Tj ET 0 0 0 rg 0 0 400 300 re f Q\n"
                   "BT 4 Tr /F1 14 Tf 20 250 Td (Fillclipz visible) Tj ET\n";
    page.areas << wordArea(20, 150, "", "Clipword", 30);
    page.areas << wordArea(20, 250, "", "Fillclipz", 14);

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "clip");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Clipword", "Fillclipz" });
    checkTextKept(result, { "Kept", "visible" });
    checkRendering(result, pages);
}

void RedactionTest::markedContentText()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);
    const int properties = pdf.add("<< /ActualText (Nebulorant) /Lang (en) >>");

    PageSpec page;
    page.content = "/Span << /ActualText (Mysterion) /Alt (Altisecret) >> BDC BT /F1 14 Tf 20 250 Td (Kozmikor) Tj ET EMC\n"
                   "/Span /P1 BDC BT /F1 14 Tf 20 200 Td (Pantoflex) Tj ET EMC\n"
                   "BT /F1 14 Tf 20 150 Td (Kept words) Tj ET\n";
    page.resources = "/Properties << /P1 " + RawPdf::ref(properties) + " >>";
    page.areas << wordArea(20, 250, "", "Kozmikor", 14);
    page.areas << wordArea(20, 200, "", "Pantoflex", 14);

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "marked");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Kozmikor", "Mysterion", "Altisecret", "Pantoflex", "Nebulorant" });
    checkTextKept(result, { "Kept words" });
    checkRendering(result, pages);
}

void RedactionTest::annotationsAndFields()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    auto fieldAppearance = [&](const QByteArray& text)
    {
        return pdf.add(RawPdf::stream("/Type /XObject /Subtype /Form /BBox [0 0 150 20] /Resources << /Font << /F1 " + RawPdf::ref(font) + " >> >>",
                                      "/Tx BMC BT /F1 12 Tf 2 5 Td (" + text + ") Tj ET EMC"));
    };

    const int keptAppearance = fieldAppearance("Keepfield");
    const int secretAppearance = fieldAppearance("Widgetsecret");
    const int keptField = pdf.add("<< /Type /Annot /Subtype /Widget /FT /Tx /T (kept) /V (Keepfield) /Rect [20 20 170 40] /F 4 /AP << /N " + RawPdf::ref(keptAppearance) + " >> >>");
    const int secretField = pdf.add("<< /Type /Annot /Subtype /Widget /FT /Tx /T (secret) /V (Widgetsecret) /Rect [200 200 350 220] /F 4 /AP << /N " + RawPdf::ref(secretAppearance) + " >> >>");
    const int note = pdf.add("<< /Type /Annot /Subtype /FreeText /Rect [200 100 350 130] /Contents (Secretnote) /DA (/F1 10 Tf 0 g) >>");
    const int link = pdf.add("<< /Type /Annot /Subtype /Link /Rect [20 250 120 270] /A << /S /URI /URI (https://example.org/kept) >> >>");
    const int script = pdf.add("<< /Type /Annot /Subtype /Link /Rect [20 150 120 170] /A << /S /JavaScript /JS (app.alert\\(1\\)) >> >>");
    const int comment = pdf.add("<< /Type /Annot /Subtype /Text /Rect [20 60 40 80] /Contents (Comment kept) >>");

    PageSpec page;
    page.content = "BT /F1 14 Tf 20 280 Td (Page text) Tj ET\n";
    page.annotations = RawPdf::ref(keptField) + ' ' + RawPdf::ref(secretField) + ' ' + RawPdf::ref(note) + ' ' + RawPdf::ref(link) + ' ' + RawPdf::ref(script) + ' ' + RawPdf::ref(comment);
    page.areas << QRectF(QPointF(190, 90), QPointF(360, 230));

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font, 0, "/AcroForm << /Fields [" + RawPdf::ref(keptField) + ' ' + RawPdf::ref(secretField) + "] >>"), "annotations");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Widgetsecret", "Secretnote" });
    checkTextKept(result, { "Keepfield", "Page text" });

    const QByteArray output = decompressed(result.outputFile);
    QVERIFY(output.contains("https://example.org/kept") || containsString(output, "https://example.org/kept"));
    QVERIFY(!output.contains("JavaScript"));
    QVERIFY(containsString(output, "Comment kept"));
    QVERIFY(!output.contains("/Redact"));
}

void RedactionTest::type3TextIsConvertedToOutlines()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);
    const int glyph = pdf.add(RawPdf::stream(QByteArray(), "500 0 0 0 400 600 d1 0 0 400 600 re f"));
    const int toUnicode = pdf.add(RawPdf::stream(QByteArray(),
        "/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CMapName /T3 def 1 begincodespacerange <00> <FF> endcodespacerange "
        "1 beginbfrange <41> <41> <0058> endbfrange endcmap CMapName currentdict /CMap defineresource pop end end"));
    const int type3 = pdf.add("<< /Type /Font /Subtype /Type3 /FontBBox [0 0 400 600] /FontMatrix [0.001 0 0 0.001 0 0] /CharProcs << /a " + RawPdf::ref(glyph) +
                              " >> /Encoding << /Type /Encoding /Differences [65 /a] >> /FirstChar 65 /LastChar 65 /Widths [500] /ToUnicode " + RawPdf::ref(toUnicode) + " >>");

    PageSpec page;
    page.content = "BT /F2 14 Tf 20 250 Td (AAAA) Tj ET\nBT /F1 14 Tf 20 200 Td (Typethreesecret) Tj ET\n";
    page.areas << QRectF(QPointF(15, 240), QPointF(60, 270));
    page.areas << wordArea(20, 200, "", "Typethreesecret", 14);

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font, type3), "type3");
    QVERIFY(!result.messages.isEmpty());
    checkSecretsRemoved(result, { "Typethreesecret" });
    QVERIFY(!extractText(result.outputFile).contains("XXXX"));
}

void RedactionTest::outlinesModeStillWorks()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    PageSpec page;
    page.content = "BT /F1 14 Tf 20 250 Td (Alpha Quixbyzor Gamma) Tj ET\n";
    page.areas << wordArea(20, 250, "Alpha ", "Quixbyzor", 14);

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "outlines", false);
    checkSecretsRemoved(result, { "Quixbyzor" });
    // Outlines: no text at all
    QVERIFY(!extractText(result.outputFile).contains("Alpha"));
}

void RedactionTest::compositeFontFromQt()
{
    REQUIRE_ENVIRONMENT();

    // Qt writes text in a composite font (Type 0, Identity-H, two bytes per glyph)
    const QString sourceFile = m_directory.filePath("qt_source.pdf");
    {
        QPdfWriter writer(sourceFile);
        writer.setPageSize(QPageSize(QSizeF(400, 300), QPageSize::Point));
        writer.setResolution(72);
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter painter(&writer);
        QFont font("DejaVu Sans");
        font.setPixelSize(14);
        painter.setFont(font);
        painter.drawText(QPointF(20, 50), "Alpha Cidsecretword Gamma");
        painter.drawText(QPointF(20, 80), "Another line kept");
        painter.end();
    }

    // Position of the word from Poppler (top-left origin)
    QProcess process;
    process.start("pdftotext", { "-bbox", sourceFile, "-" });
    process.waitForFinished(60000);
    const QString boxes = QString::fromUtf8(process.readAllStandardOutput());
    QRegularExpression expression("xMin=\"([0-9.]+)\" yMin=\"([0-9.]+)\" xMax=\"([0-9.]+)\" yMax=\"([0-9.]+)\">Cidsecretword<");
    QRegularExpressionMatch match = expression.match(boxes);
    QVERIFY2(match.hasMatch(), qPrintable(boxes));
    const QRectF area(QPointF(match.captured(1).toDouble() - 1, 300 - match.captured(4).toDouble() - 1),
                      QPointF(match.captured(3).toDouble() + 1, 300 - match.captured(2).toDouble() + 1));

    QFile file(sourceFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(file.readAll());
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    PDFDocumentBuilder builder(&document);
    builder.createAnnotationRedact(document.getCatalog()->getPage(0)->getPageReference(), area, Qt::black, Qt::red);
    PDFDocument marked = builder.build();
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    PDFDocumentWriter(nullptr).write(&buffer, &marked);

    PageSpec page;
    page.areas << area;
    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buffer.data(), "qt");
    QVERIFY(result.messages.isEmpty());
    QVERIFY(result.ok);
    QVERIFY(qpdfCheck(result.outputFile));
    QVERIFY(extractText(result.sourceFile).contains("Cidsecretword"));
    QVERIFY(!extractText(result.outputFile).contains("Cidsecretword"));
    QVERIFY(!extractText(result.outputFile).contains("secret"));
    checkTextKept(result, { "Alpha", "Gamma", "Another line kept" });
    checkRendering(result, pages);
}

void RedactionTest::hiddenOptionalContent()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);
    const int layer = pdf.add("<< /Type /OCG /Name (Hidden layer) >>");

    // Text in a layer, which is hidden - it must be removed as well (it can be shown)
    PageSpec page;
    page.content = "/OC /L1 BDC BT /F1 14 Tf 20 250 Td (Layersecretz) Tj ET EMC\n"
                   "BT /F1 14 Tf 20 200 Td (Visible kept) Tj ET\n";
    page.resources = "/Properties << /L1 " + RawPdf::ref(layer) + " >>";
    page.areas << wordArea(20, 250, "", "Layersecretz", 14);

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font, 0, "/OCProperties << /OCGs [" + RawPdf::ref(layer) + "] /D << /OFF [" + RawPdf::ref(layer) + "] >> >>"), "layer");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Layersecretz" });
    checkTextKept(result, { "Visible kept" });
    checkRendering(result, pages);

    // The layer stays hidden
    QVERIFY(decompressed(result.outputFile).contains("/OCProperties"));
}

void RedactionTest::imagesInFormAndOtherFormats()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    // 1 bit image (bright with dark stripes), indexed image, 16 bit gray image, image in a form
    QByteArray bitonal;
    for (int y = 0; y < 40; ++y)
    {
        for (int x = 0; x < 10; ++x)
        {
            bitonal.append(char(y % 8 == 0 ? 0x00 : 0xFF));
        }
    }
    const int image1 = pdf.add(RawPdf::stream("/Type /XObject /Subtype /Image /Width 80 /Height 40 /ColorSpace /DeviceGray /BitsPerComponent 1", bitonal));
    const int image2 = pdf.add(RawPdf::stream("/Type /XObject /Subtype /Image /Width 4 /Height 2 /ColorSpace [/Indexed /DeviceRGB 1 <DDDDDD 202020>] /BitsPerComponent 8",
                                              QByteArray(8, char(1))));
    QByteArray gray16;
    for (int i = 0; i < 20 * 10; ++i)
    {
        gray16.append(char(0xE0));
        gray16.append(char(0x00));
    }
    const int image3 = pdf.add(RawPdf::stream("/Type /XObject /Subtype /Image /Width 20 /Height 10 /ColorSpace /DeviceGray /BitsPerComponent 16", gray16));
    const int form = pdf.add(RawPdf::stream("/Type /XObject /Subtype /Form /BBox [0 0 200 100] /Resources << /XObject << /Im " + RawPdf::ref(image1) + " >> >>",
                                            "q 160 0 0 80 20 10 cm /Im Do Q"));

    PageSpec page;
    page.content = "q 1 0 0 1 10 10 cm /Fm Do Q\n"
                   "q 160 0 0 80 220 10 cm /Ix Do Q\n"
                   "q 160 0 0 80 220 150 cm /G16 Do Q\n";
    page.resources = "/XObject << /Fm " + RawPdf::ref(form) + " /Ix " + RawPdf::ref(image2) + " /G16 " + RawPdf::ref(image3) + " >>";
    page.areas << QRectF(QPointF(0, 0), QPointF(110, 100));     // left half of the image in the form
    page.areas << QRectF(QPointF(200, 0), QPointF(300, 100));   // left half of the indexed image
    page.areas << QRectF(QPointF(200, 140), QPointF(300, 240)); // left half of the 16 bit image

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "formats", true, QColor());
    QVERIFY(result.ok);
    QVERIFY2(result.messages.isEmpty(), qPrintable(result.messages.join('\n')));
    QVERIFY(qpdfCheck(result.outputFile));
    checkRendering(result, pages);

    const QImage after = render(result.outputFile, 1);
    QVERIFY(after.pixelColor(60, 300 - 55).value() < 30);     // bitonal image, covered (zero = black)
    QVERIFY(after.pixelColor(150, 300 - 53).value() > 200);   // bitonal image, not covered
    QVERIFY(after.pixelColor(260, 300 - 50).value() > 200);   // indexed image, covered (dark index 1 overwritten by bright index 0)
    QVERIFY(after.pixelColor(350, 300 - 50).value() < 60);    // indexed image, not covered
    QVERIFY(after.pixelColor(260, 300 - 190).value() < 30);   // 16 bit image, covered
    QVERIFY(after.pixelColor(350, 300 - 190).value() > 150);  // 16 bit image, not covered
}

void RedactionTest::tilingPatternAndSoftMaskFallBack()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);
    const int pattern = pdf.add(RawPdf::stream("/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 200 40] /XStep 200 /YStep 40 "
                                               "/Resources << /Font << /F1 " + RawPdf::ref(font) + " >> >>",
                                               "BT /F1 12 Tf 5 10 Td (Patternsecret) Tj ET"));
    const int softMaskForm = pdf.add(RawPdf::stream("/Type /XObject /Subtype /Form /BBox [0 0 400 300] /Group << /S /Transparency /CS /DeviceGray >> "
                                                    "/Resources << /Font << /F1 " + RawPdf::ref(font) + " >> >>",
                                                    "1 g 0 0 400 300 re f BT 0 g /F1 14 Tf 20 100 Td (Masksecret) Tj ET"));

    PageSpec page1;
    page1.content = "/Pattern cs /P1 scn 20 200 300 60 re f\nBT /F1 14 Tf 20 150 Td (Text on page) Tj ET\n";
    page1.resources = "/Pattern << /P1 " + RawPdf::ref(pattern) + " >>";
    page1.areas << QRectF(QPointF(10, 190), QPointF(150, 270));

    PageSpec page2;
    page2.content = "q /GS1 gs 0 0 1 rg 0 0 400 300 re f Q\nBT /F1 14 Tf 20 250 Td (Another page) Tj ET\n";
    page2.resources = "/ExtGState << /GS1 << /Type /ExtGState /SMask << /S /Luminosity /G " + RawPdf::ref(softMaskForm) + " >> >> >>";
    page2.areas << QRectF(QPointF(10, 90), QPointF(150, 130));

    std::vector<PageSpec> pages = { page1, page2 };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "fallback");
    QCOMPARE(result.messages.size(), 2);
    checkSecretsRemoved(result, { "Patternsecret", "Masksecret" });
}

void RedactionTest::widthsDifferentFromFontProgram()
{
    REQUIRE_ENVIRONMENT();

    // Widths in the font dictionary (1000) differ from the glyphs of the font program (about 600).
    // Viewers place glyphs by the widths - the redaction must test the same positions.
    RawPdf pdf;
    const int fontFile = pdf.add(RawPdf::stream("/Length1 " + QByteArray::number(m_dejaVuData.size()), m_dejaVuData));
    const int descriptor = pdf.add("<< /Type /FontDescriptor /FontName /DejaVuSans /Flags 32 /FontBBox [-1021 -463 1793 1232] "
                                   "/ItalicAngle 0 /Ascent 928 /Descent -236 /CapHeight 729 /StemV 80 /FontFile2 " + RawPdf::ref(fontFile) + " >>");
    QByteArray widths;
    for (int i = 32; i <= 126; ++i)
    {
        widths += "1000 ";
    }
    const int font = pdf.add("<< /Type /Font /Subtype /TrueType /BaseFont /DejaVuSans /FirstChar 32 /LastChar 126 /Widths [" + widths +
                             "] /Encoding /WinAnsiEncoding /FontDescriptor " + RawPdf::ref(descriptor) + " >>");

    PageSpec page;
    page.content = "BT /F1 10 Tf 20 250 Td (AAAA) Tj (Widesecret) Tj ( after) Tj ET\n";
    page.areas << QRectF(QPointF(20 + 40 - 1, 245), QPointF(20 + 140 + 1, 262));

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "widths");
    QVERIFY(result.messages.isEmpty());
    checkSecretsRemoved(result, { "Widesecret" });
    checkTextKept(result, { "AAAA", "after" });
    checkRendering(result, pages);
}

void RedactionTest::inconsistentContentFallsBack()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    // A form, which restores more states than it saved
    const int form = pdf.add(RawPdf::stream("/Type /XObject /Subtype /Form /BBox [0 0 400 300] /Resources << /Font << /F1 " + RawPdf::ref(font) + " >> >>",
                                            "Q BT /F1 14 Tf 20 100 Td (Formsecretq) Tj ET"));
    PageSpec page1;
    page1.content = "q 1 0 0 1 0 50 cm /Fm Do Q\n";
    page1.resources = "/XObject << /Fm " + RawPdf::ref(form) + " >>";
    page1.areas << QRectF(QPointF(10, 90), QPointF(200, 200));

    // Extra operands, which other viewers would read differently
    PageSpec page2;
    page2.content = "q 100 200 1 0 0 1 0 0 cm BT /F1 14 Tf 20 100 Td (Operandsecret) Tj ET Q\n";
    page2.areas << QRectF(QPointF(10, 90), QPointF(200, 120));

    // Data of an inline image, which other viewers end at the first EI
    PageSpec page3;
    page3.content = "q 10 0 0 10 300 20 cm BI /W 4 /H 4 /CS /G /BPC 8 /L 48 ID\n0123\nEI BT /F1 14 Tf 20 100 Td (Inlinesecret) Tj ET 0123456\nEI Q\n";
    page3.areas << QRectF(QPointF(10, 90), QPointF(200, 120));

    // Huge coordinates
    PageSpec page4;
    page4.content = "q 1 0 0 1 50000000000 0 cm 1 0 0 1 -50000000000 0 cm BT /F1 14 Tf 20 100 Td (Hugesecret) Tj ET Q\n";
    page4.areas << QRectF(QPointF(10, 90), QPointF(200, 120));

    std::vector<PageSpec> pages = { page1, page2, page3, page4 };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "inconsistent");
    QCOMPARE(result.messages.size(), 4);
    checkSecretsRemoved(result, { "Formsecretq", "Operandsecret", "Inlinesecret", "Hugesecret" });
}

void RedactionTest::clipAndUnpaintedPathsInArea()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    // A shape inside of the area is a part of a clipping path, which contains the whole area, and
    // another one is constructed, but not painted - both must not be in the file
    PageSpec page;
    page.content = "q 0 0 400 300 re 150 150 m 161.5 171.5 l 172.5 150 l h W n 0 0 1 rg 0 0 400 300 re f Q\n"
                   "152.5 155 m 163.5 177.5 l 171.5 152.5 l h n\n";
    page.areas << QRectF(QPointF(140, 140), QPointF(180, 190));

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "clippaths");
    QVERIFY(result.messages.isEmpty());
    QVERIFY(qpdfCheck(result.outputFile));
    const QByteArray output = decompressed(result.outputFile);
    QVERIFY(decompressed(result.sourceFile).contains("161.5 171.5"));
    QVERIFY(!output.contains("161.5 171.5"));
    QVERIFY(!output.contains("163.5 177.5"));
    checkRendering(result, pages);
}

void RedactionTest::axialShadingInArea()
{
    REQUIRE_ENVIRONMENT();

    RawPdf pdf;
    const int font = addDejaVuFont(pdf);

    PageSpec page;
    page.content = "q 50 50 300 200 re W n /Sh1 sh Q\nBT /F1 14 Tf 20 280 Td (Shading kept) Tj ET\n";
    page.resources = "/Shading << /Sh1 << /ShadingType 2 /ColorSpace /DeviceGray /Coords [50 0 350 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [1] /N 1 >> >> >>";
    page.areas << QRectF(QPointF(150, 100), QPointF(250, 200));

    std::vector<PageSpec> pages = { page };
    RedactionResult result = redact(buildDocument(pdf, pages, font), "shading", true, QColor());
    QVERIFY(result.messages.isEmpty());
    QVERIFY(qpdfCheck(result.outputFile));
    checkRendering(result, pages);
    checkTextKept(result, { "Shading kept" });

    // The shading is not painted in the area
    const QImage after = render(result.outputFile, 1);
    QVERIFY(after.pixelColor(170, 300 - 150).value() > 250);
    QVERIFY(after.pixelColor(100, 300 - 150).value() < 120);
}

void RedactionTest::labelOnBoxes()
{
    REQUIRE_ENVIRONMENT();

    // The light pixels (the label) inside of the box, as seen on the screen
    auto getLabelBox = [this](const QString& fileName, QRect displayedBox)
    {
        const QImage image = render(fileName, 1);
        QRect labelBox;
        int count = 0;
        const QRect inside = displayedBox.adjusted(3, 3, -3, -3);
        for (int v = inside.top(); v <= inside.bottom(); ++v)
        {
            for (int u = inside.left(); u <= inside.right(); ++u)
            {
                if (image.pixelColor(u, v).lightness() > 200)
                {
                    labelBox |= QRect(u, v, 1, 1);
                    ++count;
                }
            }
        }
        return std::make_pair(labelBox, count);
    };

    for (const int rotate : { 0, 90 })
    {
        for (const bool keepText : { true, false })
        {
            RawPdf pdf;
            const int font = addDejaVuFont(pdf);

            PageSpec page;
            page.rotate = rotate;
            page.content = "BT /F1 14 Tf 20 250 Td (Alpha Gamma) Tj ET\n"
                           "BT /F1 14 Tf 60 150 Td (Labelsecretx) Tj ET\n";
            const QRectF bigBox(QPointF(40, 100), QPointF(360, 200));
            page.areas << bigBox;
            page.areas << QRectF(QPointF(20, 30), QPointF(200, 32)); // 2 points - too thin for a label

            // Page 400 x 300; turned by 90 degrees clockwise on the screen it is 300 x 400, u = y, v = x
            const QRect displayedBox = rotate == 0 ? QRect(QPoint(40, 300 - 200), QPoint(360, 300 - 100))
                                                   : QRect(QPoint(100, 40), QPoint(200, 360));
            const QRect displayedThinBox = rotate == 0 ? QRect(QPoint(20, 300 - 32), QPoint(200, 300 - 30))
                                                       : QRect(QPoint(30, 20), QPoint(32, 200));

            std::vector<PageSpec> pages = { page };
            const QString name = QString("label_%1_%2").arg(rotate).arg(keepText ? "text" : "outlines");
            RedactionResult result = redact(buildDocument(pdf, pages, font), name, keepText, Qt::black, "REDACTED", Qt::white);
            QVERIFY(result.ok);
            checkSecretsRemoved(result, { "Labelsecretx" });
            // The label is drawn as shapes - it is not text of the page
            QVERIFY(!extractText(result.outputFile).contains("REDACTED"));
            if (keepText)
            {
                checkTextKept(result, { "Alpha", "Gamma" });
            }

            const auto [labelBox, count] = getLabelBox(result.outputFile, displayedBox);
            QVERIFY2(count > 100, qPrintable(QString("label pixels: %1").arg(count)));
            // Upright on the screen: wider than tall, and centred in the box
            QVERIFY2(labelBox.width() > 2 * labelBox.height(), qPrintable(QString("label %1x%2").arg(labelBox.width()).arg(labelBox.height())));
            QVERIFY(qAbs(labelBox.center().x() - displayedBox.center().x()) <= 3);
            QVERIFY(qAbs(labelBox.center().y() - displayedBox.center().y()) <= 3);
            QVERIFY(labelBox.width() <= displayedBox.width() * 0.92);

            // The thin box has no label (stays black)
            const QImage image = render(result.outputFile, 1);
            const QPoint thinCenter = displayedThinBox.center();
            QVERIFY(image.pixelColor(thinCenter).lightness() < 60);

            // A plain box has no label
            RawPdf plainPdf;
            const int plainFont = addDejaVuFont(plainPdf);
            RedactionResult plain = redact(buildDocument(plainPdf, pages, plainFont), name + "_plain", keepText, Qt::black);
            QCOMPARE(getLabelBox(plain.outputFile, displayedBox).second, 0);

            // Red label: red pixels in the box
            RawPdf redPdf;
            const int redFont = addDejaVuFont(redPdf);
            RedactionResult red = redact(buildDocument(redPdf, pages, redFont), name + "_red", keepText, Qt::black, "REDACTED", QColor(220, 0, 0));
            const QString prefix = red.outputFile + "_color";
            QProcess::execute("pdftoppm", { "-r", "72", "-png", "-hide-annotations", "-singlefile", red.outputFile, prefix });
            const QImage colorImage(prefix + ".png");
            int redCount = 0;
            for (int v = displayedBox.top() + 3; v < displayedBox.bottom() - 3; ++v)
            {
                for (int u = displayedBox.left() + 3; u < displayedBox.right() - 3; ++u)
                {
                    const QColor color = colorImage.pixelColor(u, v);
                    redCount += color.red() > 150 && color.green() < 60 && color.blue() < 60 ? 1 : 0;
                }
            }
            QVERIFY2(redCount > 100, qPrintable(QString("red pixels: %1").arg(redCount)));
        }
    }
}

void RedactionTest::redactFileFromEnvironment()
{
    // Redaction of a real document for manual checks: PDFFIRE_REDACT_INPUT (a COPY of the document!),
    // PDFFIRE_REDACT_OUTPUT and PDFFIRE_REDACT_AREAS = "page:x0,y0,x1,y1;..." (1-based pages, PDF coordinates)
    const QString input = qEnvironmentVariable("PDFFIRE_REDACT_INPUT");
    const QString output = qEnvironmentVariable("PDFFIRE_REDACT_OUTPUT");
    const QString areas = qEnvironmentVariable("PDFFIRE_REDACT_AREAS");
    if (input.isEmpty() || output.isEmpty())
    {
        QSKIP("PDFFIRE_REDACT_INPUT / PDFFIRE_REDACT_OUTPUT not set");
    }

    QFile file(input);
    QVERIFY(file.open(QIODevice::ReadOnly));
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(file.readAll());
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    PDFDocumentBuilder builder(&document);
    for (const QString& area : areas.split(';', Qt::SkipEmptyParts))
    {
        const QStringList parts = area.split(':');
        QCOMPARE(parts.size(), 2);
        const QStringList numbers = parts[1].split(',');
        QCOMPARE(numbers.size(), 4);
        const PDFPage* page = document.getCatalog()->getPage(size_t(parts[0].toInt() - 1));
        QVERIFY(page);
        const QRectF rectangle(QPointF(numbers[0].toDouble(), numbers[1].toDouble()), QPointF(numbers[2].toDouble(), numbers[3].toDouble()));
        builder.createAnnotationRedact(page->getPageReference(), rectangle.normalized(), Qt::black, Qt::red);
    }
    PDFDocument marked = builder.build();

    const QString markedFile = output + ".marked.pdf";
    QVERIFY(bool(PDFDocumentWriter(nullptr).write(markedFile, &marked, false)));

    QFile markedData(markedFile);
    QVERIFY(markedData.open(QIODevice::ReadOnly));
    // PDFFIRE_REDACT_LABEL = white / red: REDACTED on the boxes, PDFFIRE_REDACT_OUTLINES = 1: the outlines method
    const QString labelStyle = qEnvironmentVariable("PDFFIRE_REDACT_LABEL");
    const QColor labelColor = labelStyle == "red" ? QColor(220, 0, 0) : QColor(Qt::white);
    RedactionResult result = redact(markedData.readAll(), QFileInfo(output).completeBaseName(), !qEnvironmentVariableIsSet("PDFFIRE_REDACT_OUTLINES"),
                                    Qt::black, labelStyle.isEmpty() ? QString() : QString("REDACTED"), labelColor);
    QVERIFY(result.ok);
    QFile::remove(output);
    QVERIFY(QFile::copy(result.outputFile, output));
    for (const QString& message : result.messages)
    {
        qInfo() << message;
    }
}

QTEST_MAIN(RedactionTest)

#include "tst_redactiontest.moc"
