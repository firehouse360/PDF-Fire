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

#ifndef FORMSPLUGIN_H
#define FORMSPLUGIN_H

#include "pdfplugin.h"
#include "pdffireformfields.h"

#include <QObject>

#include <map>

class QActionGroup;

namespace pdf
{
class PDFFireFormDesignTool;
}

namespace pdfplugin
{

/// PDF Fire: the form maker - places typeable fields, check boxes, lists and
/// signature fields onto a PDF, edits them, and finds the empty places of a
/// form ("Name: ______") by itself
class FormsPlugin : public pdf::PDFPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PDF4QT.FormsPlugin" FILE "FormsPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    FormsPlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;
    virtual QString getPluginMenuName() const override;

private:
    void updateActions();
    void onModeActionToggled(QAction* action, bool checked);
    void onToolActivityChanged(bool active);
    void onProperties();
    void onContextMenu(QPoint globalPosition);
    void onDetectFields();
    void onTabOrder();
    void onFillForm();

    QAction* m_actionEditFields;
    QAction* m_actionFillForm;
    QAction* m_actionProperties;
    QAction* m_actionDuplicate;
    QAction* m_actionCopyToAllPages;
    QAction* m_actionDelete;
    QAction* m_actionDetectFields;
    QAction* m_actionTabOrder;
    std::map<QAction*, pdf::PDFFireFormFields::Type> m_placeActions;
    std::vector<QAction*> m_actions;
    QActionGroup* m_modeGroup;
    pdf::PDFFireFormDesignTool* m_tool;
};

}   // namespace pdfplugin

#endif // FORMSPLUGIN_H
