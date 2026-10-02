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

#include "pdffirewelcome.h"
#include "pdfwidgetutils.h"

#include <QAction>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QCheckBox>
#include <QCoreApplication>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "pdfdbgheap.h"

namespace pdfviewer
{

bool PDFFireWelcomeWidget::isShowingRecentDocuments()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    return settings.value("PDFFire/showRecentDocuments", true).toBool();
}

void PDFFireWelcomeWidget::setShowingRecentDocuments(bool show)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue("PDFFire/showRecentDocuments", show);
}

PDFFireWelcomeWidget::PDFFireWelcomeWidget(QAction* openAction, std::vector<QAction*> recentFileActions, QAction* clearRecentFilesAction, QWidget* parent) :
    QWidget(parent),
    m_recentFileActions(qMove(recentFileActions)),
    m_recentFilesWidget(new QWidget(this)),
    m_recentFilesLayout(new QVBoxLayout(m_recentFilesWidget)),
    m_clearRecentFilesAction(clearRecentFilesAction),
    m_showRecentCheckBox(new QCheckBox(tr("Show recent documents"), this)),
    m_clearRecentButton(new QPushButton(tr("Clear the list"), this))
{
    setObjectName("welcomeWidget");
    setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(pdf::PDFWidgetUtils::scaleDPI_y(this, 10));

    const int logoSize = pdf::PDFWidgetUtils::scaleDPI_x(this, 112);
    QLabel* logoLabel = new QLabel(this);
    QPixmap logo(":/pdffire/mark.png");
    logo = logo.scaled(QSize(logoSize, logoSize) * devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    logo.setDevicePixelRatio(devicePixelRatioF());
    logoLabel->setPixmap(logo);
    logoLabel->setAlignment(Qt::AlignCenter);

    QLabel* titleLabel = new QLabel(tr("PDF Fire"), this);
    titleLabel->setObjectName("welcomeTitle");
    titleLabel->setAlignment(Qt::AlignCenter);

    QLabel* subtitleLabel = new QLabel(tr("Open a document to start, or drop a PDF file here."), this);
    subtitleLabel->setObjectName("welcomeSubtitle");
    subtitleLabel->setAlignment(Qt::AlignCenter);

    QPushButton* openButton = new QPushButton(tr("Open a PDF..."), this);
    openButton->setObjectName("welcomeOpenButton");
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setFocusPolicy(Qt::NoFocus);
    connect(openButton, &QPushButton::clicked, openAction, &QAction::trigger);

    m_recentFilesWidget->setObjectName("welcomeRecentFiles");
    m_recentFilesWidget->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 520));
    m_recentFilesLayout->setContentsMargins(0, pdf::PDFWidgetUtils::scaleDPI_y(this, 18), 0, 0);
    m_recentFilesLayout->setSpacing(2);

    layout->addStretch(2);
    layout->addWidget(logoLabel);
    layout->addWidget(titleLabel);
    layout->addWidget(subtitleLabel);
    layout->addSpacing(pdf::PDFWidgetUtils::scaleDPI_y(this, 8));
    layout->addWidget(openButton, 0, Qt::AlignHCenter);
    layout->addWidget(m_recentFilesWidget, 0, Qt::AlignHCenter);

    // The recent documents can be hidden (and the list cleared), for example on a
    // computer shared with others
    m_showRecentCheckBox->setObjectName("welcomeShowRecentCheckBox");
    m_showRecentCheckBox->setChecked(isShowingRecentDocuments());
    m_clearRecentButton->setObjectName("welcomeClearRecentButton");
    m_clearRecentButton->setCursor(Qt::PointingHandCursor);
    m_clearRecentButton->setFocusPolicy(Qt::NoFocus);
    QHBoxLayout* recentOptionsLayout = new QHBoxLayout();
    recentOptionsLayout->addStretch(1);
    recentOptionsLayout->addWidget(m_showRecentCheckBox);
    recentOptionsLayout->addSpacing(pdf::PDFWidgetUtils::scaleDPI_x(this, 16));
    recentOptionsLayout->addWidget(m_clearRecentButton);
    recentOptionsLayout->addStretch(1);
    layout->addSpacing(pdf::PDFWidgetUtils::scaleDPI_y(this, 6));
    layout->addLayout(recentOptionsLayout);
    layout->addStretch(3);

    connect(m_showRecentCheckBox, &QCheckBox::toggled, this, [this](bool show)
    {
        setShowingRecentDocuments(show);
        refresh();
    });
    connect(m_clearRecentButton, &QPushButton::clicked, this, [this]()
    {
        if (m_clearRecentFilesAction)
        {
            m_clearRecentFilesAction->trigger();
        }
        refresh();
    });

    if (parent)
    {
        parent->installEventFilter(this);
        setGeometry(parent->rect());
    }

    refresh();
}

void PDFFireWelcomeWidget::refresh()
{
    while (QLayoutItem* item = m_recentFilesLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }

    const bool isShowing = isShowingRecentDocuments();

    bool hasRecentFiles = false;
    bool hasAnyRecentFile = false;
    for (QAction* action : m_recentFileActions)
    {
        const QString fileName = action->data().toString();
        if (fileName.isEmpty())
        {
            continue;
        }

        // The recent documents in the File menu follow the same choice
        hasAnyRecentFile = true;
        action->setVisible(isShowing);
        if (!isShowing)
        {
            continue;
        }

        if (!hasRecentFiles)
        {
            QLabel* headerLabel = new QLabel(tr("Recent documents"), m_recentFilesWidget);
            headerLabel->setObjectName("welcomeRecentHeader");
            m_recentFilesLayout->addWidget(headerLabel);
            hasRecentFiles = true;
        }

        const QFileInfo fileInfo(fileName);
        QPushButton* button = new QPushButton(m_recentFilesWidget);
        button->setObjectName("welcomeRecentButton");
        button->setText(QString("%1\n%2").arg(fileInfo.fileName(), fileInfo.absolutePath()));
        button->setToolTip(fileName);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        m_recentFilesLayout->addWidget(button);
    }

    m_recentFilesWidget->setVisible(hasRecentFiles);
    m_clearRecentButton->setVisible(hasAnyRecentFile);
}

bool PDFFireWelcomeWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parent() && event->type() == QEvent::Resize)
    {
        setGeometry(parentWidget()->rect());
    }

    return QWidget::eventFilter(watched, event);
}

}   // namespace pdfviewer
