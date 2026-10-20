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
    const PDFStreamFilter* filter = PDFStreamFilterStorage::getFilter("RunLengthDecode");
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
    const PDFStreamFilter* filter = PDFStreamFilterStorage::getFilter("RunLengthDecode");
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
    const PDFStreamFilter* filter = PDFStreamFilterStorage::getFilter("ASCII85Decode");
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

QTEST_GUILESS_MAIN(HostileFilesTest)

#include "tst_hostilefilestest.moc"
