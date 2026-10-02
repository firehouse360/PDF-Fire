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

#include "pdffireribbon.h"
#include "pdfwidgetutils.h"

#include <QAction>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFFireRibbonGroup::PDFFireRibbonGroup(const QString& title, QWidget* parent) :
    QWidget(parent),
    m_gridLayout(nullptr)
{
    setObjectName("ribbonGroup");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 2);
    layout->setSpacing(2);

    m_gridLayout = new QGridLayout();
    m_gridLayout->setContentsMargins(0, 0, 0, 0);
    m_gridLayout->setHorizontalSpacing(2);
    m_gridLayout->setVerticalSpacing(0);
    layout->addLayout(m_gridLayout, 1);

    QLabel* titleLabel = new QLabel(title, this);
    titleLabel->setObjectName("ribbonGroupTitle");
    titleLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    layout->addWidget(titleLabel, 0);
}

QToolButton* PDFFireRibbonGroup::createLargeButton()
{
    QToolButton* button = new QToolButton(this);
    button->setObjectName("ribbonLargeButton");
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(28, 28)));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    button->setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 54));
    return button;
}

QToolButton* PDFFireRibbonGroup::createSmallButton(bool showText)
{
    QToolButton* button = new QToolButton(this);
    button->setObjectName("ribbonSmallButton");
    button->setToolButtonStyle(showText ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    button->setIconSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(18, 18)));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setSizePolicy(showText ? QSizePolicy::Expanding : QSizePolicy::Fixed, QSizePolicy::Fixed);
    return button;
}

void PDFFireRibbonGroup::placeFullHeightWidget(QWidget* widget)
{
    if (m_row > 0)
    {
        // Finish the column of small buttons
        ++m_column;
        m_row = 0;
    }

    m_gridLayout->addWidget(widget, 0, m_column, ROWS, 1);
    ++m_column;
    ++m_itemCount;
}

void PDFFireRibbonGroup::placeSmallButton(QToolButton* button)
{
    m_gridLayout->addWidget(button, m_row, m_column, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
    ++m_itemCount;

    if (++m_row == ROWS)
    {
        m_row = 0;
        ++m_column;
    }
}

QToolButton* PDFFireRibbonGroup::addLargeAction(QAction* action)
{
    if (!action)
    {
        return nullptr;
    }

    QToolButton* button = createLargeButton();
    button->setDefaultAction(action);
    placeFullHeightWidget(button);
    return button;
}

QToolButton* PDFFireRibbonGroup::addLargeMenu(const QIcon& icon, const QString& text, QMenu* menu)
{
    if (!menu)
    {
        return nullptr;
    }

    QToolButton* button = createLargeButton();
    button->setIcon(icon);
    button->setText(text);
    button->setToolTip(text);
    button->setMenu(menu);
    button->setPopupMode(QToolButton::InstantPopup);
    placeFullHeightWidget(button);
    return button;
}

QToolButton* PDFFireRibbonGroup::addSmallAction(QAction* action, bool showText)
{
    if (!action)
    {
        return nullptr;
    }

    QToolButton* button = createSmallButton(showText);
    button->setDefaultAction(action);
    placeSmallButton(button);
    return button;
}

QToolButton* PDFFireRibbonGroup::addSmallMenu(const QIcon& icon, const QString& text, QMenu* menu)
{
    if (!menu)
    {
        return nullptr;
    }

    QToolButton* button = createSmallButton(true);
    button->setIcon(icon);
    button->setText(text);
    button->setToolTip(text);
    button->setMenu(menu);
    button->setPopupMode(QToolButton::InstantPopup);
    placeSmallButton(button);
    return button;
}

void PDFFireRibbonGroup::addWidget(QWidget* widget)
{
    if (widget)
    {
        widget->setParent(this);
        placeFullHeightWidget(widget);
    }
}

void PDFFireRibbonGroup::breakColumn()
{
    if (m_row > 0)
    {
        ++m_column;
        m_row = 0;
    }
}

PDFFireRibbonPage::PDFFireRibbonPage(QWidget* parent) :
    QWidget(parent),
    m_layout(new QHBoxLayout(this))
{
    setObjectName("ribbonPage");
    m_layout->setContentsMargins(4, 0, 4, 0);
    m_layout->setSpacing(0);
    m_layout->addStretch(1);
}

PDFFireRibbonGroup* PDFFireRibbonPage::addGroup(const QString& title)
{
    PDFFireRibbonGroup* group = new PDFFireRibbonGroup(title, this);

    QFrame* separator = new QFrame(this);
    separator->setObjectName("ribbonGroupSeparator");
    separator->setFrameShape(QFrame::NoFrame);
    separator->setFixedWidth(1);

    // The groups precede the stretch, which is the last item of the layout
    m_layout->insertWidget(m_layout->count() - 1, group);
    m_layout->insertWidget(m_layout->count() - 1, separator);

    m_groups.push_back(group);
    m_separators.push_back(separator);
    return group;
}

void PDFFireRibbonPage::removeEmptyGroups()
{
    for (size_t i = 0; i < m_groups.size(); ++i)
    {
        if (m_groups[i] && m_groups[i]->isEmpty())
        {
            delete m_groups[i];
            delete m_separators[i];
            m_groups[i] = nullptr;
            m_separators[i] = nullptr;
        }
    }
}

void PDFFireRibbonPage::updateMinimumWidth()
{
    int width = m_layout->contentsMargins().left() + m_layout->contentsMargins().right();
    for (size_t i = 0; i < m_groups.size(); ++i)
    {
        if (m_groups[i])
        {
            m_groups[i]->layout()->invalidate();
            width += m_groups[i]->sizeHint().width() + m_separators[i]->width();
        }
    }
    setMinimumWidth(width);
}

PDFFireRibbon::PDFFireRibbon(QWidget* parent) :
    QWidget(parent),
    m_fileButton(new QToolButton(this)),
    m_tabBar(new QTabBar(this)),
    m_tabRowLayout(new QHBoxLayout()),
    m_pages(new QStackedWidget(this)),
    m_cornerWidget(nullptr),
    m_isMinimized(false)
{
    setObjectName("ribbon");
    setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* tabRow = new QWidget(this);
    tabRow->setObjectName("ribbonTabRow");
    tabRow->setAttribute(Qt::WA_StyledBackground, true);
    tabRow->setLayout(m_tabRowLayout);
    m_tabRowLayout->setContentsMargins(6, 4, 8, 0);
    m_tabRowLayout->setSpacing(4);

    m_fileButton->setObjectName("ribbonFileButton");
    m_fileButton->setPopupMode(QToolButton::InstantPopup);
    m_fileButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_fileButton->setFocusPolicy(Qt::NoFocus);
    m_fileButton->hide();
    m_tabRowLayout->addWidget(m_fileButton, 0, Qt::AlignBottom);

    m_tabBar->setObjectName("ribbonTabBar");
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setFocusPolicy(Qt::NoFocus);
    m_tabBar->installEventFilter(this);
    m_tabRowLayout->addWidget(m_tabBar, 0, Qt::AlignBottom);
    m_tabRowLayout->addStretch(1);

    m_pages->setObjectName("ribbonPages");
    m_pages->setAttribute(Qt::WA_StyledBackground, true);
    m_pages->setFixedHeight(pdf::PDFWidgetUtils::scaleDPI_y(this, 108));

    layout->addWidget(tabRow);
    layout->addWidget(m_pages);

    connect(m_tabBar, &QTabBar::currentChanged, this, &PDFFireRibbon::onCurrentTabChanged);
    connect(m_tabBar, &QTabBar::tabBarClicked, this, [this](int) { if (m_isMinimized) { setMinimized(false); } });
}

void PDFFireRibbon::setFileMenu(const QString& text, QMenu* menu)
{
    m_fileButton->setText(text);
    m_fileButton->setMenu(menu);
    m_fileButton->setVisible(menu != nullptr);
}

PDFFireRibbonPage* PDFFireRibbon::addPage(const QString& title)
{
    // The page can be wider than the window, the rest is then reachable by scrolling
    QScrollArea* scrollArea = new QScrollArea(m_pages);
    scrollArea->setObjectName("ribbonPageScrollArea");
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->viewport()->setAutoFillBackground(false);

    PDFFireRibbonPage* page = new PDFFireRibbonPage(scrollArea);
    scrollArea->setWidget(page);
    scrollArea->viewport()->installEventFilter(this);

    m_pages->addWidget(scrollArea);
    m_tabBar->addTab(title);
    return page;
}

void PDFFireRibbon::setCornerWidget(QWidget* widget)
{
    if (m_cornerWidget)
    {
        m_tabRowLayout->removeWidget(m_cornerWidget);
    }

    m_cornerWidget = widget;

    if (m_cornerWidget)
    {
        m_cornerWidget->setParent(this);
        m_tabRowLayout->addWidget(m_cornerWidget, 0, Qt::AlignVCenter);
    }
}

void PDFFireRibbon::setCurrentPage(int index)
{
    m_tabBar->setCurrentIndex(index);
}

void PDFFireRibbon::setMinimized(bool minimized)
{
    m_isMinimized = minimized;
    m_pages->setVisible(!minimized);
}

bool PDFFireRibbon::eventFilter(QObject* watched, QEvent* event)
{
    // Double click on a tab minimizes the ribbon, or restores it
    if (watched == m_tabBar && event->type() == QEvent::MouseButtonDblClick)
    {
        setMinimized(!m_isMinimized);
        return true;
    }

    // A page wider than the window is scrolled by the mouse wheel
    if (event->type() == QEvent::Wheel)
    {
        if (QScrollArea* scrollArea = qobject_cast<QScrollArea*>(watched->parent()))
        {
            QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
            const QPoint delta = wheelEvent->angleDelta();
            const int step = qAbs(delta.x()) > qAbs(delta.y()) ? delta.x() : delta.y();
            QScrollBar* scrollBar = scrollArea->horizontalScrollBar();
            scrollBar->setValue(scrollBar->value() - step);
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void PDFFireRibbon::onCurrentTabChanged(int index)
{
    m_pages->setCurrentIndex(index);
}

}   // namespace pdfviewer
