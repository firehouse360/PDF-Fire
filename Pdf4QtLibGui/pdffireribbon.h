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

#ifndef PDFFIRERIBBON_H
#define PDFFIRERIBBON_H

#include <QWidget>

class QAction;
class QBoxLayout;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QMenu;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace pdfviewer
{

/// A group of the ribbon page - a titled block of buttons. Large buttons
/// take the full height of the group, small buttons are stacked in columns
/// of three rows.
class PDFFireRibbonGroup : public QWidget
{
    Q_OBJECT

public:
    explicit PDFFireRibbonGroup(const QString& title, QWidget* parent);

    /// Adds a large button (an icon with the text under it)
    QToolButton* addLargeAction(QAction* action);

    /// Adds a large button, which opens a menu
    QToolButton* addLargeMenu(const QIcon& icon, const QString& text, QMenu* menu);

    /// Adds a small button (an icon with the text beside it). Small buttons
    /// are stacked into columns.
    QToolButton* addSmallAction(QAction* action, bool showText = true);

    /// Adds a small button, which opens a menu
    QToolButton* addSmallMenu(const QIcon& icon, const QString& text, QMenu* menu);

    /// Adds an arbitrary widget, it takes the full height of the group
    void addWidget(QWidget* widget);

    /// Starts a new column of small buttons
    void breakColumn();

    /// Returns true, if nothing was added to the group
    bool isEmpty() const { return m_itemCount == 0; }

private:
    static constexpr int ROWS = 3;

    QToolButton* createSmallButton(bool showText);
    QToolButton* createLargeButton();
    void placeSmallButton(QToolButton* button);
    void placeFullHeightWidget(QWidget* widget);

    QGridLayout* m_gridLayout;
    int m_column = 0;
    int m_row = 0;
    int m_itemCount = 0;
};

/// A page of the ribbon, it is a row of groups.
class PDFFireRibbonPage : public QWidget
{
    Q_OBJECT

public:
    explicit PDFFireRibbonPage(QWidget* parent);

    PDFFireRibbonGroup* addGroup(const QString& title);

    /// Removes the groups, to which nothing was added
    void removeEmptyGroups();

    /// The page is never squeezed under the size its buttons need (their texts
    /// would be elided), call it when the texts of the buttons are final.
    void updateMinimumWidth();

private:
    QHBoxLayout* m_layout;
    std::vector<PDFFireRibbonGroup*> m_groups;
    std::vector<QWidget*> m_separators;
};

/// The ribbon - tabs with pages of grouped buttons, used in the place of the
/// menu bar and of the toolbars.
class PDFFireRibbon : public QWidget
{
    Q_OBJECT

public:
    explicit PDFFireRibbon(QWidget* parent);

    /// Sets the menu of the "File" button, which precedes the tabs
    void setFileMenu(const QString& text, QMenu* menu);

    /// Adds a new page
    PDFFireRibbonPage* addPage(const QString& title);

    /// Sets the widget displayed at the end of the tab row
    void setCornerWidget(QWidget* widget);

    /// Selects the page with a given index
    void setCurrentPage(int index);

    /// Pages are hidden, only the tab row is displayed (the ribbon is minimized)
    bool isMinimized() const { return m_isMinimized; }
    void setMinimized(bool minimized);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onCurrentTabChanged(int index);

    QToolButton* m_fileButton;
    QTabBar* m_tabBar;
    QHBoxLayout* m_tabRowLayout;
    QStackedWidget* m_pages;
    QWidget* m_cornerWidget;
    bool m_isMinimized;
};

}   // namespace pdfviewer

#endif // PDFFIRERIBBON_H
