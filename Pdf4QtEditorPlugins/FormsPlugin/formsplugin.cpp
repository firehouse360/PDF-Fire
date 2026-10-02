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

#include "formsplugin.h"
#include "pdffirepermissions.h"
#include "formfieldpropertiesdialog.h"

#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdffireformdesigntool.h"
#include "pdfwidgettool.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColorDialog>
#include <QMenu>
#include <QMainWindow>
#include <QMessageBox>
#include <QStatusBar>

namespace pdfplugin
{

using Type = pdf::PDFFireFormFields::Type;

FormsPlugin::FormsPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actionEditFields(nullptr),
    m_actionFillForm(nullptr),
    m_actionProperties(nullptr),
    m_actionDuplicate(nullptr),
    m_actionCopyToAllPages(nullptr),
    m_actionDelete(nullptr),
    m_actionDetectFields(nullptr),
    m_actionTabOrder(nullptr),
    m_modeGroup(nullptr),
    m_tool(nullptr)
{

}

void FormsPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    auto createAction = [this](const char* objectName, const QString& text, const char* icon, const QString& toolTip)
    {
        QAction* action = new QAction(QIcon(QString(":/pdfplugins/formsplugin/%1").arg(QString::fromLatin1(icon))), text, this);
        action->setObjectName(QString::fromLatin1(objectName));
        action->setToolTip(toolTip);
        m_actions.push_back(action);
        return action;
    };

    m_actionFillForm = createAction("formsplugin_FillForm", tr("Fill Form"), "form-fill.svg",
                                    tr("Fill in the form: click a field and type, tick the boxes, pick from the lists. Click a signature line to sign it with your certificate."));

    m_modeGroup = new QActionGroup(this);
    m_modeGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

    m_actionEditFields = createAction("formsplugin_EditFields", tr("Edit Fields"), "form-edit.svg",
                                      tr("Switch the form editing on or off. While it is on, the fields are shown with their names - select a field to move, resize, copy or delete it, or double-click it to change its properties. Switch it off to fill in the form."));
    m_actionEditFields->setCheckable(true);
    m_actions.push_back(nullptr);

    const std::tuple<const char*, Type, QString, const char*, QString> placeActions[] = {
        { "formsplugin_AddText", Type::Text, tr("Text Field"), "form-text.svg", tr("A box to type one line of text in") },
        { "formsplugin_AddTextBox", Type::MultilineText, tr("Text Box"), "form-textbox.svg", tr("A larger box to type several lines of text in") },
        { "formsplugin_AddDate", Type::Date, tr("Date"), "form-date.svg", tr("A box for a date") },
        { "formsplugin_AddCheckBox", Type::CheckBox, tr("Check Box"), "form-checkbox.svg", tr("A box to tick") },
        { "formsplugin_AddRadio", Type::RadioButton, tr("Radio Button"), "form-radio.svg", tr("One choice of several - place more buttons with the same group name (a new button joins the group of the selected button)") },
        { "formsplugin_AddDropdown", Type::ComboBox, tr("Drop-down"), "form-dropdown.svg", tr("A list to pick one item from") },
        { "formsplugin_AddList", Type::ListBox, tr("List Box"), "form-list.svg", tr("A list showing its items, to pick one") },
        { "formsplugin_AddSignature", Type::Signature, tr("Signature"), "form-signature.svg", tr("A signature line - to sign by hand on a printed form, or digitally in a reader, which supports it") },
    };

    for (const auto& [objectName, type, text, icon, toolTip] : placeActions)
    {
        QAction* action = createAction(objectName, text, icon, tr("%1. Click on the page to place it, or drag to give it a size.").arg(toolTip));
        action->setCheckable(true);
        m_modeGroup->addAction(action);
        m_placeActions[action] = type;
    }
    m_actions.push_back(nullptr);

    m_actionProperties = createAction("formsplugin_Properties", tr("Properties"), "form-properties.svg", tr("Change the name, the value, the items and the look of the selected field (or double-click the field)"));
    m_actionDuplicate = createAction("formsplugin_Duplicate", tr("Duplicate"), "form-duplicate.svg", tr("Make a copy of the selected field (Ctrl+D, or Ctrl+C and Ctrl+V - the copy is put where the mouse is)"));
    m_actionCopyToAllPages = createAction("formsplugin_CopyToAllPages", tr("Copy to All Pages"), "form-copy-pages.svg", tr("Put a copy of the selected field to the same place on every other page (for example, initials on every page)"));
    m_actionDelete = createAction("formsplugin_Delete", tr("Delete Field"), "form-delete.svg", tr("Delete the selected field (Delete key)"));
    m_actions.push_back(nullptr);
    m_actionDetectFields = createAction("formsplugin_DetectFields", tr("Detect Fields"), "form-detect.svg",
                                        tr("Make a fillable form automatically: fields are placed on the blanks of the document (lines like \"Name: ______\", empty boxes). Check the result - delete what is not wanted."));
    m_actionTabOrder = createAction("formsplugin_TabOrder", tr("Tab Order"), "form-taborder.svg",
                                    tr("Order the fields of every page by rows (top to bottom, left to right), so the Tab key goes through them in the reading order"));

    m_tool = new pdf::PDFFireFormDesignTool(widget->getDrawWidgetProxy(), widget->getToolManager(), this);
    widget->getToolManager()->addTool(m_tool);

    connect(m_modeGroup, &QActionGroup::triggered, this, [this](QAction* action) { onModeActionToggled(action, action->isChecked()); });
    connect(m_actionEditFields, &QAction::triggered, this, [this](bool checked) { onModeActionToggled(m_actionEditFields, checked); });
    connect(m_tool, &pdf::PDFWidgetTool::toolActivityChanged, this, &FormsPlugin::onToolActivityChanged);
    connect(m_tool, &pdf::PDFFireFormDesignTool::selectionChanged, this, &FormsPlugin::updateActions);
    connect(m_tool, &pdf::PDFFireFormDesignTool::propertiesRequested, this, &FormsPlugin::onProperties);
    connect(m_tool, &pdf::PDFFireFormDesignTool::contextMenuRequested, this, &FormsPlugin::onContextMenu);
    connect(m_actionProperties, &QAction::triggered, this, &FormsPlugin::onProperties);
    connect(m_actionDuplicate, &QAction::triggered, m_tool, &pdf::PDFFireFormDesignTool::duplicateSelected);
    connect(m_actionCopyToAllPages, &QAction::triggered, m_tool, &pdf::PDFFireFormDesignTool::copySelectedToAllPages);
    connect(m_actionDelete, &QAction::triggered, m_tool, &pdf::PDFFireFormDesignTool::deleteSelected);
    connect(m_actionDetectFields, &QAction::triggered, this, &FormsPlugin::onDetectFields);
    connect(m_actionTabOrder, &QAction::triggered, this, &FormsPlugin::onTabOrder);
    connect(m_actionFillForm, &QAction::triggered, this, &FormsPlugin::onFillForm);

    updateActions();
}

void FormsPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);
    updateActions();
}

std::vector<QAction*> FormsPlugin::getActions() const
{
    return m_actions;
}

QString FormsPlugin::getPluginMenuName() const
{
    return tr("For&ms");
}

void FormsPlugin::updateActions()
{
    const bool hasDocument = m_document != nullptr;
    const bool hasSelection = hasDocument && m_tool && m_tool->isActive() && m_tool->getSelectedWidget().isValid();

    // PDF Fire: designing the form changes the document, filling it in is allowed
    // separately (a protected or certified document)
    const bool canDesign = hasDocument && pdf::PDFFirePermissions::canDesignForms(m_document);
    const bool canFill = hasDocument && pdf::PDFFirePermissions::canFillForms(m_document);
    const QString reason = hasDocument ? pdf::PDFFirePermissions::getRestrictionReason(m_document) : QString();
    for (QAction* action : m_modeGroup->actions())
    {
        action->setEnabled(canDesign);
        action->setStatusTip(hasDocument && !canDesign ? reason : QString());
    }
    m_actionEditFields->setEnabled(canDesign);
    m_actionFillForm->setEnabled(canFill);

    m_actionProperties->setEnabled(hasSelection);
    m_actionDuplicate->setEnabled(hasSelection);
    m_actionCopyToAllPages->setEnabled(hasSelection && m_document->getCatalog()->getPageCount() > 1);
    m_actionDelete->setEnabled(hasSelection);
    m_actionDetectFields->setEnabled(canDesign);
    m_actionTabOrder->setEnabled(canDesign);
}

void FormsPlugin::onModeActionToggled(QAction* action, bool checked)
{
    // Edit Fields switches the form editing on and off; it is checked all the time
    // the form is edited. The buttons of the types choose, what a click places -
    // unchecking a type leaves the form editing on, only selecting the fields.
    if (action == m_actionEditFields)
    {
        if (checked)
        {
            if (!m_tool->isActive())
            {
                m_widget->getToolManager()->setActiveTool(m_tool);
            }
            m_tool->setPlacedType(std::nullopt);
        }
        else if (m_tool->isActive())
        {
            m_widget->getToolManager()->setActiveTool(nullptr);
        }
        updateActions();
        return;
    }

    if (!checked)
    {
        m_tool->setPlacedType(std::nullopt);
        updateActions();
        return;
    }

    if (!m_tool->isActive())
    {
        m_widget->getToolManager()->setActiveTool(m_tool);
    }
    m_actionEditFields->setChecked(true);

    auto it = m_placeActions.find(action);
    m_tool->setPlacedType(it != m_placeActions.cend() ? std::make_optional(it->second) : std::nullopt);
    updateActions();
}

void FormsPlugin::onToolActivityChanged(bool active)
{
    if (!active)
    {
        // The tool was turned off (Esc, another tool) - no mode is chosen
        if (QAction* checkedAction = m_modeGroup->checkedAction())
        {
            checkedAction->setChecked(false);
        }
        m_actionEditFields->setChecked(false);
    }
    updateActions();
}

void FormsPlugin::onProperties()
{
    const pdf::PDFObjectReference widget = m_tool->getSelectedWidget();
    if (!m_document || !widget.isValid())
    {
        return;
    }

    const std::optional<pdf::PDFFireFormFields::Settings> settings = pdf::PDFFireFormFields::readField(&m_document->getStorage(), widget);
    if (!settings)
    {
        QMessageBox::information(m_widget, tr("Properties"), tr("The properties of this kind of field cannot be changed here."));
        return;
    }

    FormFieldPropertiesDialog dialog(*settings, pdf::PDFFireFormFields::getFieldNames(&m_document->getStorage()), m_widget);
    if (dialog.exec() == QDialog::Accepted)
    {
        const pdf::PDFFireFormFields::Settings newSettings = dialog.getSettings();
        m_tool->modify([&](pdf::PDFDocumentBuilder* builder) { pdf::PDFFireFormFields::updateField(builder, widget, newSettings); });
    }
}

void FormsPlugin::changeSelectedField(const std::function<void(pdf::PDFFireFormFields::Settings&)>& change)
{
    const pdf::PDFObjectReference widget = m_tool->getSelectedWidget();
    if (!m_document || !widget.isValid())
    {
        return;
    }

    std::optional<pdf::PDFFireFormFields::Settings> settings = pdf::PDFFireFormFields::readField(&m_document->getStorage(), widget);
    if (!settings)
    {
        return;
    }

    change(*settings);
    const pdf::PDFFireFormFields::Settings newSettings = *settings;
    m_tool->modify([&](pdf::PDFDocumentBuilder* builder) { pdf::PDFFireFormFields::updateField(builder, widget, newSettings); });
}

void FormsPlugin::onContextMenu(QPoint globalPosition)
{
    using Settings = pdf::PDFFireFormFields::Settings;
    using BorderStyle = pdf::PDFFireFormFields::BorderStyle;

    // PDF Fire: the look and the options of the selected field straight from its menu,
    // as in Acrobat - the full set is in Properties
    const pdf::PDFObjectReference widget = m_tool->getSelectedWidget();
    const std::optional<Settings> current = (m_document && widget.isValid()) ? pdf::PDFFireFormFields::readField(&m_document->getStorage(), widget) : std::nullopt;

    QMenu menu(m_widget);
    menu.setObjectName("formFieldContextMenu");
    menu.addAction(m_actionProperties);
    menu.addSeparator();

    if (current)
    {
        const Settings settings = *current;
        const bool isTextField = settings.type == Type::Text || settings.type == Type::MultilineText;
        const bool isText = isTextField || settings.type == Type::Date;
        const bool isChoice = settings.type == Type::ComboBox || settings.type == Type::ListBox;

        auto addCheckable = [](QMenu* targetMenu, const QString& text, bool checked)
        {
            QAction* action = targetMenu->addAction(text);
            action->setCheckable(true);
            action->setChecked(checked);
            return action;
        };

        // Border
        QMenu* borderMenu = menu.addMenu(tr("Border"));
        borderMenu->setObjectName("formFieldBorderMenu");
        QActionGroup* widthGroup = new QActionGroup(borderMenu);
        const std::pair<QString, qreal> widths[] = { { tr("None"), 0.0 }, { tr("Thin (1 pt)"), 1.0 }, { tr("Medium (2 pt)"), 2.0 }, { tr("Thick (3 pt)"), 3.0 } };
        for (const auto& [text, width] : widths)
        {
            const bool checked = (width == 0.0) ? !settings.borderColor.isValid() : (settings.borderColor.isValid() && qFuzzyCompare(settings.borderWidth, width));
            QAction* action = addCheckable(borderMenu, text, checked);
            widthGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, width]()
            {
                changeSelectedField([width](Settings& s)
                {
                    if (width == 0.0)
                    {
                        s.borderColor = QColor();
                    }
                    else
                    {
                        if (!s.borderColor.isValid())
                        {
                            s.borderColor = Qt::black;
                        }
                        s.borderWidth = width;
                    }
                });
            });
        }
        borderMenu->addSeparator();
        QActionGroup* styleGroup = new QActionGroup(borderMenu);
        const std::pair<QString, BorderStyle> styles[] = { { tr("Solid"), BorderStyle::Solid }, { tr("Dashed"), BorderStyle::Dashed }, { tr("Underline Only"), BorderStyle::Underline } };
        for (const auto& [text, style] : styles)
        {
            QAction* action = addCheckable(borderMenu, text, settings.borderStyle == style);
            action->setEnabled(settings.borderColor.isValid());
            styleGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, style]() { changeSelectedField([style](Settings& s) { s.borderStyle = style; }); });
        }
        borderMenu->addSeparator();
        connect(borderMenu->addAction(tr("Border Color...")), &QAction::triggered, this, [this, settings]()
        {
            const QColor color = QColorDialog::getColor(settings.borderColor.isValid() ? settings.borderColor : QColor(Qt::black), m_widget, tr("Border Color"));
            if (color.isValid())
            {
                changeSelectedField([color](Settings& s) { s.borderColor = color; });
            }
        });

        // Background
        QMenu* backgroundMenu = menu.addMenu(tr("Background"));
        QAction* noBackgroundAction = addCheckable(backgroundMenu, tr("None"), !settings.backgroundColor.isValid());
        connect(noBackgroundAction, &QAction::triggered, this, [this]() { changeSelectedField([](Settings& s) { s.backgroundColor = QColor(); }); });
        connect(backgroundMenu->addAction(tr("Fill Color...")), &QAction::triggered, this, [this, settings]()
        {
            const QColor color = QColorDialog::getColor(settings.backgroundColor.isValid() ? settings.backgroundColor : QColor(Qt::white), m_widget, tr("Background Color"));
            if (color.isValid())
            {
                changeSelectedField([color](Settings& s) { s.backgroundColor = color; });
            }
        });

        if (isText || isChoice)
        {
            // Font
            QMenu* fontMenu = menu.addMenu(tr("Font"));
            fontMenu->setObjectName("formFieldFontMenu");
            QActionGroup* fontGroup = new QActionGroup(fontMenu);
            for (const pdf::PDFFireFormFields::FontInfo& font : pdf::PDFFireFormFields::getFonts())
            {
                QAction* action = addCheckable(fontMenu, font.displayName, settings.fontName == font.resourceName);
                fontGroup->addAction(action);
                const QByteArray fontName = font.resourceName;
                connect(action, &QAction::triggered, this, [this, fontName]() { changeSelectedField([fontName](Settings& s) { s.fontName = fontName; }); });
            }

            // Font size
            QMenu* sizeMenu = menu.addMenu(tr("Font Size"));
            sizeMenu->setObjectName("formFieldFontSizeMenu");
            QActionGroup* sizeGroup = new QActionGroup(sizeMenu);
            for (const qreal size : { 0.0, 8.0, 9.0, 10.0, 11.0, 12.0, 14.0, 16.0, 18.0, 24.0 })
            {
                QAction* action = addCheckable(sizeMenu, size == 0.0 ? tr("Auto (fits the field)") : tr("%1 pt").arg(size), qFuzzyCompare(settings.fontSize + 1.0, size + 1.0));
                sizeGroup->addAction(action);
                connect(action, &QAction::triggered, this, [this, size]() { changeSelectedField([size](Settings& s) { s.fontSize = size; }); });
            }

            // Alignment
            QMenu* alignmentMenu = menu.addMenu(tr("Alignment"));
            QActionGroup* alignmentGroup = new QActionGroup(alignmentMenu);
            const QString alignments[] = { tr("Left"), tr("Center"), tr("Right") };
            for (int i = 0; i < 3; ++i)
            {
                QAction* action = addCheckable(alignmentMenu, alignments[i], settings.alignment == i);
                alignmentGroup->addAction(action);
                connect(action, &QAction::triggered, this, [this, i]() { changeSelectedField([i](Settings& s) { s.alignment = i; }); });
            }
        }

        if (settings.type != Type::Signature)
        {
            connect(menu.addAction(settings.type == Type::CheckBox || settings.type == Type::RadioButton ? tr("Check Color...") : tr("Text Color...")),
                    &QAction::triggered, this, [this, settings]()
            {
                const QColor color = QColorDialog::getColor(settings.textColor.isValid() ? settings.textColor : QColor(Qt::black), m_widget, tr("Text Color"));
                if (color.isValid())
                {
                    changeSelectedField([color](Settings& s) { s.textColor = color; });
                }
            });
        }

        if (isTextField)
        {
            menu.addSeparator();
            QAction* multilineAction = addCheckable(&menu, tr("Multi-line (Wrap Words)"), settings.type == Type::MultilineText);
            multilineAction->setObjectName("formFieldMultilineAction");
            multilineAction->setToolTip(tr("A paragraph box: the words wrap to the next line"));
            connect(multilineAction, &QAction::triggered, this, [this](bool checked)
            {
                changeSelectedField([checked](Settings& s)
                {
                    s.type = checked ? Type::MultilineText : Type::Text;
                    if (checked)
                    {
                        s.comb = false;
                    }
                    else
                    {
                        s.value.replace(QChar('\n'), QChar(' '));
                    }
                });
            });
        }

        if (isText)
        {
            QAction* scrollAction = addCheckable(&menu, tr("Scroll Long Text"), !settings.doNotScroll);
            connect(scrollAction, &QAction::triggered, this, [this](bool checked) { changeSelectedField([checked](Settings& s) { s.doNotScroll = !checked; }); });
            QAction* spellAction = addCheckable(&menu, tr("Check Spelling"), !settings.doNotSpellCheck);
            connect(spellAction, &QAction::triggered, this, [this](bool checked) { changeSelectedField([checked](Settings& s) { s.doNotSpellCheck = !checked; }); });
        }

        menu.addSeparator();
        if (settings.type != Type::Signature)
        {
            QAction* requiredAction = addCheckable(&menu, tr("Required"), settings.required);
            connect(requiredAction, &QAction::triggered, this, [this](bool checked) { changeSelectedField([checked](Settings& s) { s.required = checked; }); });
        }
        QAction* readOnlyAction = addCheckable(&menu, tr("Read Only"), settings.readOnly);
        connect(readOnlyAction, &QAction::triggered, this, [this](bool checked) { changeSelectedField([checked](Settings& s) { s.readOnly = checked; }); });
        menu.addSeparator();
    }

    menu.addAction(m_actionDuplicate);
    menu.addAction(m_actionCopyToAllPages);
    menu.addSeparator();
    menu.addAction(m_actionDelete);
    menu.exec(globalPosition);
}

void FormsPlugin::onDetectFields()
{
    if (!m_document)
    {
        return;
    }

    pdf::PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
    const pdf::PDFCMSPointer cms = proxy->getCMSManager()->getCurrentCMS();
    const pdf::PDFCatalog* catalog = m_document->getCatalog();

    struct PageFields
    {
        pdf::PDFObjectReference page;
        std::vector<pdf::PDFFireFormFields::DetectedField> fields;
    };

    std::vector<PageFields> pages;
    size_t fieldCount = 0;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (size_t i = 0; i < catalog->getPageCount(); ++i)
    {
        const pdf::PDFPage* page = catalog->getPage(i);

        // A rotated page is read upright (as it is shown), and the places found are
        // turned back into the coordinates of the page
        const QRectF mediaBox = page->getMediaBox();
        QTransform toUpright;
        switch (page->getPageRotation())
        {
            case pdf::PageRotation::Rotate90:
                toUpright = QTransform(0, -1, 1, 0, -mediaBox.top(), mediaBox.right());
                break;
            case pdf::PageRotation::Rotate180:
                toUpright = QTransform(-1, 0, 0, -1, mediaBox.right(), mediaBox.bottom());
                break;
            case pdf::PageRotation::Rotate270:
                toUpright = QTransform(0, 1, -1, 0, mediaBox.bottom(), -mediaBox.left());
                break;
            default:
                break;
        }
        const QTransform fromUpright = toUpright.inverted();

        pdf::PDFFireFormFieldScanner scanner(pdf::PDFRenderer::IgnoreOptionalContent, page, m_document, proxy->getFontCache(), cms.data(),
                                             proxy->getOptionalContentActivity(), toUpright, proxy->getMeshQualitySettings());
        const pdf::PDFFireFormFieldScanner::Result result = scanner.scan();

        std::vector<QRectF> existingWidgets;
        for (pdf::PDFObjectReference widget : pdf::PDFFireFormFields::getWidgets(&m_document->getStorage(), page->getPageReference()))
        {
            existingWidgets.push_back(toUpright.mapRect(pdf::PDFFireFormFields::getWidgetRect(&m_document->getStorage(), widget)));
        }

        PageFields pageFields;
        pageFields.page = page->getPageReference();
        pageFields.fields = pdf::PDFFireFormFields::detectFields(result.runs, result.horizontalLines, result.squares, existingWidgets);
        for (pdf::PDFFireFormFields::DetectedField& field : pageFields.fields)
        {
            field.rect = fromUpright.mapRect(field.rect);
        }
        fieldCount += pageFields.fields.size();
        if (!pageFields.fields.empty())
        {
            pages.push_back(std::move(pageFields));
        }
    }
    QApplication::restoreOverrideCursor();

    if (fieldCount == 0)
    {
        QMessageBox::information(m_widget, tr("Detect Fields"),
                                 tr("No blanks were found in the document (lines like \"Name: ______\", drawn lines or empty boxes).\n\n"
                                    "Place the fields by hand: choose a type of field in the Forms tab and click on the page."));
        return;
    }

    if (QMessageBox::question(m_widget, tr("Detect Fields"),
                              (pages.size() == 1 ? tr("%1 blanks were found on 1 page. Make them fillable fields?").arg(fieldCount)
                                                  : tr("%1 blanks were found on %2 pages. Make them fillable fields?").arg(fieldCount).arg(pages.size())) +
                              QStringLiteral("\n\n") + tr("Check the result afterwards: wrong fields can be deleted (Delete key), and all of it can be undone.")) != QMessageBox::Yes)
    {
        return;
    }

    if (!m_tool->isActive())
    {
        m_actionEditFields->setChecked(true);
        onModeActionToggled(m_actionEditFields, true);
    }

    m_tool->modify([&](pdf::PDFDocumentBuilder* builder)
    {
        for (const PageFields& pageFields : pages)
        {
            for (const pdf::PDFFireFormFields::DetectedField& field : pageFields.fields)
            {
                pdf::PDFFireFormFields::Settings settings = pdf::PDFFireFormFields::getDefaultSettings(field.type);

                // The name comes from the label; it is numbered only when it is used
                const QString base = pdf::PDFFireFormFields::createNameFromLabel(field.label);
                const QStringList names = pdf::PDFFireFormFields::getFieldNames(builder->getStorage());
                settings.name = names.contains(base, Qt::CaseInsensitive) ? pdf::PDFFireFormFields::createUniqueName(builder->getStorage(), base) : base;
                settings.toolTip = field.label;

                // Fields found on lines have no border - the line of the document is the border
                if (field.type != Type::CheckBox)
                {
                    settings.borderColor = QColor();
                }
                pdf::PDFFireFormFields::createField(builder, pageFields.page, field.rect, settings);
            }
            pdf::PDFFireFormFields::sortTabOrderByRows(builder, pageFields.page);
        }
    });
}

void FormsPlugin::onFillForm()
{
    if (!m_document)
    {
        return;
    }

    // The editing of the fields ends - a click on a field fills it in
    if (m_tool->isActive())
    {
        m_widget->getToolManager()->setActiveTool(nullptr);
    }

    bool hasFields = false;
    for (size_t i = 0; i < m_document->getCatalog()->getPageCount() && !hasFields; ++i)
    {
        hasFields = !pdf::PDFFireFormFields::getWidgets(&m_document->getStorage(), m_document->getCatalog()->getPage(i)->getPageReference()).empty();
    }

    if (!hasFields)
    {
        QMessageBox::information(m_widget, tr("Fill Form"),
                                 tr("This document has no form fields to fill in.\n\n"
                                    "Make it fillable: Detect Fields finds the blanks by itself, or place the fields with the buttons of Add Field."));
        return;
    }

    if (QMainWindow* mainWindow = m_dataExchangeInterface ? m_dataExchangeInterface->getMainWindow() : nullptr)
    {
        mainWindow->statusBar()->showMessage(tr("Fill in the form: click a field and type (Tab goes to the next one), tick the boxes. "
                                                "Click a signature line to sign it with your certificate. Save when done."), 20000);
    }
}

void FormsPlugin::onTabOrder()
{
    if (!m_document)
    {
        return;
    }

    const pdf::PDFCatalog* catalog = m_document->getCatalog();
    const bool wasActive = m_tool->isActive();
    if (!wasActive)
    {
        m_widget->getToolManager()->setActiveTool(m_tool);
    }

    m_tool->modify([catalog](pdf::PDFDocumentBuilder* builder)
    {
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            const pdf::PDFObjectReference page = catalog->getPage(i)->getPageReference();
            if (!pdf::PDFFireFormFields::getWidgets(builder->getStorage(), page).empty())
            {
                pdf::PDFFireFormFields::sortTabOrderByRows(builder, page);
            }
        }
    });

    if (!wasActive)
    {
        m_widget->getToolManager()->setActiveTool(nullptr);
    }

    QMessageBox::information(m_widget, tr("Tab Order"), tr("The Tab key now goes through the fields of every page by rows, from the top to the bottom and from the left to the right."));
}

}   // namespace pdfplugin
