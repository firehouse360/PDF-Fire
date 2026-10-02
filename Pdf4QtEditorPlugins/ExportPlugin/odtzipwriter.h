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

#ifndef ODTZIPWRITER_H
#define ODTZIPWRITER_H

#include <QByteArray>
#include <QString>

#include <vector>

namespace pdfplugin
{

/// PDF Fire: a minimal ZIP writer for the OpenDocument package.
///
/// Qt's QZipWriter is a private header (it may change with every Qt release), so the few
/// parts of the ZIP format, which an OpenDocument package needs, are written here: local
/// file headers, the central directory and its end record. No ZIP64, no encryption - an
/// exported document never comes near 4 GB. Entries are either stored or compressed by
/// DEFLATE (zlib, which the PDF core uses anyway).
class OdtZipWriter
{
public:
    enum class Method
    {
        Stored,     ///< The data are stored as they are (required for "mimetype", sensible for JPEG/PNG)
        Deflated    ///< The data are compressed (XML parts)
    };

    /// Adds a file. The order of the files is kept - the OpenDocument specification
    /// requires "mimetype" to be the first file of the package, stored uncompressed.
    void addFile(const QString& name, const QByteArray& data, Method method);

    /// Returns the whole ZIP archive
    QByteArray finish() const;

    /// CRC-32 (the polynomial of ZIP), exposed for the tests
    static quint32 crc32(const QByteArray& data);

    /// Raw DEFLATE (no zlib header), exposed for the tests. Returns an empty array on error.
    static QByteArray deflateRaw(const QByteArray& data);

private:
    struct Entry
    {
        QByteArray name;
        QByteArray data;            ///< Data as written (compressed, if method is Deflated)
        quint32 crc = 0;
        quint32 uncompressedSize = 0;
        Method method = Method::Stored;
    };

    std::vector<Entry> m_entries;
};

}   // namespace pdfplugin

#endif // ODTZIPWRITER_H
