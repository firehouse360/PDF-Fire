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

#include "odtzipwriter.h"

#include <QDateTime>

#include <zlib.h>

namespace pdfplugin
{

namespace
{

void writeUInt16(QByteArray& buffer, quint16 value)
{
    buffer.append(char(value & 0xFF));
    buffer.append(char((value >> 8) & 0xFF));
}

void writeUInt32(QByteArray& buffer, quint32 value)
{
    buffer.append(char(value & 0xFF));
    buffer.append(char((value >> 8) & 0xFF));
    buffer.append(char((value >> 16) & 0xFF));
    buffer.append(char((value >> 24) & 0xFF));
}

/// ZIP stores the modification time in the MS-DOS format (2 seconds resolution, local time)
void getDosDateTime(quint16& dosTime, quint16& dosDate)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate date = now.date();
    const QTime time = now.time();
    dosTime = quint16((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
    dosDate = quint16(((qMax(date.year(), 1980) - 1980) << 9) | (date.month() << 5) | date.day());
}

}   // namespace

void OdtZipWriter::addFile(const QString& name, const QByteArray& data, Method method)
{
    Entry entry;
    entry.name = name.toUtf8();
    entry.crc = crc32(data);
    entry.uncompressedSize = quint32(data.size());
    entry.method = method;

    if (method == Method::Deflated)
    {
        QByteArray compressed = deflateRaw(data);

        // Compression is pointless, when it does not save anything - store the data then
        // (a reader must handle both methods, so this is always valid)
        if (compressed.isEmpty() || compressed.size() >= data.size())
        {
            entry.method = Method::Stored;
            entry.data = data;
        }
        else
        {
            entry.data = std::move(compressed);
        }
    }
    else
    {
        entry.data = data;
    }

    m_entries.push_back(std::move(entry));
}

QByteArray OdtZipWriter::finish() const
{
    quint16 dosTime = 0;
    quint16 dosDate = 0;
    getDosDateTime(dosTime, dosDate);

    QByteArray archive;
    QByteArray centralDirectory;

    for (const Entry& entry : m_entries)
    {
        const quint32 localHeaderOffset = quint32(archive.size());
        const quint16 method = entry.method == Method::Deflated ? 8 : 0;

        // Local file header. No extra field: the OpenDocument specification asks for none
        // on "mimetype", so that the type can be read at a fixed offset 38 of the file.
        writeUInt32(archive, 0x04034b50);
        writeUInt16(archive, 20);                       // version needed to extract (2.0)
        writeUInt16(archive, 0);                        // flags (sizes are known in advance)
        writeUInt16(archive, method);
        writeUInt16(archive, dosTime);
        writeUInt16(archive, dosDate);
        writeUInt32(archive, entry.crc);
        writeUInt32(archive, quint32(entry.data.size()));
        writeUInt32(archive, entry.uncompressedSize);
        writeUInt16(archive, quint16(entry.name.size()));
        writeUInt16(archive, 0);                        // extra field length
        archive.append(entry.name);
        archive.append(entry.data);

        // Central directory record
        writeUInt32(centralDirectory, 0x02014b50);
        writeUInt16(centralDirectory, 20);              // version made by (MS-DOS, 2.0)
        writeUInt16(centralDirectory, 20);              // version needed to extract
        writeUInt16(centralDirectory, 0);
        writeUInt16(centralDirectory, method);
        writeUInt16(centralDirectory, dosTime);
        writeUInt16(centralDirectory, dosDate);
        writeUInt32(centralDirectory, entry.crc);
        writeUInt32(centralDirectory, quint32(entry.data.size()));
        writeUInt32(centralDirectory, entry.uncompressedSize);
        writeUInt16(centralDirectory, quint16(entry.name.size()));
        writeUInt16(centralDirectory, 0);               // extra field length
        writeUInt16(centralDirectory, 0);               // comment length
        writeUInt16(centralDirectory, 0);               // disk number
        writeUInt16(centralDirectory, 0);               // internal attributes
        writeUInt32(centralDirectory, 0);               // external attributes
        writeUInt32(centralDirectory, localHeaderOffset);
        centralDirectory.append(entry.name);
    }

    const quint32 centralDirectoryOffset = quint32(archive.size());
    archive.append(centralDirectory);

    // End of central directory record
    writeUInt32(archive, 0x06054b50);
    writeUInt16(archive, 0);
    writeUInt16(archive, 0);
    writeUInt16(archive, quint16(m_entries.size()));
    writeUInt16(archive, quint16(m_entries.size()));
    writeUInt32(archive, quint32(centralDirectory.size()));
    writeUInt32(archive, centralDirectoryOffset);
    writeUInt16(archive, 0);                            // comment length

    return archive;
}

quint32 OdtZipWriter::crc32(const QByteArray& data)
{
    uLong crc = ::crc32(0L, Z_NULL, 0);
    const Bytef* bytes = reinterpret_cast<const Bytef*>(data.constData());
    qsizetype remaining = data.size();

    // zlib takes the length as uInt - feed large data in chunks
    while (remaining > 0)
    {
        const uInt chunk = uInt(qMin<qsizetype>(remaining, 1 << 30));
        crc = ::crc32(crc, bytes, chunk);
        bytes += chunk;
        remaining -= chunk;
    }

    return quint32(crc);
}

QByteArray OdtZipWriter::deflateRaw(const QByteArray& data)
{
    z_stream stream = { };

    // Negative window bits = raw DEFLATE without the zlib header and Adler-32, as ZIP wants it
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        return QByteArray();
    }

    QByteArray result;
    result.resize(qsizetype(deflateBound(&stream, uLong(data.size()))));

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    stream.avail_in = uInt(data.size());
    stream.next_out = reinterpret_cast<Bytef*>(result.data());
    stream.avail_out = uInt(result.size());

    const int status = deflate(&stream, Z_FINISH);
    const qsizetype written = qsizetype(stream.total_out);
    deflateEnd(&stream);

    if (status != Z_STREAM_END)
    {
        return QByteArray();
    }

    result.resize(written);
    return result;
}

}   // namespace pdfplugin
