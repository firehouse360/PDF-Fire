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
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTransform>
#include <QWheelEvent>
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

    // PDF Fire: the paper size - a document feeder that cannot detect the paper
    // length scans the whole area (letter → legal-length page with a blank strip)
    m_pageSizeComboBox = new QComboBox(this);
    m_pageSizeComboBox->addItem(tr("Letter (8.5 x 11 in)"), QStringLiteral("letter"));
    m_pageSizeComboBox->addItem(tr("Legal (8.5 x 14 in)"), QStringLiteral("legal"));
    m_pageSizeComboBox->addItem(tr("A4 (210 x 297 mm)"), QStringLiteral("a4"));
    m_pageSizeComboBox->addItem(tr("Whole scan area"), QStringLiteral("full"));
    const int pageSizeIndex = m_pageSizeComboBox->findData(QSettings().value(QStringLiteral("ScannerPlugin/PageSize"), QStringLiteral("letter")).toString());
    m_pageSizeComboBox->setCurrentIndex(qMax(pageSizeIndex, 0));
    connect(m_pageSizeComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]()
    {
        QSettings().setValue(QStringLiteral("ScannerPlugin/PageSize"), m_pageSizeComboBox->currentData().toString());
    });

    m_resolutionSpinBox->setRange(75, 1200);
    m_resolutionSpinBox->setSingleStep(75);
    m_resolutionSpinBox->setValue(300);
    m_resolutionSpinBox->setSuffix(tr(" dpi"));

    // PDF Fire: no page count - a document feeder scans until it is empty, a flatbed
    // scans one page per click (Add Page), and the pages are collected until Done
    m_pageCountSpinBox->setVisible(false);
    m_previewLabel = new QLabel(this);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumHeight(pdf::PDFWidgetUtils::scaleDPI_y(this, 300));
    m_previewLabel->setToolTip(tr("Scroll the mouse wheel here to go through the pages"));
    m_previewLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_previewLabel->installEventFilter(this);

    // PDF Fire: the scanned pages as thumbnails in one row (mouse wheel scrolls it)
    m_pageList = new QListWidget(this);
    m_pageList->setViewMode(QListView::IconMode);
    m_pageList->setFlow(QListView::LeftToRight);
    m_pageList->setWrapping(false);
    m_pageList->setMovement(QListView::Static);
    m_pageList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pageList->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_pageList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pageList->setIconSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(72, 93)));
    m_pageList->setSpacing(4);
    m_pageList->setFixedHeight(m_pageList->iconSize().height() + pdf::PDFWidgetUtils::scaleDPI_y(this, 52));
    m_pageList->viewport()->installEventFilter(this);
    m_pageList->installEventFilter(this);

    m_pageTools = new QWidget(this);
    QHBoxLayout* pageToolsLayout = new QHBoxLayout(m_pageTools);
    pageToolsLayout->setContentsMargins(0, 0, 0, 0);
    m_previousPageButton = new QPushButton(QStringLiteral("◀"), m_pageTools);
    m_previousPageButton->setToolTip(tr("Previous page"));
    m_nextPageButton = new QPushButton(QStringLiteral("▶"), m_pageTools);
    m_nextPageButton->setToolTip(tr("Next page"));
    m_pageNumberLabel = new QLabel(m_pageTools);
    m_pageNumberLabel->setAlignment(Qt::AlignCenter);
    QPushButton* rotateLeftButton = new QPushButton(QStringLiteral("⟲"), m_pageTools);
    rotateLeftButton->setToolTip(tr("Rotate the page left"));
    QPushButton* rotateRightButton = new QPushButton(QStringLiteral("⟳"), m_pageTools);
    rotateRightButton->setToolTip(tr("Rotate the page right"));
    m_moveEarlierButton = new QPushButton(tr("Move Earlier"), m_pageTools);
    m_moveEarlierButton->setToolTip(tr("Move the page one place to the front"));
    m_moveLaterButton = new QPushButton(tr("Move Later"), m_pageTools);
    m_moveLaterButton->setToolTip(tr("Move the page one place to the back"));
    QPushButton* deleteButton = new QPushButton(tr("Delete Page"), m_pageTools);
    deleteButton->setToolTip(tr("Remove the page from the scan (Delete key)"));
    for (QPushButton* button : { rotateLeftButton, rotateRightButton })
    {
        QFont font = button->font();
        font.setPointSizeF(font.pointSizeF() * 1.5);
        button->setFont(font);
    }
    pageToolsLayout->addWidget(m_previousPageButton);
    pageToolsLayout->addWidget(m_pageNumberLabel, 1);
    pageToolsLayout->addWidget(m_nextPageButton);
    pageToolsLayout->addSpacing(pdf::PDFWidgetUtils::scaleDPI_x(this, 12));
    pageToolsLayout->addWidget(rotateLeftButton);
    pageToolsLayout->addWidget(rotateRightButton);
    pageToolsLayout->addWidget(m_moveEarlierButton);
    pageToolsLayout->addWidget(m_moveLaterButton);
    pageToolsLayout->addWidget(deleteButton);

    connect(m_pageList, &QListWidget::currentRowChanged, this, &ScanDialog::updatePagePreview);
    connect(m_previousPageButton, &QPushButton::clicked, this, [this]() { showNeighbourPage(-1); });
    connect(m_nextPageButton, &QPushButton::clicked, this, [this]() { showNeighbourPage(+1); });
    connect(rotateLeftButton, &QPushButton::clicked, this, [this]() { rotateCurrentPage(-90); });
    connect(rotateRightButton, &QPushButton::clicked, this, [this]() { rotateCurrentPage(+90); });
    connect(m_moveEarlierButton, &QPushButton::clicked, this, [this]() { moveCurrentPage(-1); });
    connect(m_moveLaterButton, &QPushButton::clicked, this, [this]() { moveCurrentPage(+1); });
    connect(deleteButton, &QPushButton::clicked, this, &ScanDialog::deleteCurrentPage);

    m_ocrCheckBox = new QCheckBox(tr("Make the scanned pages searchable (recognize the text - OCR)"), this);
    m_ocrCheckBox->setChecked(QSettings().value(QStringLiteral("ScannerPlugin/RecognizeText"), true).toBool());
    m_ocrCheckBox->setVisible(false);

    QFormLayout* formLayout = new QFormLayout();
    formLayout->addRow(tr("Device:"), m_deviceComboBox);
    formLayout->addRow(tr("Source:"), m_sourceComboBox);
    formLayout->addRow(tr("Page size:"), m_pageSizeComboBox);
    formLayout->addRow(tr("Color mode:"), m_colorModeComboBox);
    formLayout->addRow(tr("Resolution:"), m_resolutionSpinBox);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(m_reloadButton);
    mainLayout->addWidget(m_ocrCheckBox);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_previewLabel, 1);
    mainLayout->addWidget(m_pageTools);
    mainLayout->addWidget(m_pageList);
    mainLayout->addWidget(m_buttonBox);

    connect(m_reloadButton, &QPushButton::clicked, this, &ScanDialog::reloadDevices);
    connect(m_deviceComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &ScanDialog::updateSources);
    connect(m_scanButton, &QPushButton::clicked, this, &ScanDialog::scan);
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
    // PDF Fire: the controls are switched off while the work runs. The progress window
    // appears only after a moment, and until then the buttons still took clicks: a second
    // click on Scan started a second scan inside the first one.
    m_isBusy = true;
    std::vector<std::pair<QPointer<QWidget>, bool>> controls;
    for (QWidget* control : std::initializer_list<QWidget*>{ m_deviceComboBox, m_sourceComboBox, m_colorModeComboBox, m_pageSizeComboBox,
                                                             m_resolutionSpinBox, m_reloadButton, m_scanButton, m_buttonBox,
                                                             m_pageTools, m_pageList, m_ocrCheckBox })
    {
        if (control)
        {
            controls.emplace_back(control, control->isEnabled());
            control->setEnabled(false);
        }
    }

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
    m_activeProgress = &progress;
    thread->start();
    loop.exec();
    m_activeProgress = nullptr;
    thread->wait();
    delete thread;
    QApplication::restoreOverrideCursor();

    for (const auto& [control, isEnabled] : controls)
    {
        if (control)
        {
            control->setEnabled(isEnabled);
        }
    }
    m_isBusy = false;
}

void ScanDialog::reject()
{
    if (m_isBusy)
    {
        // The scan is cancelled by the Cancel button of the progress window
        if (m_activeProgress)
        {
            m_activeProgress->raise();
            m_activeProgress->activateWindow();
        }
        return;
    }

    if (!m_pages.empty())
    {
        const QString question = m_pages.size() == 1 ? tr("Throw away the scanned page?")
                                                     : tr("Throw away the %1 scanned pages?").arg(m_pages.size());
        if (QMessageBox::question(this, tr("Scan Pages"), question + QLatin1Char(' ') + tr("To keep them, click No and then Done."),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        {
            return;
        }
    }

    QDialog::reject();
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
    m_thumbnails.clear();
    return std::move(m_pages);
}

void ScanDialog::reloadDevices()
{
    if (m_isBusy)
    {
        return;
    }

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
    if (!m_backend || m_isBusy)
    {
        return;
    }

    ScanResult result;
    ScanSettings settings = getSettings();

    // PDF Fire: the number of pages scanned so far is shown while a feeder runs
    QPointer<ScanDialog> self(this);
    settings.pageScannedCallback = [self](int pageCount)
    {
        QMetaObject::invokeMethod(self.data(), [self, pageCount]()
        {
            if (self && self->m_activeProgress)
            {
                self->m_activeProgress->setLabelText(pageCount == 1 ? tr("Scanning... 1 page so far") : tr("Scanning... %1 pages so far").arg(pageCount));
            }
        }, Qt::QueuedConnection);
    };
    runInBackground(tr("Scanning... (a page can take a minute)"), true, [this, &result, settings]() { result = m_backend->scan(settings); });

    // The pages scanned before an error (for example a paper jam) are kept
    const int firstNewPage = int(m_pages.size());
    for (ScannedPage& page : result.pages)
    {
        m_pages.push_back(std::move(page));
    }

    // The pages are added to the pages scanned before (Add Page on a flatbed),
    // the first new page is shown
    if (int(m_pages.size()) > firstNewPage)
    {
        rebuildPageList(firstNewPage);
    }
    updateScanButtons();

    if (!result)
    {
        QString message = result.errorMessage;
        const int keptPageCount = int(m_pages.size()) - firstNewPage;
        if (keptPageCount > 0)
        {
            message += QStringLiteral("\n\n");
            message += keptPageCount == 1 ? tr("The page scanned before this was kept.")
                                          : tr("The %1 pages scanned before this were kept.").arg(keptPageCount);
            if (isFeederSource())
            {
                message += QLatin1Char(' ') + tr("Click Scan More to add the rest - check the last page in the preview to see where the scan stopped.");
            }
        }
        QMessageBox::critical(this, tr("Scanner Error"), message);
    }
}

bool ScanDialog::eventFilter(QObject* watched, QEvent* event)
{
    // The mouse wheel goes through the pages - over the preview it shows the
    // next/previous page, over the thumbnails it scrolls the row sideways
    if (event->type() == QEvent::Wheel)
    {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        const QPoint delta = wheelEvent->angleDelta();
        const int steps = qAbs(delta.y()) >= qAbs(delta.x()) ? delta.y() : delta.x();

        if (watched == m_previewLabel)
        {
            if (steps != 0)
            {
                showNeighbourPage(steps < 0 ? +1 : -1);
            }
            return true;
        }

        if (watched == m_pageList->viewport() && delta.y() != 0 && delta.x() == 0)
        {
            QScrollBar* scrollBar = m_pageList->horizontalScrollBar();
            scrollBar->setValue(scrollBar->value() - delta.y());
            return true;
        }
    }

    // The preview is made for the size of the window
    if (event->type() == QEvent::Resize && watched == m_previewLabel)
    {
        QTimer::singleShot(0, this, &ScanDialog::updatePagePreview);
    }

    if (event->type() == QEvent::KeyPress && watched == m_pageList)
    {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Delete)
        {
            deleteCurrentPage();
            return true;
        }
    }

    return QDialog::eventFilter(watched, event);
}

void ScanDialog::rebuildPageList(int selectedIndex)
{
    // The thumbnails are made once per page (the images are large), the list
    // only gets them in the current order
    const QSize iconSize = m_pageList->iconSize() * m_pageList->devicePixelRatioF();
    m_thumbnails.resize(m_pages.size());
    for (size_t i = 0; i < m_pages.size(); ++i)
    {
        if (m_thumbnails[i].isNull())
        {
            QPixmap thumbnail = QPixmap::fromImage(m_pages[i].image.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            thumbnail.setDevicePixelRatio(m_pageList->devicePixelRatioF());
            m_thumbnails[i] = thumbnail;
        }
    }

    QSignalBlocker blocker(m_pageList);
    m_pageList->clear();
    for (size_t i = 0; i < m_pages.size(); ++i)
    {
        QListWidgetItem* item = new QListWidgetItem(QIcon(m_thumbnails[i]), QString::number(i + 1), m_pageList);
        item->setTextAlignment(Qt::AlignHCenter);
    }

    if (!m_pages.empty())
    {
        m_pageList->setCurrentRow(qBound(0, selectedIndex, int(m_pages.size()) - 1));
        m_pageList->scrollToItem(m_pageList->currentItem());
    }
    blocker.unblock();
    updatePagePreview();
}

int ScanDialog::currentPageIndex() const
{
    const int row = m_pageList->currentRow();
    return (row >= 0 && row < int(m_pages.size())) ? row : -1;
}

void ScanDialog::updatePagePreview()
{
    const int index = currentPageIndex();
    const int pageCount = int(m_pages.size());
    if (index < 0)
    {
        m_previewLabel->clear();
        m_pageNumberLabel->clear();
        return;
    }

    const qreal ratio = m_previewLabel->devicePixelRatioF();
    const QSize size = QSize(m_previewLabel->width(), m_previewLabel->height()) * ratio;
    QPixmap pixmap = QPixmap::fromImage(m_pages[index].image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    pixmap.setDevicePixelRatio(ratio);
    m_previewLabel->setPixmap(pixmap);

    m_pageNumberLabel->setText(tr("Page %1 of %2").arg(index + 1).arg(pageCount));
    m_previousPageButton->setEnabled(index > 0);
    m_nextPageButton->setEnabled(index + 1 < pageCount);
    m_moveEarlierButton->setEnabled(index > 0);
    m_moveLaterButton->setEnabled(index + 1 < pageCount);
    m_pageList->scrollToItem(m_pageList->currentItem());
}

void ScanDialog::showNeighbourPage(int delta)
{
    const int index = currentPageIndex();
    if (index >= 0)
    {
        m_pageList->setCurrentRow(qBound(0, index + delta, int(m_pages.size()) - 1));
    }
}

void ScanDialog::rotateCurrentPage(int degrees)
{
    const int index = currentPageIndex();
    if (index < 0)
    {
        return;
    }

    ScannedPage& page = m_pages[index];
    page.image = page.image.transformed(QTransform().rotate(degrees));
    std::swap(page.dpiX, page.dpiY);
    m_thumbnails[index] = QPixmap();
    rebuildPageList(index);
}

void ScanDialog::moveCurrentPage(int delta)
{
    const int index = currentPageIndex();
    const int newIndex = index + delta;
    if (index < 0 || newIndex < 0 || newIndex >= int(m_pages.size()))
    {
        return;
    }

    std::swap(m_pages[index], m_pages[newIndex]);
    std::swap(m_thumbnails[index], m_thumbnails[newIndex]);
    rebuildPageList(newIndex);
}

void ScanDialog::deleteCurrentPage()
{
    const int index = currentPageIndex();
    if (index < 0)
    {
        return;
    }

    m_pages.erase(m_pages.begin() + index);
    m_thumbnails.erase(m_thumbnails.begin() + index);
    rebuildPageList(index);
    updateScanButtons();
}

bool ScanDialog::isFeederSource() const
{
    const QString source = m_sourceComboBox->currentText();
    return source.contains(QLatin1String("ADF"), Qt::CaseInsensitive) ||
           source.contains(QLatin1String("Feeder"), Qt::CaseInsensitive) ||
           source.contains(QLatin1String("Duplex"), Qt::CaseInsensitive);
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
    m_pageTools->setVisible(hasPages);
    m_pageList->setVisible(hasPages);
    m_previewLabel->setVisible(hasPages);

    if (hasPages)
    {
        m_statusLabel->setText(m_pages.size() == 1 ? tr("1 page scanned.") : tr("%1 pages scanned.").arg(m_pages.size()));
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

    const QString pageSize = m_pageSizeComboBox->currentData().toString();
    if (pageSize == QLatin1String("letter"))
    {
        settings.pageWidthMm = 215.9;
        settings.pageHeightMm = 279.4;
    }
    else if (pageSize == QLatin1String("legal"))
    {
        settings.pageWidthMm = 215.9;
        settings.pageHeightMm = 355.6;
    }
    else if (pageSize == QLatin1String("a4"))
    {
        settings.pageWidthMm = 210.0;
        settings.pageHeightMm = 297.0;
    }
    return settings;
}

}   // namespace pdfplugin
