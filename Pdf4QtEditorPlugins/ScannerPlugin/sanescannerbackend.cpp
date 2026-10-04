// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "sanescannerbackend.h"

#include <sane/saneopts.h>

#include <QByteArray>
#include <QObject>

#include <algorithm>
#include <cstring>

namespace pdfplugin
{

namespace
{

// PDF Fire: what happened and what to do - SANE's own texts are terse
// ("Document feeder jammed"), and a too-full feeder ends exactly like this
QString getStatusMessage(SANE_Status status)
{
    switch (status)
    {
        case SANE_STATUS_JAMMED:
            return QObject::tr("The paper jammed in the document feeder. This often happens when the feeder is too full or "
                               "two sheets were pulled in at once. Clear the jam on the scanner, put the pages that were "
                               "not scanned back in (fewer at a time), and scan again.");
        case SANE_STATUS_COVER_OPEN:
            return QObject::tr("The scanner cover or the document feeder is open. Close it and scan again.");
        case SANE_STATUS_DEVICE_BUSY:
            return QObject::tr("The scanner is busy (another scan or a print job may be running). Wait a moment and scan again.");
        case SANE_STATUS_NO_DOCS:
            return QObject::tr("There is no paper in the document feeder.");
        case SANE_STATUS_IO_ERROR:
            return QObject::tr("The connection to the scanner failed (%1). Check that the scanner is switched on and connected, and scan again.").arg(QString::fromLocal8Bit(sane_strstatus(status)));
        default:
            return QString::fromLocal8Bit(sane_strstatus(status));
    }
}

}   // namespace

SaneScannerBackend::SaneScannerBackend()
{
    SANE_Int versionCode = 0;
    m_initialized = sane_init(&versionCode, nullptr) == SANE_STATUS_GOOD;
}

SaneScannerBackend::~SaneScannerBackend()
{
    if (m_initialized)
    {
        sane_exit();
    }
}

QString SaneScannerBackend::backendName() const
{
    return QStringLiteral("SANE");
}

std::vector<ScannerDevice> SaneScannerBackend::devices(QString* errorMessage)
{
    std::vector<ScannerDevice> result;
    if (errorMessage)
    {
        errorMessage->clear();
    }

    if (!m_initialized)
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("Failed to initialize SANE.");
        }
        return result;
    }

    const SANE_Device** deviceList = nullptr;
    const SANE_Status status = sane_get_devices(&deviceList, SANE_FALSE);
    if (status != SANE_STATUS_GOOD)
    {
        if (errorMessage)
        {
            *errorMessage = getStatusMessage(status);
        }
        return result;
    }

    for (int i = 0; deviceList && deviceList[i]; ++i)
    {
        const SANE_Device* saneDevice = deviceList[i];
        ScannerDevice device;
        device.id = QString::fromLocal8Bit(saneDevice->name);
        device.name = QString::fromLocal8Bit(saneDevice->name);
        device.vendor = QString::fromLocal8Bit(saneDevice->vendor);
        device.model = QString::fromLocal8Bit(saneDevice->model);
        result.push_back(std::move(device));
    }

    // PDF Fire: one network scanner is often found by several drivers. The driver
    // "airscan" is the most reliable for network scanners, the old driver "escl"
    // the least (it prints raw data to the terminal while searching) - the
    // first device is chosen in the dialog, so the reliable ones go first.
    auto getRank = [](const ScannerDevice& device)
    {
        if (device.id.startsWith(QLatin1String("airscan:")))
        {
            return 0;
        }
        if (device.id.startsWith(QLatin1String("escl:")))
        {
            return 3;
        }
        if (device.id.startsWith(QLatin1String("hpaio:")))
        {
            return 1;
        }
        return 2;
    };
    std::stable_sort(result.begin(), result.end(), [&getRank](const ScannerDevice& left, const ScannerDevice& right) { return getRank(left) < getRank(right); });

    return result;
}

QStringList SaneScannerBackend::sources(const QString& deviceId)
{
    QStringList result;
    if (!m_initialized)
    {
        return result;
    }

    SANE_Handle handle = nullptr;
    if (sane_open(deviceId.toLocal8Bit().constData(), &handle) != SANE_STATUS_GOOD)
    {
        return result;
    }

    const int option = findOption(handle, SANE_NAME_SCAN_SOURCE);
    if (option >= 0)
    {
        const SANE_Option_Descriptor* descriptor = sane_get_option_descriptor(handle, option);
        if (descriptor && descriptor->constraint_type == SANE_CONSTRAINT_STRING_LIST && descriptor->constraint.string_list)
        {
            for (int i = 0; descriptor->constraint.string_list[i]; ++i)
            {
                result << QString::fromLocal8Bit(descriptor->constraint.string_list[i]);
            }
        }
    }

    sane_close(handle);
    return result;
}

ScanResult SaneScannerBackend::scan(const ScanSettings& settings)
{
    ScanResult result;
    if (!m_initialized)
    {
        result.errorMessage = QObject::tr("Failed to initialize SANE.");
        return result;
    }

    m_isCancelled = false;
    SANE_Handle handle = nullptr;
    SANE_Status status = sane_open(settings.deviceId.toLocal8Bit().constData(), &handle);
    if (status != SANE_STATUS_GOOD)
    {
        result.errorMessage = getStatusMessage(status);
        return result;
    }
    m_activeHandle = handle;

    // PDF Fire: the source first - changing it can change the allowed resolutions
    // (HP Smart Tank 7600: flatbed up to 1200 dpi, feeder only up to 300 dpi, and
    // the driver silently lowers the resolution). The resolution the scanner
    // really uses is read back, otherwise the pages get a wrong size.
    if (!settings.source.isEmpty())
    {
        setOptionString(handle, SANE_NAME_SCAN_SOURCE, settings.source);
    }
    setOptionInt(handle, SANE_NAME_SCAN_RESOLUTION, settings.resolutionDpi);
    setOptionString(handle, SANE_NAME_SCAN_MODE, colorModeToScannerName(settings.colorMode));
    const int resolutionDpi = getOptionInt(handle, SANE_NAME_SCAN_RESOLUTION, settings.resolutionDpi);

    // PDF Fire: the scan area - set after the source, because the feeder and
    // the flatbed have different maximal areas (values are limited to them)
    if (settings.pageWidthMm > 0.0 && settings.pageHeightMm > 0.0)
    {
        setOptionLength(handle, SANE_NAME_SCAN_TL_X, 0.0, resolutionDpi);
        setOptionLength(handle, SANE_NAME_SCAN_TL_Y, 0.0, resolutionDpi);
        setOptionLength(handle, SANE_NAME_SCAN_BR_X, settings.pageWidthMm, resolutionDpi);
        setOptionLength(handle, SANE_NAME_SCAN_BR_Y, settings.pageHeightMm, resolutionDpi);
    }

    // PDF Fire: sane_cancel is called only once, after the last page - between the
    // pages of a document feeder it cancels the whole scan job (eSCL scanners then
    // answer the next sane_start with "Invalid argument")
    for (int pageIndex = 0; pageIndex < settings.pageCount && !m_isCancelled; ++pageIndex)
    {
        status = sane_start(handle);
        if (status == SANE_STATUS_NO_DOCS && pageIndex > 0)
        {
            break;
        }
        if (status == SANE_STATUS_NO_DOCS)
        {
            result.errorMessage = QObject::tr("There is no paper in the document feeder.");
            break;
        }
        if (status != SANE_STATUS_GOOD)
        {
            result.errorMessage = getStatusMessage(status);
            break;
        }

        QString errorMessage;
        QImage image = readImage(handle, resolutionDpi, &errorMessage);

        if (m_isCancelled)
        {
            result.errorMessage = QObject::tr("The scanning was cancelled.");
            break;
        }

        if (image.isNull())
        {
            result.errorMessage = errorMessage.isEmpty() ? QObject::tr("SANE returned an empty image.") : errorMessage;
            break;
        }

        ScannedPage page;
        page.image = std::move(image);
        page.dpiX = resolutionDpi;
        page.dpiY = resolutionDpi;
        result.pages.push_back(std::move(page));

        if (settings.pageScannedCallback)
        {
            settings.pageScannedCallback(int(result.pages.size()));
        }
    }

    sane_cancel(handle);
    m_activeHandle = nullptr;
    sane_close(handle);
    return result;
}

void SaneScannerBackend::cancel()
{
    // SANE allows sane_cancel to be called from another thread - the running
    // sane_read returns SANE_STATUS_CANCELLED then
    m_isCancelled = true;
    if (SANE_Handle handle = m_activeHandle.load())
    {
        sane_cancel(handle);
    }
}

bool SaneScannerBackend::setOptionInt(SANE_Handle handle, const char* name, int value)
{
    const int option = findOption(handle, name);
    if (option < 0)
    {
        return false;
    }

    const SANE_Option_Descriptor* descriptor = sane_get_option_descriptor(handle, option);
    if (descriptor && descriptor->type == SANE_TYPE_FIXED)
    {
        SANE_Fixed saneValue = SANE_FIX(value);
        return sane_control_option(handle, option, SANE_ACTION_SET_VALUE, &saneValue, nullptr) == SANE_STATUS_GOOD;
    }
    else
    {
        SANE_Int saneValue = value;
        return sane_control_option(handle, option, SANE_ACTION_SET_VALUE, &saneValue, nullptr) == SANE_STATUS_GOOD;
    }
}

bool SaneScannerBackend::setOptionString(SANE_Handle handle, const char* name, const QString& value)
{
    const int option = findOption(handle, name);
    if (option < 0)
    {
        return false;
    }

    QByteArray bytes = value.toLocal8Bit();
    return sane_control_option(handle, option, SANE_ACTION_SET_VALUE, bytes.data(), nullptr) == SANE_STATUS_GOOD;
}

bool SaneScannerBackend::setOptionLength(SANE_Handle handle, const char* name, double millimeters, int dpi)
{
    const int option = findOption(handle, name);
    if (option < 0)
    {
        return false;
    }

    const SANE_Option_Descriptor* descriptor = sane_get_option_descriptor(handle, option);
    if (!descriptor || !SANE_OPTION_IS_SETTABLE(descriptor->cap) || !SANE_OPTION_IS_ACTIVE(descriptor->cap) || descriptor->size != sizeof(SANE_Word))
    {
        return false;
    }

    double value = millimeters;
    if (descriptor->unit == SANE_UNIT_PIXEL)
    {
        value = millimeters / 25.4 * dpi;
    }
    else if (descriptor->unit != SANE_UNIT_MM)
    {
        return false;
    }

    const bool isFixed = descriptor->type == SANE_TYPE_FIXED;
    if (!isFixed && descriptor->type != SANE_TYPE_INT)
    {
        return false;
    }

    // Limited to the range of the scanner (a smaller feeder or glass)
    if (descriptor->constraint_type == SANE_CONSTRAINT_RANGE && descriptor->constraint.range)
    {
        const SANE_Range* range = descriptor->constraint.range;
        const double minimum = isFixed ? SANE_UNFIX(range->min) : range->min;
        const double maximum = isFixed ? SANE_UNFIX(range->max) : range->max;
        value = qBound(minimum, value, maximum);
    }

    SANE_Word saneValue = isFixed ? SANE_FIX(value) : SANE_Word(qRound(value));
    return sane_control_option(handle, option, SANE_ACTION_SET_VALUE, &saneValue, nullptr) == SANE_STATUS_GOOD;
}

int SaneScannerBackend::getOptionInt(SANE_Handle handle, const char* name, int defaultValue)
{
    const int option = findOption(handle, name);
    if (option < 0)
    {
        return defaultValue;
    }

    const SANE_Option_Descriptor* descriptor = sane_get_option_descriptor(handle, option);
    if (!descriptor || (descriptor->type != SANE_TYPE_INT && descriptor->type != SANE_TYPE_FIXED) || descriptor->size != sizeof(SANE_Word))
    {
        return defaultValue;
    }

    SANE_Word value = 0;
    if (sane_control_option(handle, option, SANE_ACTION_GET_VALUE, &value, nullptr) != SANE_STATUS_GOOD)
    {
        return defaultValue;
    }

    const int result = descriptor->type == SANE_TYPE_FIXED ? qRound(SANE_UNFIX(value)) : value;
    return result > 0 ? result : defaultValue;
}

int SaneScannerBackend::findOption(SANE_Handle handle, const char* name) const
{
    const SANE_Option_Descriptor* optionCountDescriptor = sane_get_option_descriptor(handle, 0);
    if (!optionCountDescriptor)
    {
        return -1;
    }

    SANE_Int optionCount = 0;
    if (sane_control_option(handle, 0, SANE_ACTION_GET_VALUE, &optionCount, nullptr) != SANE_STATUS_GOOD)
    {
        return -1;
    }

    for (int option = 1; option < optionCount; ++option)
    {
        const SANE_Option_Descriptor* descriptor = sane_get_option_descriptor(handle, option);
        if (descriptor && descriptor->name && qstrcmp(descriptor->name, name) == 0)
        {
            return option;
        }
    }

    return -1;
}

QImage SaneScannerBackend::readImage(SANE_Handle handle, int dpi, QString* errorMessage)
{
    SANE_Parameters parameters;
    SANE_Status status = sane_get_parameters(handle, &parameters);
    if (status != SANE_STATUS_GOOD)
    {
        if (errorMessage)
        {
            *errorMessage = getStatusMessage(status);
        }
        return QImage();
    }

    if (parameters.pixels_per_line <= 0 || parameters.bytes_per_line <= 0)
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("The scanner did not report a valid image size.");
        }
        return QImage();
    }

    // PDF Fire: the size is computed in 64 bits and limited (a broken driver can
    // report anything); a page of unknown length (lines = -1, document feeders)
    // is read in parts, until the end of the page
    constexpr qint64 MAXIMAL_IMAGE_BYTES = qint64(1) << 30;
    const bool isLengthKnown = parameters.lines > 0;
    const qint64 expectedSize = isLengthKnown ? qint64(parameters.bytes_per_line) * qint64(parameters.lines) : qint64(parameters.bytes_per_line) * 1024;
    if (expectedSize > MAXIMAL_IMAGE_BYTES)
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("The scanned image would be too large (%1 MB). Choose a lower resolution.").arg(expectedSize / (1024 * 1024));
        }
        return QImage();
    }

    QByteArray rawData(qsizetype(expectedSize), Qt::Uninitialized);
    qsizetype offset = 0;
    while (true)
    {
        if (offset == rawData.size())
        {
            if (isLengthKnown)
            {
                // PDF Fire: the page is complete only when sane_read reports EOF -
                // stopping at the reported size leaves the scan job unfinished
                // (eSCL document feeders then fail the next page). Extra data is
                // dropped.
                SANE_Byte drainBuffer[65536];
                SANE_Int drainedBytes = 0;
                do
                {
                    status = sane_read(handle, drainBuffer, SANE_Int(sizeof(drainBuffer)), &drainedBytes);
                } while (status == SANE_STATUS_GOOD && drainedBytes > 0);

                if (status != SANE_STATUS_EOF && status != SANE_STATUS_GOOD)
                {
                    if (errorMessage)
                    {
                        *errorMessage = getStatusMessage(status);
                    }
                    return QImage();
                }
                break;
            }
            if (rawData.size() * 2 > MAXIMAL_IMAGE_BYTES)
            {
                if (errorMessage)
                {
                    *errorMessage = QObject::tr("The scanned image is too large. Choose a lower resolution.");
                }
                return QImage();
            }
            rawData.resize(rawData.size() * 2);
        }

        SANE_Int bytesRead = 0;
        const SANE_Int maximalLength = SANE_Int(qMin<qsizetype>(rawData.size() - offset, 1 << 24));
        status = sane_read(handle, reinterpret_cast<SANE_Byte*>(rawData.data() + offset), maximalLength, &bytesRead);
        if (status == SANE_STATUS_EOF)
        {
            break;
        }
        if (status != SANE_STATUS_GOOD)
        {
            if (errorMessage)
            {
                *errorMessage = getStatusMessage(status);
            }
            return QImage();
        }
        if (bytesRead <= 0)
        {
            break;
        }

        offset += bytesRead;
    }

    if (!isLengthKnown)
    {
        parameters.lines = int(offset / parameters.bytes_per_line);
        if (parameters.lines <= 0)
        {
            if (errorMessage)
            {
                *errorMessage = QObject::tr("The scanner sent no image.");
            }
            return QImage();
        }
    }
    else if (offset < qsizetype(parameters.bytes_per_line) * parameters.lines)
    {
        // A shorter page than reported: the rest is white
        memset(rawData.data() + offset, 0xFF, rawData.size() - offset);
    }

    QImage image;
    if (parameters.format == SANE_FRAME_GRAY && parameters.depth == 8)
    {
        image = QImage(parameters.pixels_per_line, parameters.lines, QImage::Format_Grayscale8);
        for (int y = 0; y < parameters.lines; ++y)
        {
            memcpy(image.scanLine(y), rawData.constData() + y * parameters.bytes_per_line, parameters.pixels_per_line);
        }
    }
    else if (parameters.format == SANE_FRAME_GRAY && parameters.depth == 1)
    {
        image = QImage(reinterpret_cast<const uchar*>(rawData.constData()),
                       parameters.pixels_per_line,
                       parameters.lines,
                       parameters.bytes_per_line,
                       QImage::Format_Mono).copy();
    }
    else if (parameters.format == SANE_FRAME_RGB && parameters.depth == 8)
    {
        image = QImage(parameters.pixels_per_line, parameters.lines, QImage::Format_RGB888);
        for (int y = 0; y < parameters.lines; ++y)
        {
            memcpy(image.scanLine(y), rawData.constData() + y * parameters.bytes_per_line, parameters.pixels_per_line * 3);
        }
    }
    else
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("Unsupported SANE image format or bit depth.");
        }
        return QImage();
    }

    image.setDotsPerMeterX(int(dpi / 0.0254));
    image.setDotsPerMeterY(int(dpi / 0.0254));
    return image;
}

}   // namespace pdfplugin
