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

#ifndef SCANDIALOG_H
#define SCANDIALOG_H

#include "scannerbackend.h"

#include <QDialog>
#include <QPixmap>

#include <functional>

#include <memory>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QListWidget;
class QProgressDialog;
class QPushButton;
class QSpinBox;

namespace pdfplugin
{

class ScanDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScanDialog(QWidget* parent);
    virtual ~ScanDialog() override;

    std::vector<ScannedPage> takePages();

    /// PDF Fire: the text recognition (OCR) of the scanned pages - offered, when it is installed
    void setOcrAvailable(bool available);
    bool isOcrRequested() const;

    /// PDF Fire: closing the window while the scanner works is refused (the scan
    /// must be cancelled first), and scanned pages are not thrown away unasked
    virtual void reject() override;

private:
    void reloadDevices();
    void updateSources();

    /// PDF Fire: runs the work of the scanner in another thread - searching the network
    /// and scanning take long, and the window must not freeze meanwhile (a frozen
    /// window is offered to be killed by the desktop)
    void runInBackground(const QString& message, bool isCancellable, const std::function<void()>& work);
    void scan();
    ScanSettings getSettings() const;

    /// PDF Fire: is the chosen source a document feeder (it scans until it is empty)?
    bool isFeederSource() const;
    void updateScanButtons();

    // PDF Fire: the scanned pages - a strip of thumbnails and a preview of the
    // selected page; the pages can be browsed (buttons, mouse wheel, arrow keys),
    // rotated, moved and deleted before they are inserted
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    void rebuildPageList(int selectedIndex);
    void updatePagePreview();
    int currentPageIndex() const;
    void showNeighbourPage(int delta);
    void rotateCurrentPage(int degrees);
    void moveCurrentPage(int delta);
    void deleteCurrentPage();

    std::unique_ptr<ScannerBackend> m_backend;
    std::vector<ScannerDevice> m_devices;
    std::vector<ScannedPage> m_pages;
    std::vector<QPixmap> m_thumbnails;

    QComboBox* m_deviceComboBox = nullptr;
    QComboBox* m_sourceComboBox = nullptr;
    QComboBox* m_colorModeComboBox = nullptr;
    QComboBox* m_pageSizeComboBox = nullptr;
    QSpinBox* m_resolutionSpinBox = nullptr;
    QSpinBox* m_pageCountSpinBox = nullptr;
    QLabel* m_statusLabel = nullptr;
    QCheckBox* m_ocrCheckBox = nullptr;
    QLabel* m_previewLabel = nullptr;
    QListWidget* m_pageList = nullptr;
    QWidget* m_pageTools = nullptr;
    QLabel* m_pageNumberLabel = nullptr;
    QPushButton* m_previousPageButton = nullptr;
    QPushButton* m_nextPageButton = nullptr;
    QPushButton* m_moveEarlierButton = nullptr;
    QPushButton* m_moveLaterButton = nullptr;
    QProgressDialog* m_activeProgress = nullptr;

    /// PDF Fire: the scanner is working (searching, connecting, scanning) - a second
    /// click must not start a second scan: the scanner answers "busy", and the error
    /// window then got stuck under the progress window of the first scan (the app froze)
    bool m_isBusy = false;
    QPushButton* m_reloadButton = nullptr;
    QPushButton* m_scanButton = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};

}   // namespace pdfplugin

#endif // SCANDIALOG_H
