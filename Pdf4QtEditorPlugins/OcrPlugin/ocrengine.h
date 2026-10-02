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

#ifndef OCRENGINE_H
#define OCRENGINE_H

#include "ocrtextlayer.h"

#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace tesseract
{
class TessBaseAPI;
}

namespace pdfplugin
{

/// PDF Fire: optical character recognition with Tesseract (Apache 2.0).
///
/// One engine is initialized once for a language, and then recognizes any number of
/// page images. An engine is used by one thread at a time; separate engines can run
/// in separate threads.
class OcrEngine
{
public:
    explicit OcrEngine();
    ~OcrEngine();

    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    /// Returns the directory with the *.traineddata files (empty, if there is none)
    static QString getTessdataDirectory();

    /// Returns installed languages (names of the *.traineddata files), without
    /// the special data (osd - orientation and script detection, equ - equations)
    static QStringList getAvailableLanguages();

    /// Returns Tesseract's glyph-less font program (pdf.ttf in the tessdata directory),
    /// or an empty array, if it is not installed (the text layer works without it)
    static QByteArray getGlyphLessFontProgram();

    /// Initializes the engine for the language ("eng", or several, like "eng+deu")
    bool init(const QString& language, QString* errorMessage);

    /// Recognizes the text of the image.
    /// \param image Image of the page
    /// \param dpi Resolution of the image (helps the recognition of the font sizes)
    /// \param isCancelled Returns true, when the recognition shall be stopped (can be empty)
    /// \param progress Receives the progress of the recognition, 0-100 (can be empty)
    /// \param errorMessage Error message
    OcrPageText recognize(const QImage& image,
                          int dpi,
                          const std::function<bool()>& isCancelled,
                          const std::function<void(int)>& progress,
                          QString* errorMessage);

    /// Recognizes the text of the image of the whole page (as it is displayed - the media box,
    /// rotated by /Rotate) and adds the invisible text layer to the page. This is the function
    /// to be called after a page has been scanned.
    /// \param builder Document builder
    /// \param page Page reference
    /// \param image Image of the page
    /// \param language Language ("eng", "eng+deu", ...)
    /// \param dpi Resolution of the image
    /// \param font In/out: the font of the text layer, shared by the pages (invalid = create it)
    /// \param errorMessage Error message
    /// \returns true, if the text layer has been added (false also when no text is found)
    static bool addTextLayer(pdf::PDFDocumentBuilder* builder,
                             pdf::PDFObjectReference page,
                             const QImage& image,
                             const QString& language,
                             int dpi,
                             pdf::PDFObjectReference* font,
                             QString* errorMessage);

private:
    std::unique_ptr<tesseract::TessBaseAPI> m_api;
};

}   // namespace pdfplugin

#endif // OCRENGINE_H
