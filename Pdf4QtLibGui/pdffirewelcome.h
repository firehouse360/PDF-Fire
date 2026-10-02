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

#ifndef PDFFIREWELCOME_H
#define PDFFIREWELCOME_H

#include <QWidget>

#include <vector>

class QAction;
class QCheckBox;
class QPushButton;
class QVBoxLayout;

namespace pdfviewer
{

/// The page displayed in the window, when no document is opened - the logo,
/// a button opening a document, and the recently opened documents. The widget
/// covers its parent widget, and follows its size.
class PDFFireWelcomeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PDFFireWelcomeWidget(QAction* openAction, std::vector<QAction*> recentFileActions, QAction* clearRecentFilesAction, QWidget* parent);

    /// The recent documents are shown (on this page and in the File menu). A setting
    /// of the application, so the choice is remembered.
    static bool isShowingRecentDocuments();
    static void setShowingRecentDocuments(bool show);

    /// Rebuilds the list of the recent documents from the actions
    void refresh();

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::vector<QAction*> m_recentFileActions;
    QWidget* m_recentFilesWidget;
    QVBoxLayout* m_recentFilesLayout;
    QAction* m_clearRecentFilesAction;
    QCheckBox* m_showRecentCheckBox;
    QPushButton* m_clearRecentButton;
};

}   // namespace pdfviewer

#endif // PDFFIREWELCOME_H
