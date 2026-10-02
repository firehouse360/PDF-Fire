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

#ifndef PAGEMARKSPLUGIN_H
#define PAGEMARKSPLUGIN_H

#include "pdfplugin.h"

#include <QObject>

#include <functional>

namespace pdf
{
class PDFDocumentBuilder;
}

namespace pdfplugin
{

/// PDF Fire: Add Watermark, Add Header & Footer, Bates Numbering, Add Background
/// (a page of another PDF) and Remove - as Acrobat has them. The marks are written by PageMarks (see pagemarks.h).
class PageMarksPlugin : public pdf::PDFPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PDF4QT.PageMarksPlugin" FILE "PageMarksPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    PageMarksPlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;
    virtual QString getPluginMenuName() const override;
    virtual PluginMenuLocation getPluginMenuLocation() const override;

private:
    enum class Existing
    {
        Replace,
        Add,
        Cancel
    };

    void updateActions();
    void onAddWatermark();
    void onAddHeaderFooter();
    void onBatesNumbering();
    void onAddBackground();
    void onRemoveMarks();

    /// When the document has marks of the kind already, asks whether the new ones
    /// replace them (as Acrobat asks)
    Existing askAboutExisting(int kind, const QString& question);

    /// Changes the document in one undoable step
    void modify(const std::function<bool(pdf::PDFDocumentBuilder*, QString*)>& change);

    QWidget* getDialogParent() const;

    QAction* m_actionAddWatermark;
    QAction* m_actionAddHeaderFooter;
    QAction* m_actionBatesNumbering;
    QAction* m_actionAddBackground;
    QAction* m_actionRemoveMarks;
};

}   // namespace pdfplugin

#endif // PAGEMARKSPLUGIN_H
