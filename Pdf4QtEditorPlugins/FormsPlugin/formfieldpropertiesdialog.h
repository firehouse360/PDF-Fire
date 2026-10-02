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

#ifndef FORMFIELDPROPERTIESDIALOG_H
#define FORMFIELDPROPERTIESDIALOG_H

#include "pdffireformfields.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace pdfplugin
{

/// PDF Fire: the properties of a form field (name, tool tip, required, value,
/// list items, date format, colors ...)
class FormFieldPropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    /// \param settings Settings of the field
    /// \param usedNames Names of the other fields (a new name must differ)
    explicit FormFieldPropertiesDialog(const pdf::PDFFireFormFields::Settings& settings, const QStringList& usedNames, QWidget* parent);

    pdf::PDFFireFormFields::Settings getSettings() const;

    virtual void accept() override;

private:
    QPushButton* createColorButton(QColor* color);
    static void updateColorButton(QPushButton* button, const QColor& color);

    pdf::PDFFireFormFields::Settings m_settings;
    QStringList m_usedNames;

    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_toolTipEdit = nullptr;
    QCheckBox* m_requiredCheckBox = nullptr;
    QCheckBox* m_readOnlyCheckBox = nullptr;
    QDoubleSpinBox* m_fontSizeSpinBox = nullptr;
    QComboBox* m_alignmentComboBox = nullptr;
    QSpinBox* m_maxLengthSpinBox = nullptr;
    QLineEdit* m_valueEdit = nullptr;
    QPlainTextEdit* m_multilineValueEdit = nullptr;
    QComboBox* m_dateFormatComboBox = nullptr;
    QLineEdit* m_exportValueEdit = nullptr;
    QCheckBox* m_checkedCheckBox = nullptr;
    QPlainTextEdit* m_optionsEdit = nullptr;
    QCheckBox* m_editableCheckBox = nullptr;
    QCheckBox* m_borderCheckBox = nullptr;
    QCheckBox* m_backgroundCheckBox = nullptr;
    QDoubleSpinBox* m_borderWidthSpinBox = nullptr;
    QColor m_borderColor;
    QColor m_backgroundColor;
};

}   // namespace pdfplugin

#endif // FORMFIELDPROPERTIESDIALOG_H
