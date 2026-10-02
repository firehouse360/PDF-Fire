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

#ifndef ODTWRITER_H
#define ODTWRITER_H

#include "odtdocument.h"

#include <QByteArray>

namespace pdfplugin
{

/// PDF Fire: writes OdtDocument as an OpenDocument Text package (ODF 1.3), which is opened
/// by LibreOffice Writer and by Microsoft Word. The package is: "mimetype" (first, stored),
/// content.xml, styles.xml, meta.xml, the pictures and META-INF/manifest.xml listing them all.
class OdtWriter
{
public:
    /// Returns the complete .odt file
    static QByteArray createPackage(const OdtDocument& document);

    /// The parts of the package (public for the tests)
    static QByteArray createContentXml(const OdtDocument& document);
    static QByteArray createStylesXml(const OdtDocument& document);
    static QByteArray createMetaXml(const OdtDocument& document);
    static QByteArray createManifestXml(const OdtDocument& document);

    static constexpr const char* MIME_TYPE = "application/vnd.oasis.opendocument.text";
};

}   // namespace pdfplugin

#endif // ODTWRITER_H
