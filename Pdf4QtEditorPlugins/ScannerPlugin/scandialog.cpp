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

#include "scandialog.h"

#include "pdfwidgetutils.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QEventLoop>
#include <QProgressDialog>
#include <QPixmap>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pdfplugin
{

ScanDialog::ScanDialog(QWidget* parent) :
    QDialog(parent),
    m_backend(createPlatformScannerBackend())
{
    setWindowTitle(tr("Scan Pages"));

    m_deviceComboBox = new QComboBox(this);
    m_sourceComboBox = new QComboBox(this);
    m_colorModeComboBox = new QComboBox(this);
    m_resolutionSpinBox = new QSpinBox(this);
    m_pageCountSpinBox = new QSpinBox(this);
    m_statusLabel = new QLabel(this);
    m_reloadButton = new QPushButton(tr("Reload Devices"), this);
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_scanButton = m_buttonBox->addButton(tr("Scan"), QDialogButtonBox::ActionRole);

    m_colorModeComboBox->addItem(tr("Color"), int(ScanSettings::ColorMode::Color));
    m_colorModeComboBox->addItem(tr("Grayscale"), int(ScanSettings::ColorMode::Grayscale));
    m_colorModeComboBox->addItem(tr("Lineart"), int(ScanSettings::ColorMode::Lineart));

    m_resolutionSpinBox->setRange(75, 1200);
    m_resolutionSpinBox->setSingleStep(75);
    m_resolutionSpinBox->setValue(300);
    m_resolutionSpinBox->setSuffix(tr(" dpi"));

    // PDF Fire: no page count - a document feeder scans until it is empty, a flatbed
    // scans one page per click (Add Page), and the pages are collected until Done
    m_pageCountSpinBox->setVisible(false);
    m_previewLabel = new QLabel(this);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumHeight(160);
    m_removeLastButton = new QPushButton(tr("Remove Last Page"), this);
    m_ocrCheckBox = new QCheckBox(tr("Make the scanned pages searchable (recognize the text - OCR)"), this);
    m_ocrCheckBox->setChecked(QSettings().value(QStringLiteral("ScannerPlugin/RecognizeText"), true).toBool());
    m_ocrCheckBox->setVisible(false);

    QFormLayout* formLayout = new QFormLayout();
    formLayout->addRow(tr("Device:"), m_deviceComboBox);
    formLayout->addRow(tr("Source:"), m_sourceComboBox);
    formLayout->addRow(tr("Color mode:"), m_colorModeComboBox);
    formLayout->addRow(tr("Resolution:"), m_resolutionSpinBox);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(m_reloadButton);
    mainLayout->addWidget(m_ocrCheckBox);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_previewLabel);
    mainLayout->addWidget(m_removeLastButton);
    mainLayout->addWidget(m_buttonBox);

    connect(m_reloadButton, &QPushButton::clicked, this, &ScanDialog::reloadDevices);
    connect(m_deviceComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &ScanDialog::updateSources);
    connect(m_scanButton, &QPushButton::clicked, this, &ScanDialog::scan);
    connect(m_removeLastButton, &QPushButton::clicked, this, &ScanDialog::removeLastPage);
    connect(m_sourceComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &ScanDialog::updateScanButtons);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &ScanDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &ScanDialog::reject);

    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    updateScanButtons();

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(520, 260));
    pdf::PDFWidgetUtils::style(this);

    // The search starts after the dialog is shown (it can take a while)
    QTimer::singleShot(0, this, &ScanDialog::reloadDevices);
}

void ScanDialog::runInBackground(const QString& message, bool isCancellable, const std::function<void()>& work)
{
    QProgressDialog progress(message, isCancellable ? tr("Cancel") : QString(), 0, 0, this);
    progress.setWindowTitle(windowTitle());
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    if (!isCancellable)
    {
        progress.setCancelButton(nullptr);
    }

    QEventLoop loop;
    QThread* thread = QThread::create(work);
    connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    connect(&progress, &QProgressDialog::canceled, this, [this, &progress]()
    {
        progress.setLabelText(tr("Cancelling..."));
        if (m_backend)
        {
            m_backend->cancel();
        }
    });

    QApplication::setOverrideCursor(Qt::BusyCursor);
    thread->start();
    loop.exec();
    thread->wait();
    delete thread;
    QApplication::restoreOverrideCursor();
}

ScanDialog::~ScanDialog() = default;

void ScanDialog::setOcrAvailable(bool available)
{
    m_ocrCheckBox->setVisible(available);
}

bool ScanDialog::isOcrRequested() const
{
    QSettings().setValue(QStringLiteral("ScannerPlugin/RecognizeText"), m_ocrCheckBox->isChecked());
    return m_ocrCheckBox->isVisible() && m_ocrCheckBox->isChecked();
}

std::vector<ScannedPage> ScanDialog::takePages()
{
    return std::move(m_pages);
}

void ScanDialog::reloadDevices()
{
    m_devices.clear();
    m_deviceComboBox->clear();
    m_sourceComboBox->clear();
    m_statusLabel->clear();
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    if (!m_backend)
    {
        m_scanButton->setEnabled(false);
        m_statusLabel->setText(tr("No scanner backend is available for this platform."));
        return;
    }

    QString errorMessage;
    std::vector<ScannerDevice> devices;
    runInBackground(tr("Looking for scanners (also on the network)..."), false, [this, &devices, &errorMessage]() { devices = m_backend->devices(&errorMessage); });
    m_devices = std::move(devices);

    for (const ScannerDevice& device : m_devices)
    {
        QString label = device.name;
        if (!device.vendor.isEmpty() || !device.model.isEmpty())
        {
            label = tr("%1 (%2 %3)").arg(device.name, device.vendor, device.model).simplified();
        }
        m_deviceComboBox->addItem(label, device.id);
    }

    m_scanButton->setEnabled(!m_devices.empty());
    if (m_devices.empty())
    {
        m_statusLabel->setText(errorMessage.isEmpty() ? tr("No scanner devices found.") : errorMessage);
    }
    else
    {
        m_statusLabel->setText(tr("%1 scanner backend ready.").arg(m_backend->backendName()));
    }

    updateSources();
}

void ScanDialog::updateSources()
{
    m_sourceComboBox->clear();
    if (!m_backend || m_deviceComboBox->currentIndex() < 0)
    {
        return;
    }

    QStringList sources;
    const QString deviceId = m_deviceComboBox->currentData().toString();
    runInBackground(tr("Connecting to the scanner..."), false, [this, &sources, deviceId]() { sources = m_backend->sources(deviceId); });
    if (sources.empty())
    {
        m_sourceComboBox->addItem(tr("Default"), QString());
        return;
    }

    for (const QString& source : sources)
    {
        m_sourceComboBox->addItem(source, source);
    }
}

void ScanDialog::scan()
{
    if (!m_backend)
    {
        return;
    }

    ScanResult result;
    const ScanSettings settings = getSettings();
    runInBackground(tr("Scanning... (a page can take a minute)"), true, [this, &result, settings]() { result = m_backend->scan(settings); });

    if (!result)
    {
        QMessageBox::critical(this, tr("Scanner Error"), result.errorMessage);
        return;
    }

    // The pages are added to the pages scanned before (Add Page on a flatbed)
    for (ScannedPage& page : result.pages)
    {
        m_pages.push_back(std::move(page));
    }
    updateScanButtons();
}

bool ScanDialog::isFeederSource() const
{
    const QString source = m_sourceComboBox->currentText();
    return source.contains(QLatin1String("ADF"), Qt::CaseInsensitive) ||
           source.contains(QLatin1String("Feeder"), Qt::CaseInsensitive) ||
           source.contains(QLatin1String("Duplex"), Qt::CaseInsensitive);
}

void ScanDialog::removeLastPage()
{
    if (!m_pages.empty())
    {
        m_pages.pop_back();
    }
    updateScanButtons();
}

void ScanDialog::updateScanButtons()
{
    const bool hasPages = !m_pages.empty();
    if (isFeederSource())
    {
        m_scanButton->setText(hasPages ? tr("Scan More") : tr("Scan All Pages"));
        m_scanButton->setToolTip(tr("Scans every page in the document feeder"));
    }
    else
    {
        m_scanButton->setText(hasPages ? tr("Add Page") : tr("Scan Page"));
        m_scanButton->setToolTip(tr("Scans the page on the glass. Put the next page on the glass and click Add Page."));
    }

    QPushButton* doneButton = m_buttonBox->button(QDialogButtonBox::Ok);
    doneButton->setEnabled(hasPages);
    doneButton->setText(!hasPages ? tr("Done") : (m_pages.size() == 1 ? tr("Done - Insert 1 Page") : tr("Done - Insert %1 Pages").arg(m_pages.size())));
    m_removeLastButton->setVisible(hasPages);

    if (hasPages)
    {
        m_statusLabel->setText(m_pages.size() == 1 ? tr("1 page scanned.") : tr("%1 pages scanned.").arg(m_pages.size()));
        const QImage& image = m_pages.back().image;
        m_previewLabel->setPixmap(QPixmap::fromImage(image.scaled(QSize(200, 160), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    else
    {
        m_previewLabel->clear();
    }
}

ScanSettings ScanDialog::getSettings() const
{
    ScanSettings settings;
    settings.deviceId = m_deviceComboBox->currentData().toString();
    settings.source = m_sourceComboBox->currentData().toString();
    settings.resolutionDpi = m_resolutionSpinBox->value();
    settings.pageCount = isFeederSource() ? 9999 : 1;
    settings.colorMode = static_cast<ScanSettings::ColorMode>(m_colorModeComboBox->currentData().toInt());
    return settings;
}

}   // namespace pdfplugin
