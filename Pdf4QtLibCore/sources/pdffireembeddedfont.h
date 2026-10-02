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

#ifndef PDFFIREEMBEDDEDFONT_H
#define PDFFIREEMBEDDEDFONT_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QRawFont>

#include <map>

namespace pdf
{

/// PDF Fire: a TrueType font of the system embedded into a PDF, so the text written
/// with it is real text (searchable, selectable, copyable) and looks the same in every
/// reader. The font is written as a composite font (Type0, Identity-H, CIDFontType2):
/// the text is written as glyph indices, the widths and the ToUnicode map are written
/// for the glyphs used.
class PDF4QTLIBCORESHARED_EXPORT PDFFireEmbeddedFont
{
public:
    /// Creates the font for the raw font. Returns false, when the font cannot be
    /// embedded: it is not a TrueType font (CFF outlines), or its licence does not
    /// allow embedding (OS/2 fsType restricted or bitmap only).
    bool initialize(const QRawFont& rawFont);

    /// Converts the text to the glyph indices of the font; returns false, when a
    /// character of the text is missing in the font. The glyphs are remembered
    /// (with their characters) for the font object.
    /// \param text Text
    /// \param glyphs Glyph indices (written to the content stream, 2 bytes each)
    bool addText(const QString& text, QByteArray* glyphs);

    /// Creates the font object (a direct object; the streams are nested, they are
    /// made indirect when the resources are written)
    PDFObject createFontObject() const;

    /// Reassembles the font file (sfnt) from the tables of the raw font
    static QByteArray createFontFile(const QRawFont& rawFont);

    /// Is the embedding allowed by the font (the fsType field of the OS/2 table)?
    static bool isEmbeddingAllowed(const QRawFont& rawFont);

    const QRawFont& getRawFont() const { return m_rawFont; }

private:
    QRawFont m_rawFont;         ///< The font, pixel size = 1000 (units of the glyph space)
    QByteArray m_fontFile;
    QByteArray m_postScriptName;
    std::map<quint32, QString> m_usedGlyphs;
};

}   // namespace pdf

#endif // PDFFIREEMBEDDEDFONT_H
