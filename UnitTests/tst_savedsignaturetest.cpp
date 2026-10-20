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

// PDF Fire: tests of the saved signature and initials of Fill & Sign
// (PDFFireSavedSignature, PDFFireSavedSignatureStore).

#include "pdffiresavedsignature.h"
#include "pdffirepageoperations.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"

#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace pdf;

class SavedSignatureTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void drawnJsonRoundTrip();
    void typedJsonRoundTrip();
    void invalidJsonIsSafe();
    void aspectRatio();
    void storeSaveLoadRemove();
    void placeOnPageAddsContent();
    void placedSignatureIsUpright_data();
    void placedSignatureIsUpright();

private:
    /// A wide signature: a long stroke at the top, a short stroke at the bottom left
    static PDFFireSavedSignature createSignature();

    /// The bounding box of the ink (dark pixels) of the rendered page
    static QRect inkBox(const QImage& image, const QRect& area);

    QTemporaryDir m_directory;
};

void SavedSignatureTest::initTestCase()
{
    // The saved signatures go to a temporary folder, never to the user's data
    QVERIFY(m_directory.isValid());
    qputenv("XDG_DATA_HOME", m_directory.filePath("data").toUtf8());
    if (!PDFFireSavedSignatureStore::getFileName().startsWith(m_directory.path()))
    {
        // Windows and macOS ignore XDG_DATA_HOME - Qt's test mode keeps the user's data out of reach
        QStandardPaths::setTestModeEnabled(true);
        QFile::remove(PDFFireSavedSignatureStore::getFileName());
    }
    QVERIFY(PDFFireSavedSignatureStore::getFileName().startsWith(m_directory.path()) || QStandardPaths::isTestModeEnabled());
}

PDFFireSavedSignature SavedSignatureTest::createSignature()
{
    PDFFireSavedSignature signature;
    std::vector<PDFFireSavedSignature::Stroke> strokes;
    strokes.push_back({ QPointF(10, 10), QPointF(200, 10), QPointF(390, 10) });
    strokes.push_back({ QPointF(10, 90), QPointF(60, 90) });
    signature.setStrokes(strokes, QSizeF(400, 100));
    signature.setColor(Qt::black);
    return signature;
}

void SavedSignatureTest::drawnJsonRoundTrip()
{
    const PDFFireSavedSignature signature = createSignature();
    QVERIFY(!signature.isEmpty());
    QCOMPARE(signature.getKind(), PDFFireSavedSignature::Kind::Drawn);

    const QByteArray json = QJsonDocument(signature.toJson()).toJson();
    const PDFFireSavedSignature loaded = PDFFireSavedSignature::fromJson(QJsonDocument::fromJson(json).object());
    QCOMPARE(loaded.getKind(), PDFFireSavedSignature::Kind::Drawn);
    QCOMPARE(loaded.getStrokes().size(), size_t(2));
    QCOMPARE(loaded.getStrokes()[0].size(), size_t(3));
    QCOMPARE(loaded.getStrokes()[1].back(), QPointF(60, 90));
    QCOMPARE(loaded.getPadSize(), QSizeF(400, 100));
    QCOMPARE(loaded.getColor(), QColor(Qt::black));
}

void SavedSignatureTest::typedJsonRoundTrip()
{
    PDFFireSavedSignature signature;
    signature.setText("Jane Q. Firefighter", "Sans Serif");
    signature.setColor(QColor(16, 52, 166));

    const PDFFireSavedSignature loaded = PDFFireSavedSignature::fromJson(signature.toJson());
    QCOMPARE(loaded.getKind(), PDFFireSavedSignature::Kind::Typed);
    QCOMPARE(loaded.getText(), QString("Jane Q. Firefighter"));
    QCOMPARE(loaded.getFontFamily(), QString("Sans Serif"));
    QCOMPARE(loaded.getColor(), QColor(16, 52, 166));
    QVERIFY(!loaded.getPath().isEmpty());
    QVERIFY(loaded.getAspectRatio() > 2.0);
}

void SavedSignatureTest::invalidJsonIsSafe()
{
    QVERIFY(PDFFireSavedSignature::fromJson(QJsonObject()).isEmpty());
    QVERIFY(PDFFireSavedSignature::fromJson(QJsonDocument::fromJson(R"({"kind":"unknown"})").object()).isEmpty());

    // Broken numbers and an odd count of coordinates are skipped, a bad pad gets a default
    const QJsonObject broken = QJsonDocument::fromJson(R"({"kind":"drawn","pad":[-5,"x"],"strokes":[[1,2,3],["a","b"],[]],"pen":99,"color":"nonsense"})").object();
    const PDFFireSavedSignature signature = PDFFireSavedSignature::fromJson(broken);
    QCOMPARE(signature.getStrokes().size(), size_t(1));
    QCOMPARE(signature.getStrokes()[0].size(), size_t(1));
    QVERIFY(signature.getPadSize().width() > 0);
    QVERIFY(signature.getPenWidth() <= 0.5);
    QVERIFY(signature.getColor().isValid());
}

void SavedSignatureTest::aspectRatio()
{
    // 380 x 80 of strokes + the pen (2.5 % of the pad height = 2.5)
    const PDFFireSavedSignature signature = createSignature();
    QVERIFY(qAbs(signature.getAspectRatio() - 382.5 / 82.5) < 0.01);
    QCOMPARE(PDFFireSavedSignature().getAspectRatio(), 1.0);
}

void SavedSignatureTest::storeSaveLoadRemove()
{
    using Role = PDFFireSavedSignatureStore::Role;
    QVERIFY(PDFFireSavedSignatureStore::load(Role::Signature).isEmpty());

    PDFFireSavedSignature initials;
    initials.setText("JQF", "Sans Serif");
    QVERIFY(PDFFireSavedSignatureStore::save(Role::Signature, createSignature()));
    QVERIFY(PDFFireSavedSignatureStore::save(Role::Initials, initials));
    QVERIFY(QFile::exists(PDFFireSavedSignatureStore::getFileName()));

    // Only the user can read the file (Windows: Qt does not read the NTFS permissions by
    // default and reports the file readable by everyone - the per-user AppData folder protects it)
#ifdef Q_OS_UNIX
    QCOMPARE(QFile::permissions(PDFFireSavedSignatureStore::getFileName()) & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions());
#endif

    QCOMPARE(PDFFireSavedSignatureStore::load(Role::Signature).getStrokes().size(), size_t(2));
    QCOMPARE(PDFFireSavedSignatureStore::load(Role::Initials).getText(), QString("JQF"));

    // Removing one keeps the other
    QVERIFY(PDFFireSavedSignatureStore::remove(Role::Signature));
    QVERIFY(PDFFireSavedSignatureStore::load(Role::Signature).isEmpty());
    QCOMPARE(PDFFireSavedSignatureStore::load(Role::Initials).getText(), QString("JQF"));

    // An empty signature is not saved - it removes the saved one
    QVERIFY(PDFFireSavedSignatureStore::save(Role::Initials, PDFFireSavedSignature()));
    QVERIFY(PDFFireSavedSignatureStore::load(Role::Initials).isEmpty());
}

void SavedSignatureTest::placeOnPageAddsContent()
{
    const PDFDocument document = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    PDFDocument result;

    QVERIFY(!PDFFireSavedSignature::placeOnPage(&document, 0, QRectF(100, 100, 200, 50), PDFFireSavedSignature(), &result));
    QVERIFY(!PDFFireSavedSignature::placeOnPage(&document, 1, QRectF(100, 100, 200, 50), createSignature(), &result));
    QVERIFY(!PDFFireSavedSignature::placeOnPage(&document, 0, QRectF(), createSignature(), &result));

    QVERIFY(PDFFireSavedSignature::placeOnPage(&document, 0, QRectF(100, 100, 200, 50), createSignature(), &result));
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(1));

    // The page has new content, the source document is not changed (Undo goes back to it)
    const PDFPage* page = result.getCatalog()->getPage(0);
    const PDFObject& contents = result.getObject(page->getContents());
    QByteArray content;
    if (contents.isArray())
    {
        for (const PDFObject& item : *contents.getArray())
        {
            const PDFObject& stream = result.getObject(item);
            if (stream.isStream())
            {
                content += result.getDecodedStream(stream.getStream());
            }
        }
    }
    else if (contents.isStream())
    {
        content = result.getDecodedStream(contents.getStream());
    }
    QVERIFY(content.contains(" l") || content.contains(" c"));
    QVERIFY(document.getObject(document.getCatalog()->getPage(0)->getContents()) != contents);
}

QRect SavedSignatureTest::inkBox(const QImage& image, const QRect& area)
{
    QRect box;
    for (int y = area.top(); y <= area.bottom(); ++y)
    {
        for (int x = area.left(); x <= area.right(); ++x)
        {
            if (qGray(image.pixel(x, y)) < 128)
            {
                box |= QRect(x, y, 1, 1);
            }
        }
    }
    return box;
}

void SavedSignatureTest::placedSignatureIsUpright_data()
{
    QTest::addColumn<int>("rotation");
    QTest::newRow("0") << 0;
    QTest::newRow("90") << 90;
    QTest::newRow("180") << 180;
    QTest::newRow("270") << 270;
}

void SavedSignatureTest::placedSignatureIsUpright()
{
    QFETCH(int, rotation);

    if (QStandardPaths::findExecutable("pdftoppm").isEmpty())
    {
        QSKIP("pdftoppm (poppler-utils) is not installed");
    }

    // A page 600 x 800, turned; the signature 300 x 75 on the screen, as the placing tool
    // gives it (on a page turned by 90 or 270 degrees the rectangle is transposed)
    PDFDocumentBuilder builder;
    const PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 600, 800));
    PageRotation pageRotation = PageRotation::None;
    switch (rotation)
    {
        case 90: pageRotation = PageRotation::Rotate90; break;
        case 180: pageRotation = PageRotation::Rotate180; break;
        case 270: pageRotation = PageRotation::Rotate270; break;
        default: break;
    }
    builder.setPageRotation(pageReference, pageRotation);
    const PDFDocument document = builder.build();

    const bool isTurned = rotation == 90 || rotation == 270;
    const QRectF rect(150, 300, isTurned ? 75 : 300, isTurned ? 300 : 75);

    PDFDocument result;
    QVERIFY(PDFFireSavedSignature::placeOnPage(&document, 0, rect, createSignature(), &result));

    const QString fileName = m_directory.filePath(QString("upright-%1.pdf").arg(rotation));
    QVERIFY(PDFDocumentWriter(nullptr).write(fileName, &result, false));
    const QString prefix = fileName + "_page";
    QCOMPARE(QProcess::execute("pdftoppm", { "-r", "72", "-gray", "-singlefile", fileName, prefix }), 0);
    const QImage image = QImage(prefix + ".pgm").convertToFormat(QImage::Format_Grayscale8);
    QVERIFY(!image.isNull());

    // On the screen the signature is wide: a long line at the top, a short one at the
    // bottom left - whatever the rotation of the page
    const QRect box = inkBox(image, image.rect());
    QVERIFY2(box.isValid(), "nothing was drawn");
    QVERIFY2(box.width() > 3 * box.height(), qPrintable(QString("ink box %1 x %2").arg(box.width()).arg(box.height())));
    QVERIFY(qAbs(box.width() - 300) <= 4);

    const QRect top = inkBox(image, QRect(box.left(), box.top(), box.width(), box.height() / 4));
    const QRect bottom = inkBox(image, QRect(box.left(), box.bottom() - box.height() / 4, box.width(), box.height() / 4 + 1));
    QVERIFY2(top.width() > box.width() * 0.9, "the long stroke is not at the top");
    QVERIFY2(bottom.width() < box.width() * 0.3, "the short stroke is not at the bottom");
    QVERIFY2(bottom.left() - box.left() < 4, "the short stroke is not on the left");
}

QTEST_MAIN(SavedSignatureTest)

#include "tst_savedsignaturetest.moc"
