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

// PDF Fire: regression tests for hostile (damaged or malicious) PDF files. Each test
// feeds a small hand-made input, which crashed the application, read or wrote memory
// out of bounds, or exhausted the memory, and checks it now fails gracefully. They
// are meant to be run under AddressSanitizer as well (build kind "asan").

#include "pdfcertificatemanager.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentsigner.h"
#include "pdfdocumentwriter.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdffunction.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"
#include "pdfparser.h"
#include "pdfrenderer.h"
#include "pdfsecurityhandler.h"
#include "pdfstreamfilters.h"

#include <QtTest>
#include <QBuffer>
#include <QElapsedTimer>
#include <QPainter>
#include <QRandomGenerator>

#include <openssl/evp.h>
#include <openssl/pkcs12.h>
#include <openssl/x509.h>

#include <zlib.h>

#include <limits>

using namespace pdf;

class HostileFilesTest : public QObject
{
    Q_OBJECT

private slots:
    void runLengthTruncated();
    void runLengthFuzz();
    void decompressionBombFlate();
    void decompressionBombAscii85();
    void predictorHugeColumns();
    void xrefStreamNegativeIndex();
    void xrefStreamHugeSize();
    void xrefTableHugeCount();
    void recoveryHugeObjectNumber();
    void postScriptIntegerEdgeCases();
    void sampledFunctionHugeSize();
    void imageShortSoftMask();
    void imageHugeSize();
    void type3GlyphDrawsItself();
    void tilingPatternPaintsItself();
    void parserNestingDepth();
    void pageTreeDepth();
    void cidFontHugeWidthRange();
    void passwordProtectedSignAndIncrementalSave();
    void pkcs12HugeIterationCount();

private:
    /// Builds a PDF file from the bodies of objects 1..n (object 1 is the catalog)
    static QByteArray makePdf(const QList<QByteArray>& objects);

    /// Body of a stream object with the given dictionary entries (without /Length)
    static QByteArray makeStream(const QByteArray& dictionary, const QByteArray& data);

    static PDFDocument readPdf(const QByteArray& data, PDFDocumentReader::Result* result = nullptr, bool permissive = false);

    /// Renders the first page, returns the rendering errors
    static QList<PDFRenderError> renderFirstPage(const PDFDocument& document);

    /// zlib (deflate) compression, fast level
    static QByteArray deflate(const QByteArray& data);

    static QByteArray decodeStream(const PDFDocument& document, PDFInteger objectNumber);
};

QByteArray HostileFilesTest::makePdf(const QList<QByteArray>& objects)
{
    QByteArray pdf = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<qsizetype> offsets;
    for (qsizetype i = 0; i < objects.size(); ++i)
    {
        offsets.push_back(pdf.size());
        pdf += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }

    const qsizetype xrefOffset = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const qsizetype offset : offsets)
    {
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    pdf += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return pdf;
}

QByteArray HostileFilesTest::makeStream(const QByteArray& dictionary, const QByteArray& data)
{
    return "<< " + dictionary + " /Length " + QByteArray::number(data.size()) + " >>\nstream\n" + data + "\nendstream";
}

PDFDocument HostileFilesTest::readPdf(const QByteArray& data, PDFDocumentReader::Result* result, bool permissive)
{
    PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, permissive, false);
    PDFDocument document = reader.readFromBuffer(data);
    if (result)
    {
        *result = reader.getReadingResult();
    }
    return document;
}

QList<PDFRenderError> HostileFilesTest::renderFirstPage(const PDFDocument& document)
{
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFOptionalContentActivity optionalContentActivity(&document, OCUsage::View, nullptr);
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(&document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    PDFCMSGeneric cms;
    PDFMeshQualitySettings meshQualitySettings;

    PDFRenderer renderer(&document, &fontCache, &cms, &optionalContentActivity, PDFRenderer::getDefaultFeatures(), meshQualitySettings);
    QImage image(100, 100, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    QList<PDFRenderError> errors = renderer.render(&painter, QRectF(0, 0, 100, 100), 0);
    painter.end();
    return errors;
}

QByteArray HostileFilesTest::deflate(const QByteArray& data)
{
    uLongf size = compressBound(uLong(data.size()));
    QByteArray result(qsizetype(size), Qt::Uninitialized);
    if (compress2(reinterpret_cast<Bytef*>(result.data()), &size, reinterpret_cast<const Bytef*>(data.constData()), uLong(data.size()), 1) != Z_OK)
    {
        return QByteArray();
    }
    result.resize(qsizetype(size));
    return result;
}

QByteArray HostileFilesTest::decodeStream(const PDFDocument& document, PDFInteger objectNumber)
{
    const PDFObject& object = document.getStorage().getObject(PDFObjectReference(objectNumber, 0));
    if (!object.isStream())
    {
        throw PDFException("Not a stream");
    }
    return document.getDecodedStream(object.getStream());
}

void HostileFilesTest::runLengthTruncated()
{
    // A literal run, which is longer than the rest of the input, read the heap behind
    // it (2 bytes decoded to 28 kB of heap memory). A repeat run without its byte read
    // one byte past the end and continued from there.
    // The filter classes are exported, the storage is not (Windows links only exported symbols)
    const PDFRunLengthDecodeFilter runLengthFilter;
    const PDFStreamFilter* filter = &runLengthFilter;
    QVERIFY(filter);

    QCOMPARE(filter->apply(QByteArray("\x7f" "A", 2), PDFObject(), nullptr), QByteArray("A"));
    QCOMPARE(filter->apply(QByteArray("\x81", 1), PDFObject(), nullptr), QByteArray());
    QCOMPARE(filter->apply(QByteArray("\x02" "AB", 3), PDFObject(), nullptr), QByteArray("AB"));

    // Well-formed input is unchanged: 3 literal bytes, 4 repeated bytes, end of data
    QCOMPARE(filter->apply(QByteArray("\x02" "abc" "\xfd" "x" "\x80" "zz", 8), PDFObject(), nullptr), QByteArray("abcxxxx"));
}

void HostileFilesTest::runLengthFuzz()
{
    // Random short inputs - meaningful under AddressSanitizer
    // The filter classes are exported, the storage is not (Windows links only exported symbols)
    const PDFRunLengthDecodeFilter runLengthFilter;
    const PDFStreamFilter* filter = &runLengthFilter;
    QRandomGenerator generator(431);
    for (int i = 0; i < 20000; ++i)
    {
        QByteArray input(generator.bounded(1, 12), Qt::Uninitialized);
        for (char& byte : input)
        {
            byte = char(generator.bounded(256));
        }

        const QByteArray output = filter->apply(input, PDFObject(), nullptr);
        QVERIFY(output.size() <= input.size() * 128);
    }
}

void HostileFilesTest::decompressionBombFlate()
{
    // Two Flate filters: about 1 kB per 1 MB of zeros on each level. 300 MB of zeros
    // (more than the limit of a decoded stream) compress to a few kilobytes. The stream
    // must fail quickly instead of exhausting the memory.
    const QByteArray zeros(300 * 1024 * 1024, 0);
    const QByteArray once = deflate(zeros);
    const QByteArray twice = deflate(once);
    QVERIFY(!twice.isEmpty());
    QVERIFY(twice.size() < 64 * 1024);

    const PDFDocument document = readPdf(makePdf({ "<< /Type /Catalog /Pages 2 0 R >>",
                                                   "<< /Type /Pages /Kids [] /Count 0 >>",
                                                   makeStream("/Filter [/FlateDecode /FlateDecode]", twice) }));

    QElapsedTimer timer;
    timer.start();
    QVERIFY_THROWS_EXCEPTION(PDFException, decodeStream(document, 3));
    QVERIFY(timer.elapsed() < 30000);

    // A single filter of the same data is decoded (the limit is not hit)
    const PDFDocument smallDocument = readPdf(makePdf({ "<< /Type /Catalog /Pages 2 0 R >>",
                                                        "<< /Type /Pages /Kids [] /Count 0 >>",
                                                        makeStream("/Filter /FlateDecode", deflate(QByteArray(1024 * 1024, 'a'))) }));
    QCOMPARE(decodeStream(smallDocument, 3).size(), 1024 * 1024);
}

void HostileFilesTest::decompressionBombAscii85()
{
    // ASCII85 'z' stands for four zero bytes: Flate of 70 million 'z' is tiny, it
    // decodes to 280 MB (pdfcraft v0.9.0 fixed the same chain bomb)
    const QByteArray letters(70 * 1024 * 1024, 'z');
    const QByteArray compressed = deflate(letters);
    QVERIFY(!compressed.isEmpty());

    const PDFDocument document = readPdf(makePdf({ "<< /Type /Catalog /Pages 2 0 R >>",
                                                   "<< /Type /Pages /Kids [] /Count 0 >>",
                                                   makeStream("/Filter [/FlateDecode /ASCII85Decode]", compressed) }));
    QVERIFY_THROWS_EXCEPTION(PDFException, decodeStream(document, 3));

    // Ordinary ASCII85 still works
    const PDFAscii85DecodeFilter ascii85Filter;
    const PDFStreamFilter* filter = &ascii85Filter;
    QCOMPARE(filter->apply(QByteArray("87cURD]i,\"Ebo80~>"), PDFObject(), nullptr), QByteArray("Hello World!"));
    QCOMPARE(filter->apply(QByteArray("zz~>"), PDFObject(), nullptr), QByteArray(8, 0));
}

void HostileFilesTest::predictorHugeColumns()
{
    // One predictor tag byte with /Columns 100000000 produced a 100 MB row of zeros;
    // a larger /Columns overflowed the int row size (undefined behaviour).
    const QByteArray oneByte = deflate(QByteArray("\x02", 1));
    const PDFDocument document = readPdf(makePdf({ "<< /Type /Catalog /Pages 2 0 R >>",
                                                   "<< /Type /Pages /Kids [] /Count 0 >>",
                                                   makeStream("/Filter /FlateDecode /DecodeParms << /Predictor 12 /Columns 100000000 >>", oneByte),
                                                   makeStream("/Filter /FlateDecode /DecodeParms << /Predictor 12 /Colors 32 /BitsPerComponent 16 /Columns 2147483647 >>", oneByte),
                                                   makeStream("/Filter /FlateDecode /DecodeParms << /Predictor 12 /Columns 4 >>", deflate(QByteArray("\x00" "abcd" "\x02" "\x01\x01\x01\x01", 10))) }));

    QVERIFY(decodeStream(document, 3).size() < 1024);
    QVERIFY_THROWS_EXCEPTION(PDFException, decodeStream(document, 4));

    // A well-formed PNG predictor (None, then Up) is unchanged
    QCOMPARE(decodeStream(document, 5), QByteArray("abcd" "bcde"));
}

void HostileFilesTest::xrefStreamNegativeIndex()
{
    // A cross-reference stream with /Index [-10 20] wrote 10 entries before the start
    // of the table (heap buffer overflow at open)
    QByteArray pdf = "%PDF-1.7\n";
    const qsizetype object1 = pdf.size();
    pdf += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    const qsizetype object2 = pdf.size();
    pdf += "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
    const qsizetype object3 = pdf.size();

    QByteArray entries;
    auto addEntry = [&entries](int type, quint32 offset, quint16 generation)
    {
        entries.append(char(type));
        entries.append(char(offset >> 24)).append(char(offset >> 16)).append(char(offset >> 8)).append(char(offset));
        entries.append(char(generation >> 8)).append(char(generation));
    };
    for (int i = 0; i < 20; ++i)
    {
        addEntry(1, quint32(i % 2 ? object1 : object2), 0);
    }

    pdf += "3 0 obj\n" + makeStream("/Type /XRef /Size 4 /W [1 4 2] /Index [-10 20] /Root 1 0 R", entries) + "\nendobj\n";
    pdf += "startxref\n" + QByteArray::number(object3) + "\n%%EOF\n";

    PDFDocumentReader::Result result = PDFDocumentReader::Result::OK;
    readPdf(pdf, &result, false);
    QCOMPARE(result, PDFDocumentReader::Result::Failed);

    // Also the permissive reading (recovery of a damaged file) must not crash
    readPdf(pdf, &result, true);
}

void HostileFilesTest::xrefStreamHugeSize()
{
    // /Size 2147483647 sized the table to about 100 GB (std::bad_alloc terminated the application)
    QByteArray pdf = "%PDF-1.7\n";
    pdf += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    pdf += "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
    const qsizetype object3 = pdf.size();
    pdf += "3 0 obj\n" + makeStream("/Type /XRef /Size 2147483647 /W [1 4 2] /Index [0 1] /Root 1 0 R", QByteArray(7, 0)) + "\nendobj\n";
    pdf += "startxref\n" + QByteArray::number(object3) + "\n%%EOF\n";

    QElapsedTimer timer;
    timer.start();
    PDFDocumentReader::Result result = PDFDocumentReader::Result::OK;
    readPdf(pdf, &result, false);
    QCOMPARE(result, PDFDocumentReader::Result::Failed);

    // A field wider than 8 bytes is invalid
    QByteArray widePdf = pdf;
    widePdf.replace("/Size 2147483647 /W [1 4 2]", QByteArray("/Size 3 /W [1 40 2]").leftJustified(27, ' '));
    readPdf(widePdf, &result, false);
    QCOMPARE(result, PDFDocumentReader::Result::Failed);
    QVERIFY(timer.elapsed() < 5000);
}

void HostileFilesTest::xrefTableHugeCount()
{
    // A classic table "0 2147483647" was allocated before its entries were read
    QByteArray pdf = makePdf({ "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [] /Count 0 >>" });
    const qsizetype index = pdf.indexOf("xref\n0 3\n");
    QVERIFY(index > 0);
    pdf.replace(index, 9, "xref\n0 2147483647\n");

    QElapsedTimer timer;
    timer.start();
    PDFDocumentReader::Result result = PDFDocumentReader::Result::OK;
    readPdf(pdf, &result, false);
    QCOMPARE(result, PDFDocumentReader::Result::Failed);
    QVERIFY(timer.elapsed() < 5000);

    // Negative first object number
    pdf.replace("xref\n0 2147483647\n", "xref\n-5 3\n");
    readPdf(pdf, &result, false);
    QCOMPARE(result, PDFDocumentReader::Result::Failed);
}

void HostileFilesTest::recoveryHugeObjectNumber()
{
    // A damaged file (no valid cross-reference table) is recovered by scanning for
    // objects - an object number of 100 million sized the object table to gigabytes
    QByteArray pdf = "%PDF-1.7\n";
    pdf += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    pdf += "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
    pdf += "100000000 0 obj\n<< /Hostile true >>\nendobj\n";
    pdf += "trailer\n<< /Size 3 /Root 1 0 R >>\nstartxref\n999999\n%%EOF\n";

    QElapsedTimer timer;
    timer.start();
    PDFDocumentReader::Result result = PDFDocumentReader::Result::Failed;
    const PDFDocument document = readPdf(pdf, &result, true);
    QCOMPARE(result, PDFDocumentReader::Result::OK);
    QVERIFY(document.getStorage().getObjects().size() < 1000);
    QVERIFY(timer.elapsed() < 5000);
}

void HostileFilesTest::postScriptIntegerEdgeCases()
{
    // INT64_MIN idiv -1 raised SIGFPE (the application was killed), INT64_MIN mod -1
    // too; shifts by 64 bits or more and integer overflow were undefined behaviour
    // (the lexer reads 32-bit integers, INT64_MIN = -(2^31)^2 * 2 is computed; the results
    // are divided by 2^64 - itself an integer overflow to a real number - to fit the range)
    const QByteArray program = "{ pop -2147483648 dup mul neg 2 mul dup -1 idiv 65536 dup mul dup mul div exch -1 mod "
                               "1 100 bitshift 1 -100 bitshift -2147483648 dup mul dup add 65536 dup mul dup mul div }";
    const PDFDocument document = readPdf(makePdf({ "<< /Type /Catalog /Pages 2 0 R >>",
                                                   "<< /Type /Pages /Kids [] /Count 0 >>",
                                                   makeStream("/FunctionType 4 /Domain [0 1] /Range [-1 1 -1 1 -1 1 -1 1 -1 1]", program) }));

    PDFFunctionPtr function;
    try
    {
        function = PDFFunction::createFunction(&document, document.getStorage().getObject(PDFObjectReference(3, 0)));
    }
    catch (const PDFException& exception)
    {
        QFAIL(qPrintable(exception.getMessage()));
    }
    QVERIFY(function);

    const PDFReal input = 0.5;
    std::array<PDFReal, 5> output = { };
    PDFFunction::FunctionResult result = function->apply(&input, &input + 1, output.data(), output.data() + output.size());
    QVERIFY2(result.evaluated, qPrintable(result.errorMessage));
    QCOMPARE(output[0], -0.5);
    QCOMPARE(output[1], 0.0);
    QCOMPARE(output[2], 0.0);
    QCOMPARE(output[3], 0.0);
    QCOMPARE(output[4], 0.5);
}

void HostileFilesTest::sampledFunctionHugeSize()
{
    // /Size [100000 100000] allocated 10^10 samples (80 GB) before the data were
    // checked; the sample count was multiplied in an int
    const PDFDocument document = readPdf(makePdf({ "<< /Type /Catalog /Pages 2 0 R >>",
                                                   "<< /Type /Pages /Kids [] /Count 0 >>",
                                                   makeStream("/FunctionType 0 /Domain [0 1 0 1] /Range [0 1] /Size [100000 100000] /BitsPerSample 8", QByteArray(10, 'x')),
                                                   makeStream("/FunctionType 0 /Domain [0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1] /Range [0 1] "
                                                              "/Size [1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1] /BitsPerSample 8", QByteArray(10, 'x')),
                                                   makeStream("/FunctionType 0 /Domain [0 1] /Range [0 1] /Size [2] /BitsPerSample 8", QByteArray("\x00\xff", 2)) }));

    QElapsedTimer timer;
    timer.start();
    QVERIFY_THROWS_EXCEPTION(PDFException, PDFFunction::createFunction(&document, document.getStorage().getObject(PDFObjectReference(3, 0))));
    QVERIFY_THROWS_EXCEPTION(PDFException, PDFFunction::createFunction(&document, document.getStorage().getObject(PDFObjectReference(4, 0))));
    QVERIFY(timer.elapsed() < 2000);

    // A well-formed sampled function still works
    PDFFunctionPtr function = PDFFunction::createFunction(&document, document.getStorage().getObject(PDFObjectReference(5, 0)));
    QVERIFY(function);
    const PDFReal input = 1.0;
    PDFReal output = 0.0;
    QVERIFY(function->apply(&input, &input + 1, &output, &output + 1).evaluated);
    QCOMPARE(output, 1.0);
}

void HostileFilesTest::imageShortSoftMask()
{
    // A soft mask of 1000x1000 pixels with 10 bytes of data: its rows were read
    // directly from the data, far past its end
    const QByteArray imageData = deflate(QByteArray(1000 * 1000, char(0x80)));
    const PDFDocument document = readPdf(makePdf({ "<< /Type /Catalog /Pages 2 0 R >>",
                                                   "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                                   "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Resources << /XObject << /Im1 5 0 R >> >> /Contents 4 0 R >>",
                                                   makeStream("", "q 100 0 0 100 0 0 cm /Im1 Do Q"),
                                                   makeStream("/Type /XObject /Subtype /Image /Width 1000 /Height 1000 /ColorSpace /DeviceGray /BitsPerComponent 8 /SMask 6 0 R /Filter /FlateDecode", imageData),
                                                   makeStream("/Type /XObject /Subtype /Image /Width 1000 /Height 1000 /ColorSpace /DeviceGray /BitsPerComponent 8", QByteArray(10, char(0xff))) }));
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));
    renderFirstPage(document);
}

void HostileFilesTest::imageHugeSize()
{
    // /Width 4294967295 wrapped the 32-bit stride; 100000 x 100000 pixels allocated
    // an image of 10 GB (or got a null image, then wrote through a null scan line)
    const PDFDocument document = readPdf(makePdf({ "<< /Type /Catalog /Pages 2 0 R >>",
                                                   "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                                   "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Resources << /XObject << /Im1 5 0 R /Im2 6 0 R /Im3 7 0 R >> >> /Contents 4 0 R >>",
                                                   makeStream("", "q 100 0 0 100 0 0 cm /Im1 Do /Im2 Do /Im3 Do Q"),
                                                   makeStream("/Type /XObject /Subtype /Image /Width 4294967295 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8", QByteArray(16, 'x')),
                                                   makeStream("/Type /XObject /Subtype /Image /Width 100000 /Height 100000 /ColorSpace /DeviceGray /BitsPerComponent 8", QByteArray(16, 'x')),
                                                   makeStream("/Type /XObject /Subtype /Image /Width 100000 /Height 100000 /ImageMask true", QByteArray(16, 'x')) }));

    QElapsedTimer timer;
    timer.start();
    const QList<PDFRenderError> errors = renderFirstPage(document);
    QVERIFY(!errors.isEmpty());
    QVERIFY(timer.elapsed() < 10000);
}

void HostileFilesTest::type3GlyphDrawsItself()
{
    // The glyph procedure of a Type 3 font shows a glyph of the same font: it recursed
    // until the stack overflowed
    const PDFDocument document = readPdf(makePdf({ "<< /Type /Catalog /Pages 2 0 R >>",
                                                   "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                                   "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
                                                   makeStream("", "BT /F1 12 Tf 10 10 Td (A) Tj ET"),
                                                   "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] /CharProcs << /A 6 0 R >> "
                                                   "/Encoding << /Type /Encoding /Differences [65 /A] >> /FirstChar 65 /LastChar 65 /Widths [1000] /Resources << /Font << /F1 5 0 R >> >> >>",
                                                   makeStream("", "1000 0 d0 0 0 500 500 re f BT /F1 1000 Tf 0 0 Td (A) Tj ET") }));

    const QList<PDFRenderError> errors = renderFirstPage(document);
    QVERIFY(std::any_of(errors.cbegin(), errors.cend(), [](const PDFRenderError& error) { return error.message.contains("nested too deep"); }));
}

void HostileFilesTest::tilingPatternPaintsItself()
{
    // A tiling pattern, which paints with itself, recursed until the stack overflowed
    const PDFDocument document = readPdf(makePdf({ "<< /Type /Catalog /Pages 2 0 R >>",
                                                   "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                                   "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Resources << /Pattern << /P1 5 0 R >> >> /Contents 4 0 R >>",
                                                   makeStream("", "/Pattern cs /P1 scn 0 0 20 20 re f"),
                                                   makeStream("/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 10 10] /XStep 10 /YStep 10 /Resources << /Pattern << /P1 5 0 R >> >>",
                                                              "/Pattern cs /P1 scn 0 0 10 10 re f") }));

    QElapsedTimer timer;
    timer.start();
    const QList<PDFRenderError> errors = renderFirstPage(document);
    QVERIFY(std::any_of(errors.cbegin(), errors.cend(), [](const PDFRenderError& error) { return error.message.contains("nested too deep"); }));
    QVERIFY(timer.elapsed() < 30000);
}

void HostileFilesTest::parserNestingDepth()
{
    // "[[[[..." recursed once per bracket until the stack overflowed
    const QByteArray deep = QByteArray(100000, '[') + QByteArray(100000, ']');
    PDFParser parser(deep, nullptr, PDFParser::None);
    QVERIFY_THROWS_EXCEPTION(PDFException, parser.getObject());

    const QByteArray deepDictionaries = QByteArray("<< /A ").repeated(100000);
    PDFParser dictionaryParser(deepDictionaries, nullptr, PDFParser::None);
    QVERIFY_THROWS_EXCEPTION(PDFException, dictionaryParser.getObject());

    // Ordinary nesting is fine
    const QByteArray shallow = QByteArray(100, '[') + "1" + QByteArray(100, ']');
    PDFParser shallowParser(shallow, nullptr, PDFParser::None);
    QVERIFY(shallowParser.getObject().isArray());
}

void HostileFilesTest::pageTreeDepth()
{
    // A chain of 100000 page tree nodes (no cycle, so the cycle check does not stop
    // it) recursed once per node
    constexpr int depth = 100000;
    QList<QByteArray> objects;
    objects << "<< /Type /Catalog /Pages 2 0 R >>";
    for (int i = 0; i < depth; ++i)
    {
        const int number = 2 + i;
        objects << "<< /Type /Pages /Kids [" + QByteArray::number(number + 1) + " 0 R] /Count 1 >>";
    }
    objects << "<< /Type /Page /MediaBox [0 0 100 100] >>";

    PDFDocumentReader::Result result = PDFDocumentReader::Result::OK;
    const PDFDocument document = readPdf(makePdf(objects), &result, false);
    QVERIFY(result != PDFDocumentReader::Result::OK || document.getCatalog()->getPageCount() == 0);

    // A tree of ordinary depth is read
    QList<QByteArray> shallowObjects;
    shallowObjects << "<< /Type /Catalog /Pages 2 0 R >>";
    for (int i = 0; i < 10; ++i)
    {
        shallowObjects << "<< /Type /Pages /Kids [" + QByteArray::number(3 + i) + " 0 R] /Count 1 >>";
    }
    shallowObjects << "<< /Type /Page /MediaBox [0 0 100 100] >>";
    const PDFDocument shallowDocument = readPdf(makePdf(shallowObjects), &result, false);
    QCOMPARE(result, PDFDocumentReader::Result::OK);
    QCOMPARE(shallowDocument.getCatalog()->getPageCount(), size_t(1));
}

void HostileFilesTest::cidFontHugeWidthRange()
{
    // /W [0 4294967295 500] inserted four billion entries into a hash map
    const PDFDocument document = readPdf(makePdf({ "<< /Type /Catalog /Pages 2 0 R >>",
                                                   "<< /Type /Pages /Kids [] /Count 0 >>",
                                                   "<< /Type /Font /Subtype /Type0 /BaseFont /Hostile /Encoding /Identity-H /DescendantFonts [4 0 R] >>",
                                                   "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Hostile /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
                                                   "/W [0 4294967295 500 7 [250]] /DW 1000 >>" }));

    QElapsedTimer timer;
    timer.start();
    PDFFontPointer font;
    try
    {
        font = PDFFont::createFont(document.getStorage().getObject(PDFObjectReference(3, 0)), "F1", &document);
    }
    catch (const PDFException&)
    {
        // A font without a font file may be refused - it must be refused quickly
    }
    QVERIFY(timer.elapsed() < 5000);

    if (const PDFType0Font* type0Font = dynamic_cast<const PDFType0Font*>(font.get()))
    {
        QCOMPARE(type0Font->getGlyphAdvance(123456), 500.0);
    }
}

void HostileFilesTest::passwordProtectedSignAndIncrementalSave()
{
    // Signing or incrementally saving a document, which needs a password to open,
    // reads it again without a password callback: the empty std::function was called
    // and std::bad_function_call terminated the application
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 612, 792));
    PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = PDFSecurityHandlerFactory::AES_256;
    settings.userPassword = "open-secret";
    settings.ownerPassword = "owner-secret";
    settings.permissions = uint32_t(PDFSecurityHandler::Permission::PrintLowResolution);
    builder.setSecurityHandler(PDFSecurityHandlerFactory::createSecurityHandler(settings));
    const PDFDocument source = builder.build();

    QBuffer buffer;
    QVERIFY(buffer.open(QBuffer::WriteOnly));
    PDFDocumentWriter writer(nullptr);
    QVERIFY(writer.write(&buffer, &source));
    buffer.close();
    const QByteArray encryptedData = buffer.data();

    PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = true; return QString("open-secret"); }, false, false);
    const PDFDocument document = reader.readFromBuffer(encryptedData);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    // A reader without a callback reports "cancelled"
    PDFDocumentReader noCallbackReader(nullptr, nullptr, false, false);
    noCallbackReader.readFromBuffer(encryptedData);
    QCOMPARE(noCallbackReader.getReadingResult(), PDFDocumentReader::Result::Cancelled);

    // Incremental update gives a message instead of a crash
    QBuffer incrementalBuffer;
    QVERIFY(incrementalBuffer.open(QBuffer::WriteOnly));
    const PDFOperationResult incrementalResult = writer.writeIncrementalUpdate(&incrementalBuffer, encryptedData, &document);
    QVERIFY(!incrementalResult);
    QVERIFY(incrementalResult.getErrorMessage().contains("password"));

    // Signing gives its own result and message
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.originalDocumentData = encryptedData;
    parameters.signFunction = [](const QByteArray&, QByteArray& signature)
    {
        signature = QByteArray(64, char(0x30));
        return true;
    };
    parameters.createSignatureFieldFunction = [&document](PDFDocumentBuilder& signatureBuilder, PDFObjectReference signatureDictionary)
    {
        return signatureBuilder.createSignatureField("Signature", signatureDictionary, document.getCatalog()->getPage(0)->getPageReference());
    };

    QByteArray signedDocument;
    const PDFDocumentSigner::Result signResult = PDFDocumentSigner::sign(parameters, signedDocument);
    QCOMPARE(signResult, PDFDocumentSigner::Result::PasswordRequired);
    QVERIFY(!PDFDocumentSigner::getResultMessage(signResult).isEmpty());
    QVERIFY(signedDocument.isEmpty());
}

void HostileFilesTest::pkcs12HugeIterationCount()
{
    // Every .pfx file of the certificate folder is read at startup; a file asking for
    // INT_MAX iterations hung the application at every start
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_EC_gen("P-256"), &EVP_PKEY_free);
    QVERIFY(key);
    std::unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(), &X509_free);
    QVERIFY(certificate);
    X509_set_version(certificate.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1);
    X509_gmtime_adj(X509_getm_notBefore(certificate.get()), 0);
    X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 3600);
    X509_NAME* name = X509_get_subject_name(certificate.get());
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("Hostile test"), -1, -1, 0);
    X509_set_issuer_name(certificate.get(), name);
    X509_set_pubkey(certificate.get(), key.get());
    QVERIFY(X509_sign(certificate.get(), key.get(), EVP_sha256()) > 0);

    auto toBytes = [](PKCS12* pkcs12)
    {
        unsigned char* buffer = nullptr;
        const int length = i2d_PKCS12(pkcs12, &buffer);
        QByteArray result(reinterpret_cast<const char*>(buffer), qMax(length, 0));
        OPENSSL_free(buffer);
        return result;
    };

    // An ordinary file (default encryption and iterations) is accepted
    std::unique_ptr<PKCS12, decltype(&PKCS12_free)> ordinary(PKCS12_create("password", "Hostile test", key.get(), certificate.get(), nullptr, 0, 0, 0, 0, 0), &PKCS12_free);
    QVERIFY(ordinary);
    QVERIFY(PDFCertificateManager::isPKCS12IterationCountAcceptable(toBytes(ordinary.get())));

    // MAC with INT_MAX iterations (the count is changed after the file is created,
    // creating it with the count would take ages)
    // (a MAC iteration count of 1 is not stored at all, 2 is)
    std::unique_ptr<PKCS12, decltype(&PKCS12_free)> hostileMac(PKCS12_create("password", "Hostile test", key.get(), certificate.get(), nullptr, -1, -1, 1, 2, 0), &PKCS12_free);
    QVERIFY(hostileMac);
    const ASN1_INTEGER* macIterations = nullptr;
    PKCS12_get0_mac(nullptr, nullptr, nullptr, &macIterations, hostileMac.get());
    QVERIFY(macIterations);
    ASN1_INTEGER_set(const_cast<ASN1_INTEGER*>(macIterations), std::numeric_limits<int>::max());
    const QByteArray hostileMacData = toBytes(hostileMac.get());

    QElapsedTimer timer;
    timer.start();
    QVERIFY(!PDFCertificateManager::isPKCS12IterationCountAcceptable(hostileMacData));
    PDFCertificateEntry entry;
    entry.pkcs12 = hostileMacData;
    QVERIFY(!PDFCertificateManager::isCertificateValid(entry, "password"));
    QVERIFY(timer.elapsed() < 1000);

    // A shrouded key with more iterations than the limit (the certificate and the MAC are cheap)
    std::unique_ptr<PKCS12, decltype(&PKCS12_free)> hostileKey(PKCS12_create("password", "Hostile test", key.get(), certificate.get(), nullptr, 0, -1,
                                                                             int(PDFCertificateManager::MAXIMAL_PKCS12_ITERATIONS) + 1, 1, 0), &PKCS12_free);
    QVERIFY(hostileKey);
    QVERIFY(!PDFCertificateManager::isPKCS12IterationCountAcceptable(toBytes(hostileKey.get())));

    // Garbage is not a PKCS#12 file - nothing to derive, nothing to refuse here
    QVERIFY(PDFCertificateManager::isPKCS12IterationCountAcceptable(QByteArray("not a certificate")));
}

QTEST_GUILESS_MAIN(HostileFilesTest)

#include "tst_hostilefilestest.moc"
