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

#include "ocrengine.h"

#include <tesseract/baseapi.h>
#include <tesseract/ocrclass.h>
#include <tesseract/resultiterator.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace pdfplugin
{

namespace
{

/// Data of the monitor callbacks of Tesseract
struct MonitorData
{
    tesseract::ETEXT_DESC* monitor = nullptr;
    const std::function<bool()>* isCancelled = nullptr;
    const std::function<void(int)>* progress = nullptr;
    int lastProgress = -1;
};

bool cancelCallback(void* data, int /*words*/)
{
    // Tesseract calls this function regularly during the recognition - the progress
    // is reported from here too (the progress callbacks are not called by all versions)
    MonitorData* monitorData = static_cast<MonitorData*>(data);
    if (monitorData->progress && *monitorData->progress && monitorData->monitor->progress != monitorData->lastProgress)
    {
        monitorData->lastProgress = monitorData->monitor->progress;
        (*monitorData->progress)(qBound(0, int(monitorData->lastProgress), 100));
    }

    return monitorData->isCancelled && *monitorData->isCancelled && (*monitorData->isCancelled)();
}

QString tr(const char* text)
{
    return QCoreApplication::translate("pdfplugin::OcrEngine", text);
}

}   // namespace

OcrEngine::OcrEngine() = default;

OcrEngine::~OcrEngine()
{
    if (m_api)
    {
        m_api->End();
    }
}

QString OcrEngine::getTessdataDirectory()
{
    QStringList candidates;

    // TESSDATA_PREFIX is the directory with the data (Tesseract 4 and newer)
    const QString prefix = qEnvironmentVariable("TESSDATA_PREFIX");
    if (!prefix.isEmpty())
    {
        candidates << prefix << QDir(prefix).filePath("tessdata");
    }

    // PDF Fire: the language data installed with the program (Windows: next to the program)
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    candidates << applicationDirectory.filePath("tessdata")
               << applicationDirectory.filePath("../share/tessdata");

    candidates << "/usr/share/tesseract-ocr/5/tessdata"
               << "/usr/share/tesseract-ocr/4.00/tessdata"
               << "/usr/share/tesseract/tessdata"
               << "/usr/share/tessdata"
               << "/usr/local/share/tessdata"
               << "/usr/local/share/tesseract-ocr/5/tessdata"
               << "/app/share/tessdata";

    for (const QString& candidate : candidates)
    {
        QDir directory(candidate);
        if (directory.exists() && !directory.entryList({ "*.traineddata" }, QDir::Files).isEmpty())
        {
            return directory.absolutePath();
        }
    }

    return QString();
}

QStringList OcrEngine::getAvailableLanguages()
{
    QStringList languages;

    const QString directory = getTessdataDirectory();
    if (directory.isEmpty())
    {
        return languages;
    }

    for (const QFileInfo& fileInfo : QDir(directory).entryInfoList({ "*.traineddata" }, QDir::Files, QDir::Name))
    {
        const QString language = fileInfo.completeBaseName();
        if (language != "osd" && language != "equ")
        {
            languages << language;
        }
    }

    return languages;
}

QByteArray OcrEngine::getGlyphLessFontProgram()
{
    const QString directory = getTessdataDirectory();
    if (directory.isEmpty())
    {
        return QByteArray();
    }

    QFile file(QDir(directory).filePath("pdf.ttf"));
    if (file.size() > 0 && file.size() < 1024 * 1024 && file.open(QFile::ReadOnly))
    {
        return file.readAll();
    }

    return QByteArray();
}

bool OcrEngine::init(const QString& language, QString* errorMessage)
{
    const QString directory = getTessdataDirectory();
    if (directory.isEmpty())
    {
        *errorMessage = tr("Text recognition data was not found. Install the Tesseract language data (for example, the package tesseract-ocr-eng).");
        return false;
    }

    const QStringList available = getAvailableLanguages();
    const QStringList requested = language.split(QChar('+'), Qt::SkipEmptyParts);
    if (requested.isEmpty())
    {
        *errorMessage = tr("No language has been selected.");
        return false;
    }

    for (const QString& requestedLanguage : requested)
    {
        if (!available.contains(requestedLanguage))
        {
            *errorMessage = tr("The language '%1' is not installed.").arg(requestedLanguage);
            return false;
        }
    }

    m_api = std::make_unique<tesseract::TessBaseAPI>();
    if (m_api->Init(QFile::encodeName(directory).constData(), language.toUtf8().constData(), tesseract::OEM_DEFAULT) != 0)
    {
        m_api.reset();
        *errorMessage = tr("The text recognition engine could not be started for the language '%1'.").arg(language);
        return false;
    }

    // Default of the API is a single block of text - a page has columns, headings and so on
    m_api->SetPageSegMode(tesseract::PSM_AUTO);
    return true;
}

OcrPageText OcrEngine::recognize(const QImage& image,
                                 int dpi,
                                 const std::function<bool()>& isCancelled,
                                 const std::function<void(int)>& progress,
                                 QString* errorMessage)
{
    OcrPageText result;
    result.imageSize = image.size();

    if (!m_api)
    {
        *errorMessage = tr("The text recognition engine is not started.");
        return result;
    }

    if (image.isNull())
    {
        *errorMessage = tr("The page image is empty.");
        return result;
    }

    // 8 bit gray image - Tesseract binarizes it itself
    const QImage grayImage = image.convertToFormat(QImage::Format_Grayscale8);
    m_api->SetImage(grayImage.constBits(), grayImage.width(), grayImage.height(), 1, int(grayImage.bytesPerLine()));
    m_api->SetSourceResolution(qBound(70, dpi, 2400));

    tesseract::ETEXT_DESC monitor;
    MonitorData monitorData;
    monitorData.monitor = &monitor;
    monitorData.isCancelled = &isCancelled;
    monitorData.progress = &progress;
    monitor.cancel = &cancelCallback;
    monitor.cancel_this = &monitorData;

    const int recognizeResult = m_api->Recognize(&monitor);
    if (isCancelled && isCancelled())
    {
        m_api->Clear();
        return result;
    }

    if (recognizeResult != 0)
    {
        m_api->Clear();
        *errorMessage = tr("Text recognition failed.");
        return result;
    }

    std::unique_ptr<tesseract::ResultIterator> iterator(m_api->GetIterator());
    if (iterator && !iterator->Empty(tesseract::RIL_WORD))
    {
        iterator->Begin();
        do
        {
            if (iterator->Empty(tesseract::RIL_WORD))
            {
                continue;
            }

            int left = 0;
            int top = 0;
            int right = 0;
            int bottom = 0;

            if (result.lines.empty() || iterator->IsAtBeginningOf(tesseract::RIL_TEXTLINE))
            {
                OcrLine line;
                if (iterator->BoundingBox(tesseract::RIL_TEXTLINE, &left, &top, &right, &bottom))
                {
                    line.box = QRectF(QPointF(left, top), QPointF(right, bottom));
                }

                int x1 = 0;
                int y1 = 0;
                int x2 = 0;
                int y2 = 0;
                if (iterator->Baseline(tesseract::RIL_TEXTLINE, &x1, &y1, &x2, &y2) && x2 > x1)
                {
                    line.baseline = QLineF(x1, y1, x2, y2);
                }

                result.lines.push_back(std::move(line));
            }

            OcrWord word;
            if (std::unique_ptr<char[]> text{ iterator->GetUTF8Text(tesseract::RIL_WORD) })
            {
                word.text = QString::fromUtf8(text.get()).trimmed();
            }

            if (word.text.isEmpty() || !iterator->BoundingBox(tesseract::RIL_WORD, &left, &top, &right, &bottom))
            {
                continue;
            }

            word.box = QRectF(QPointF(left, top), QPointF(right, bottom));
            word.confidence = iterator->Confidence(tesseract::RIL_WORD);
            result.lines.back().words.push_back(std::move(word));
        }
        while (iterator->Next(tesseract::RIL_WORD));
    }

    iterator.reset();
    m_api->Clear();

    // Lines without words are not needed
    result.lines.erase(std::remove_if(result.lines.begin(), result.lines.end(), [](const OcrLine& line) { return line.words.empty(); }), result.lines.end());
    return result;
}

bool OcrEngine::addTextLayer(pdf::PDFDocumentBuilder* builder,
                             pdf::PDFObjectReference page,
                             const QImage& image,
                             const QString& language,
                             int dpi,
                             pdf::PDFObjectReference* font,
                             QString* errorMessage)
{
    QString dummyErrorMessage;
    if (!errorMessage)
    {
        errorMessage = &dummyErrorMessage;
    }

    OcrEngine engine;
    if (!engine.init(language, errorMessage))
    {
        return false;
    }

    const OcrPageText text = engine.recognize(image, dpi, {}, {}, errorMessage);
    if (!errorMessage->isEmpty())
    {
        return false;
    }

    pdf::PDFObjectReference localFont;
    return OcrTextLayer::addTextLayer(builder, page, text, font ? font : &localFont, getGlyphLessFontProgram());
}

}   // namespace pdfplugin
