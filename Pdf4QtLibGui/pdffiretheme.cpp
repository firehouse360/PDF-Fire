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

#include "pdffiretheme.h"
#include "pdfwidgetutils.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

#include "pdfdbgheap.h"

namespace pdfviewer
{

namespace
{

struct ThemeColors
{
    const char* chrome;         ///< Tab row, status bar - the frame of the window
    const char* surface;        ///< Ribbon pages, panels, menus
    const char* base;           ///< Lists, trees, edit boxes
    const char* alternateBase;
    const char* border;
    const char* text;
    const char* mutedText;
    const char* disabledText;
    const char* hover;          ///< Background of a hovered button
    const char* pressed;        ///< Background of a pressed / checked button
    const char* scrollHandle;
    const char* scrollHandleHover;
    const char* documentArea;   ///< Area around the pages, see PDFDrawWidgetProxy::drawPages
};

constexpr const char* ACCENT = "#f2561d";
constexpr const char* ACCENT_HOVER = "#ff6a33";
constexpr const char* ACCENT_PRESSED = "#d8460f";

constexpr ThemeColors DARK_COLORS =
{
    "#1c1d20",
    "#282a2e",
    "#1f2023",
    "#25272b",
    "#3a3c42",
    "#e6e7ea",
    "#9a9da4",
    "#64676e",
    "rgba(255, 255, 255, 0.08)",
    "rgba(242, 86, 29, 0.28)",
    "#4a4d54",
    "#62656d",
    "#36383d",
};

constexpr ThemeColors LIGHT_COLORS =
{
    "#e9ebef",
    "#fbfbfc",
    "#ffffff",
    "#f5f6f8",
    "#d5d8de",
    "#1f2328",
    "#636a74",
    "#a3a8b0",
    "rgba(0, 0, 0, 0.06)",
    "rgba(242, 86, 29, 0.18)",
    "#c3c7ce",
    "#a5aab3",
    "#d6d9df",
};

QString createStyleSheet(const ThemeColors& c)
{
    QString styleSheet = QStringLiteral(R"(
/* ---- Ribbon ---------------------------------------------------------------- */

QWidget#ribbonTabRow {
    background: @chrome;
}

QStackedWidget#ribbonPages {
    background: @surface;
    border-top: 1px solid @border;
    border-bottom: 1px solid @border;
}

QScrollArea#ribbonPageScrollArea, QWidget#ribbonPage, QWidget#ribbonGroup {
    background: transparent;
}

QTabBar#ribbonTabBar::tab {
    background: transparent;
    color: @mutedText;
    padding: 6px 14px 7px 14px;
    margin: 0px 1px 0px 1px;
    border: none;
    border-bottom: 2px solid transparent;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}

QTabBar#ribbonTabBar::tab:hover {
    color: @text;
    background: @hover;
}

QTabBar#ribbonTabBar::tab:selected {
    color: @text;
    background: @surface;
    border-bottom: 2px solid @accent;
}

QToolButton#ribbonFileButton {
    background: @accent;
    color: #ffffff;
    font-weight: 600;
    border: none;
    border-radius: 6px;
    padding: 5px 18px 6px 18px;
    margin: 0px 6px 3px 0px;
}

QToolButton#ribbonFileButton:hover {
    background: @accentHover;
}

QToolButton#ribbonFileButton:pressed, QToolButton#ribbonFileButton:open {
    background: @accentPressed;
}

QToolButton#ribbonFileButton::menu-indicator {
    image: none;
    width: 0px;
}

QToolButton#ribbonLargeButton, QToolButton#ribbonSmallButton {
    background: transparent;
    color: @text;
    border: 1px solid transparent;
    border-radius: 6px;
}

QToolButton#ribbonLargeButton {
    padding: 4px 6px 2px 6px;
}

QToolButton#ribbonSmallButton {
    padding: 2px 6px 2px 4px;
}

QToolButton#ribbonLargeButton:hover, QToolButton#ribbonSmallButton:hover {
    background: @hover;
}

QToolButton#ribbonLargeButton:pressed, QToolButton#ribbonSmallButton:pressed,
QToolButton#ribbonLargeButton:checked, QToolButton#ribbonSmallButton:checked,
QToolButton#ribbonLargeButton:open, QToolButton#ribbonSmallButton:open {
    background: @pressed;
    border: 1px solid @accent;
}

QToolButton#ribbonLargeButton:disabled, QToolButton#ribbonSmallButton:disabled {
    color: @disabledText;
}

QToolButton#ribbonLargeButton::menu-indicator {
    subcontrol-origin: padding;
    subcontrol-position: bottom right;
    width: 8px;
    height: 8px;
}

QToolButton#ribbonSmallButton::menu-indicator {
    subcontrol-origin: padding;
    subcontrol-position: center right;
    width: 8px;
    height: 8px;
}

QLabel#ribbonGroupTitle {
    color: @mutedText;
    font-size: 8pt;
    padding: 0px 0px 1px 0px;
}

QFrame#ribbonGroupSeparator {
    background: @border;
    margin: 10px 0px 10px 0px;
}

QLineEdit#ribbonSearchBox {
    background: @base;
    color: @text;
    border: 1px solid @border;
    border-radius: 12px;
    padding: 3px 12px 3px 12px;
    margin: 0px 0px 4px 0px;
    selection-background-color: @accent;
}

QLineEdit#ribbonSearchBox:focus {
    border: 1px solid @accent;
}

/* ---- Welcome page ---------------------------------------------------------- */

QWidget#welcomeWidget {
    background: @documentArea;
}

QWidget#welcomeRecentFiles {
    background: transparent;
}

QLabel#welcomeTitle {
    color: @text;
    font-size: 24pt;
    font-weight: 700;
}

QLabel#welcomeSubtitle {
    color: @mutedText;
    font-size: 11pt;
}

QLabel#welcomeRecentHeader {
    color: @mutedText;
    font-size: 9pt;
    font-weight: 600;
    padding: 0px 0px 4px 10px;
}

QPushButton#welcomeOpenButton {
    background: @accent;
    color: #ffffff;
    font-size: 11pt;
    font-weight: 600;
    border: none;
    border-radius: 8px;
    padding: 9px 28px 10px 28px;
}

QPushButton#welcomeOpenButton:hover {
    background: @accentHover;
}

QPushButton#welcomeOpenButton:pressed {
    background: @accentPressed;
}

QCheckBox#welcomeShowRecentCheckBox {
    color: @mutedText;
}

QPushButton#welcomeClearRecentButton {
    background: transparent;
    color: @mutedText;
    border: none;
    text-decoration: underline;
    padding: 2px 4px 2px 4px;
}

QPushButton#welcomeClearRecentButton:hover {
    color: @text;
}

QPushButton#welcomeRecentButton {
    background: transparent;
    color: @text;
    border: none;
    border-radius: 6px;
    padding: 6px 10px 6px 10px;
    text-align: left;
}

QPushButton#welcomeRecentButton:hover {
    background: @hover;
}

QPushButton#welcomeRecentButton:pressed {
    background: @pressed;
}

/* ---- Window ---------------------------------------------------------------- */

QMainWindow::separator {
    background: @border;
    width: 1px;
    height: 1px;
}

QStatusBar {
    background: @chrome;
    color: @mutedText;
    border-top: 1px solid @border;
}

QStatusBar::item {
    border: none;
}

QStatusBar QLabel {
    color: @mutedText;
}

QStatusBar QToolButton {
    background: transparent;
    border: none;
    border-radius: 4px;
    padding: 3px;
}

QStatusBar QToolButton:hover {
    background: @hover;
}

QStatusBar QToolButton:pressed, QStatusBar QToolButton:checked {
    background: @pressed;
}

QStatusBar QSpinBox, QStatusBar QDoubleSpinBox {
    background: @base;
    color: @text;
    border: 1px solid @border;
    border-radius: 4px;
    padding: 1px 4px 1px 4px;
    selection-background-color: @accent;
}

QStatusBar QSpinBox:focus, QStatusBar QDoubleSpinBox:focus {
    border: 1px solid @accent;
}

QDockWidget {
    color: @text;
    titlebar-close-icon: none;
    titlebar-normal-icon: none;
}

QDockWidget::title {
    background: @surface;
    padding: 6px 8px 6px 10px;
    border-bottom: 1px solid @border;
}

/* ---- Side panel ------------------------------------------------------------ */

pdfviewer--PDFSidebarWidget {
    background: @surface;
}

pdfviewer--PDFSidebarWidget QToolButton {
    background: transparent;
    border: none;
    border-left: 2px solid transparent;
    border-radius: 0px;
    padding: 7px 4px 7px 2px;
    color: @mutedText;
}

pdfviewer--PDFSidebarWidget QToolButton:hover {
    background: @hover;
}

pdfviewer--PDFSidebarWidget QToolButton:checked {
    background: @pressed;
    border-left: 2px solid @accent;
    color: @text;
}

pdfviewer--PDFSidebarWidget QTreeView, pdfviewer--PDFSidebarWidget QListView,
pdfviewer--PDFSidebarWidget QTreeWidget, pdfviewer--PDFSidebarWidget QListWidget {
    background: @base;
    border: none;
    outline: none;
}

QTreeView::item, QListView::item {
    padding: 3px 2px 3px 2px;
    border-radius: 4px;
}

QTreeView::item:hover, QListView::item:hover {
    background: @hover;
}

QTreeView::item:selected, QListView::item:selected {
    background: @pressed;
    color: @text;
}

/* ---- Menus, tooltips, scroll bars ------------------------------------------ */

QMenu {
    background: @surface;
    color: @text;
    border: 1px solid @border;
    border-radius: 8px;
    padding: 5px;
}

QMenu::item {
    padding: 6px 28px 6px 12px;
    border-radius: 5px;
    margin: 1px 0px 1px 0px;
}

QMenu::item:selected {
    background: @pressed;
}

QMenu::item:disabled {
    color: @disabledText;
}

QMenu::icon {
    padding-left: 8px;
}

QMenu::separator {
    height: 1px;
    background: @border;
    margin: 5px 8px 5px 8px;
}

QToolTip {
    background: @surface;
    color: @text;
    border: 1px solid @border;
    padding: 4px 6px 4px 6px;
}

QScrollBar:vertical {
    background: transparent;
    width: 12px;
    margin: 0px;
}

QScrollBar:horizontal {
    background: transparent;
    height: 12px;
    margin: 0px;
}

QScrollBar::handle:vertical {
    background: @scrollHandle;
    min-height: 32px;
    border-radius: 4px;
    margin: 2px 2px 2px 2px;
}

QScrollBar::handle:horizontal {
    background: @scrollHandle;
    min-width: 32px;
    border-radius: 4px;
    margin: 2px 2px 2px 2px;
}

QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
    background: @scrollHandleHover;
}

QScrollBar::add-line, QScrollBar::sub-line {
    width: 0px;
    height: 0px;
}

QScrollBar::add-page, QScrollBar::sub-page {
    background: transparent;
}
)");

    styleSheet.replace("@accentHover", ACCENT_HOVER);
    styleSheet.replace("@accentPressed", ACCENT_PRESSED);
    styleSheet.replace("@accent", ACCENT);
    styleSheet.replace("@alternateBase", c.alternateBase);
    styleSheet.replace("@scrollHandleHover", c.scrollHandleHover);
    styleSheet.replace("@scrollHandle", c.scrollHandle);
    styleSheet.replace("@documentArea", c.documentArea);
    styleSheet.replace("@disabledText", c.disabledText);
    styleSheet.replace("@mutedText", c.mutedText);
    styleSheet.replace("@chrome", c.chrome);
    styleSheet.replace("@surface", c.surface);
    styleSheet.replace("@base", c.base);
    styleSheet.replace("@border", c.border);
    styleSheet.replace("@text", c.text);
    styleSheet.replace("@hover", c.hover);
    styleSheet.replace("@pressed", c.pressed);

    return styleSheet;
}

QPalette createPalette(const ThemeColors& c, bool isDark)
{
    const QColor window(c.surface);
    const QColor base(c.base);
    const QColor text(c.text);
    const QColor disabledText(c.disabledText);
    const QColor border(c.border);
    const QColor accent(ACCENT);

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, QColor(c.alternateBase));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, isDark ? window.lighter(118) : QColor(c.base));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, window);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, QColor(c.mutedText));
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Accent, accent);
    palette.setColor(QPalette::Link, isDark ? QColor("#ff8a5c") : QColor("#c8410f"));
    palette.setColor(QPalette::LinkVisited, isDark ? QColor("#c9a090") : QColor("#8a4a30"));

    // Shades used by the Fusion style for the frames and bevels
    palette.setColor(QPalette::Light, isDark ? window.lighter(150) : QColor(Qt::white));
    palette.setColor(QPalette::Midlight, isDark ? window.lighter(125) : window.lighter(102));
    palette.setColor(QPalette::Mid, border);
    palette.setColor(QPalette::Dark, isDark ? window.darker(160) : border.darker(115));
    palette.setColor(QPalette::Shadow, isDark ? QColor(Qt::black) : border.darker(150));

    for (QPalette::ColorRole role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText })
    {
        palette.setColor(QPalette::Disabled, role, disabledText);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, border);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);

    return palette;
}

}   // namespace

void PDFFireTheme::apply()
{
    const bool isDark = pdf::PDFWidgetUtils::isDarkTheme();
    const ThemeColors& colors = isDark ? DARK_COLORS : LIGHT_COLORS;

    // The Fusion style draws the same on every desktop, and it follows the palette
    // and the style sheet completely - the native styles do not.
    if (QStyle* fusionStyle = QStyleFactory::create("Fusion"))
    {
        QApplication::setStyle(fusionStyle);
    }

    QApplication::setPalette(createPalette(colors, isDark));
    qApp->setStyleSheet(createStyleSheet(colors));
}

QColor PDFFireTheme::getAccentColor()
{
    return QColor(ACCENT);
}

}   // namespace pdfviewer
