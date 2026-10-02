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

#include "formfieldpropertiesdialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace pdfplugin
{

using Type = pdf::PDFFireFormFields::Type;

FormFieldPropertiesDialog::FormFieldPropertiesDialog(const pdf::PDFFireFormFields::Settings& settings, const QStringList& usedNames, QWidget* parent) :
    QDialog(parent),
    m_settings(settings),
    m_usedNames(usedNames),
    m_borderColor(settings.borderColor.isValid() ? settings.borderColor : QColor(Qt::black)),
    m_backgroundColor(settings.backgroundColor.isValid() ? settings.backgroundColor : QColor(Qt::white)),
    m_textColor(settings.textColor.isValid() ? settings.textColor : QColor(Qt::black))
{
    setWindowTitle(tr("%1 Properties").arg(pdf::PDFFireFormFields::getTypeName(settings.type)));

    const bool isTextField = settings.type == Type::Text || settings.type == Type::MultilineText;
    const bool isText = isTextField || settings.type == Type::Date;
    const bool isButton = settings.type == Type::CheckBox || settings.type == Type::RadioButton;
    const bool isChoice = settings.type == Type::ComboBox || settings.type == Type::ListBox;

    // PDF Fire: the pages of Acrobat's field properties - General, Appearance, Options
    QVBoxLayout* layout = new QVBoxLayout(this);
    QTabWidget* tabWidget = new QTabWidget(this);
    tabWidget->setObjectName("fieldPropertiesTabs");
    layout->addWidget(tabWidget);

    // ---- General ----
    QWidget* generalPage = new QWidget(tabWidget);
    QFormLayout* generalLayout = new QFormLayout(generalPage);
    m_nameEdit = new QLineEdit(settings.name, generalPage);
    m_nameEdit->setToolTip(settings.type == Type::RadioButton ? tr("Radio buttons with the same group name work together - only one of them can be chosen")
                                                              : tr("The name of the field, under which its value is saved and exported"));
    generalLayout->addRow(settings.type == Type::RadioButton ? tr("Group name:") : tr("Name:"), m_nameEdit);
    m_toolTipEdit = new QLineEdit(settings.toolTip, generalPage);
    m_toolTipEdit->setPlaceholderText(tr("Shown when the mouse is over the field"));
    generalLayout->addRow(tr("Tool tip:"), m_toolTipEdit);
    m_requiredCheckBox = new QCheckBox(tr("Required - must be filled in"), generalPage);
    m_requiredCheckBox->setChecked(settings.required);
    if (settings.type != Type::Signature)
    {
        generalLayout->addRow(QString(), m_requiredCheckBox);
    }
    else
    {
        // A signature field cannot be required - the check box is not shown
        m_requiredCheckBox->setVisible(false);
    }
    m_readOnlyCheckBox = new QCheckBox(tr("Read only - cannot be changed"), generalPage);
    m_readOnlyCheckBox->setChecked(settings.readOnly);
    generalLayout->addRow(QString(), m_readOnlyCheckBox);
    tabWidget->addTab(generalPage, tr("General"));

    // ---- Appearance ----
    QWidget* lookPage = new QWidget(tabWidget);
    QFormLayout* lookLayout = new QFormLayout(lookPage);

    QWidget* borderWidget = new QWidget(lookPage);
    QHBoxLayout* borderLayout = new QHBoxLayout(borderWidget);
    borderLayout->setContentsMargins(0, 0, 0, 0);
    m_borderCheckBox = new QCheckBox(tr("Show"), borderWidget);
    m_borderCheckBox->setObjectName("borderCheckBox");
    m_borderCheckBox->setChecked(settings.borderColor.isValid());
    m_borderCheckBox->setToolTip(tr("A line around the field, also on the printed form. Off - only the screen shows where the field is."));
    QPushButton* borderColorButton = createColorButton(&m_borderColor);
    m_borderWidthSpinBox = new QDoubleSpinBox(borderWidget);
    m_borderWidthSpinBox->setRange(0.5, 5.0);
    m_borderWidthSpinBox->setSingleStep(0.5);
    m_borderWidthSpinBox->setSuffix(tr(" pt"));
    m_borderWidthSpinBox->setValue(qBound(0.5, settings.borderWidth, 5.0));
    m_borderStyleComboBox = new QComboBox(borderWidget);
    m_borderStyleComboBox->addItems({ tr("Solid"), tr("Dashed"), tr("Underline") });
    m_borderStyleComboBox->setCurrentIndex(int(settings.borderStyle));
    m_borderStyleComboBox->setToolTip(tr("Underline - only a line to write on, along the bottom of the field"));
    borderLayout->addWidget(m_borderCheckBox);
    borderLayout->addWidget(borderColorButton);
    borderLayout->addWidget(m_borderWidthSpinBox);
    borderLayout->addWidget(m_borderStyleComboBox);
    borderLayout->addStretch(1);
    lookLayout->addRow(settings.type == Type::Signature ? tr("Box:") : tr("Border:"), borderWidget);
    auto updateBorderControls = [this, borderColorButton]()
    {
        const bool enabled = m_borderCheckBox->isChecked();
        borderColorButton->setEnabled(enabled);
        m_borderWidthSpinBox->setEnabled(enabled);
        m_borderStyleComboBox->setEnabled(enabled);
    };
    connect(m_borderCheckBox, &QCheckBox::toggled, this, updateBorderControls);
    updateBorderControls();

    QWidget* backgroundWidget = new QWidget(lookPage);
    QHBoxLayout* backgroundLayout = new QHBoxLayout(backgroundWidget);
    backgroundLayout->setContentsMargins(0, 0, 0, 0);
    m_backgroundCheckBox = new QCheckBox(tr("Fill"), backgroundWidget);
    m_backgroundCheckBox->setChecked(settings.backgroundColor.isValid());
    backgroundLayout->addWidget(m_backgroundCheckBox);
    backgroundLayout->addWidget(createColorButton(&m_backgroundColor));
    backgroundLayout->addStretch(1);
    lookLayout->addRow(tr("Background:"), backgroundWidget);

    if (isText || isChoice)
    {
        m_fontComboBox = new QComboBox(lookPage);
        m_fontComboBox->setObjectName("fontComboBox");
        for (const pdf::PDFFireFormFields::FontInfo& font : pdf::PDFFireFormFields::getFonts())
        {
            m_fontComboBox->addItem(font.displayName, font.resourceName);
        }
        m_fontComboBox->setCurrentIndex(qMax(0, m_fontComboBox->findData(settings.fontName)));
        m_fontComboBox->setToolTip(tr("The standard fonts - every PDF reader has them, so the form looks the same everywhere"));
        lookLayout->addRow(tr("Font:"), m_fontComboBox);

        m_fontSizeSpinBox = new QDoubleSpinBox(lookPage);
        m_fontSizeSpinBox->setObjectName("fontSizeSpinBox");
        m_fontSizeSpinBox->setRange(0.0, 72.0);
        m_fontSizeSpinBox->setDecimals(1);
        m_fontSizeSpinBox->setSpecialValueText(tr("Auto"));
        m_fontSizeSpinBox->setSuffix(tr(" pt"));
        m_fontSizeSpinBox->setValue(settings.fontSize);
        m_fontSizeSpinBox->setToolTip(tr("Auto - the text fits the field"));
        lookLayout->addRow(tr("Font size:"), m_fontSizeSpinBox);
    }

    if (settings.type != Type::Signature)
    {
        lookLayout->addRow(isButton ? tr("Check color:") : tr("Text color:"), createColorButton(&m_textColor));
    }
    tabWidget->addTab(lookPage, tr("Appearance"));

    // ---- Options ----
    if (settings.type != Type::Signature)
    {
        QWidget* optionsPage = new QWidget(tabWidget);
        QFormLayout* valueLayout = new QFormLayout(optionsPage);

        if (isText || isChoice)
        {
            m_alignmentComboBox = new QComboBox(optionsPage);
            m_alignmentComboBox->addItems({ tr("Left"), tr("Center"), tr("Right") });
            m_alignmentComboBox->setCurrentIndex(qBound(0, settings.alignment, 2));
            valueLayout->addRow(tr("Alignment:"), m_alignmentComboBox);
        }

        if (settings.type == Type::Date)
        {
            m_dateFormatComboBox = new QComboBox(optionsPage);
            m_dateFormatComboBox->setEditable(true);
            m_dateFormatComboBox->addItems({ "mm/dd/yyyy", "m/d/yyyy", "mm/dd/yy", "dd/mm/yyyy", "yyyy-mm-dd", "dd.mm.yyyy", "mmm d, yyyy", "mmmm d, yyyy" });
            m_dateFormatComboBox->setCurrentText(settings.dateFormat);
            m_dateFormatComboBox->setToolTip(tr("How the date is written. Readers, which support it (like Adobe Acrobat), check the typed date and format it."));
            valueLayout->addRow(tr("Date format:"), m_dateFormatComboBox);
        }

        if (isTextField)
        {
            m_multilineValueEdit = new QPlainTextEdit(settings.value, optionsPage);
            m_multilineValueEdit->setMaximumHeight(70);
            m_multilineValueEdit->setPlaceholderText(tr("Empty"));
            valueLayout->addRow(tr("Default text:"), m_multilineValueEdit);
        }
        else if (isText || isChoice)
        {
            m_valueEdit = new QLineEdit(settings.value, optionsPage);
            m_valueEdit->setPlaceholderText(isChoice ? tr("One of the items (empty - nothing chosen)") : tr("Empty"));
            valueLayout->addRow(isChoice ? tr("Default item:") : tr("Default text:"), m_valueEdit);
        }

        if (isTextField)
        {
            m_multilineCheckBox = new QCheckBox(tr("Multi-line - the words wrap to the next line (a paragraph box)"), optionsPage);
            m_multilineCheckBox->setObjectName("multilineCheckBox");
            m_multilineCheckBox->setChecked(settings.type == Type::MultilineText);
            valueLayout->addRow(QString(), m_multilineCheckBox);
        }

        if (isText)
        {
            m_scrollCheckBox = new QCheckBox(tr("Scroll long text - allow more text than the field shows"), optionsPage);
            m_scrollCheckBox->setChecked(!settings.doNotScroll);
            valueLayout->addRow(QString(), m_scrollCheckBox);

            m_maxLengthSpinBox = new QSpinBox(optionsPage);
            m_maxLengthSpinBox->setRange(0, 10000);
            m_maxLengthSpinBox->setSpecialValueText(tr("No limit"));
            m_maxLengthSpinBox->setSuffix(tr(" characters"));
            m_maxLengthSpinBox->setValue(settings.maxLength);
            valueLayout->addRow(tr("Limit to:"), m_maxLengthSpinBox);
        }

        if (isTextField)
        {
            m_combCheckBox = new QCheckBox(tr("Comb - one character in each box (as many boxes as the limit)"), optionsPage);
            m_combCheckBox->setChecked(settings.comb);
            m_combCheckBox->setToolTip(tr("For things like a ZIP code or an account number written in boxes. Needs the limit, one line, no password."));
            valueLayout->addRow(QString(), m_combCheckBox);

            m_passwordCheckBox = new QCheckBox(tr("Password - the typed text is hidden"), optionsPage);
            m_passwordCheckBox->setChecked(settings.password);
            valueLayout->addRow(QString(), m_passwordCheckBox);

            auto updateTextOptions = [this]()
            {
                const bool isMultiline = m_multilineCheckBox->isChecked();
                const bool canComb = !isMultiline && !m_passwordCheckBox->isChecked() && m_maxLengthSpinBox->value() > 0;
                m_combCheckBox->setEnabled(canComb);
                m_passwordCheckBox->setEnabled(!m_combCheckBox->isChecked() || !canComb);
            };
            connect(m_multilineCheckBox, &QCheckBox::toggled, this, updateTextOptions);
            connect(m_passwordCheckBox, &QCheckBox::toggled, this, updateTextOptions);
            connect(m_combCheckBox, &QCheckBox::toggled, this, updateTextOptions);
            connect(m_maxLengthSpinBox, qOverload<int>(&QSpinBox::valueChanged), this, updateTextOptions);
            updateTextOptions();
        }

        if (isText || settings.type == Type::ComboBox)
        {
            m_spellCheckBox = new QCheckBox(tr("Check spelling"), optionsPage);
            m_spellCheckBox->setChecked(!settings.doNotSpellCheck);
            valueLayout->addRow(QString(), m_spellCheckBox);
        }

        if (isChoice)
        {
            m_optionsEdit = new QPlainTextEdit(settings.options.join(QChar('\n')), optionsPage);
            m_optionsEdit->setPlaceholderText(tr("One item on a line"));
            m_optionsEdit->setMaximumHeight(110);
            valueLayout->addRow(tr("Items:"), m_optionsEdit);

            if (settings.type == Type::ComboBox)
            {
                m_editableCheckBox = new QCheckBox(tr("Allow typing a text, which is not in the items"), optionsPage);
                m_editableCheckBox->setChecked(settings.editable);
                valueLayout->addRow(QString(), m_editableCheckBox);
            }
        }

        if (isButton)
        {
            m_exportValueEdit = new QLineEdit(settings.exportValue, optionsPage);
            m_exportValueEdit->setToolTip(settings.type == Type::RadioButton ? tr("The value saved, when this button of the group is chosen")
                                                                             : tr("The value saved, when the box is checked"));
            valueLayout->addRow(tr("Value when checked:"), m_exportValueEdit);
            m_checkedCheckBox = new QCheckBox(tr("Checked by default"), optionsPage);
            m_checkedCheckBox->setChecked(settings.checked);
            valueLayout->addRow(QString(), m_checkedCheckBox);
        }

        tabWidget->addTab(optionsPage, tr("Options"));
    }
    else
    {
        QLabel* hint = new QLabel(tr("A signature field is a signature line: it can be signed by hand on a printed form, "
                                     "or signed digitally in a PDF reader, which supports signing (like Adobe Acrobat)."), this);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &FormFieldPropertiesDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &FormFieldPropertiesDialog::reject);
    layout->addWidget(buttonBox);

    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

QPushButton* FormFieldPropertiesDialog::createColorButton(QColor* color)
{
    QPushButton* button = new QPushButton(this);
    button->setFixedWidth(44);
    button->setToolTip(tr("Choose the color"));
    updateColorButton(button, *color);
    connect(button, &QPushButton::clicked, this, [this, button, color]()
    {
        const QColor newColor = QColorDialog::getColor(*color, this, tr("Choose the Color"));
        if (newColor.isValid())
        {
            *color = newColor;
            updateColorButton(button, newColor);
        }
    });
    return button;
}

void FormFieldPropertiesDialog::updateColorButton(QPushButton* button, const QColor& color)
{
    button->setStyleSheet(QString("QPushButton { background-color: %1; border: 1px solid #808080; border-radius: 3px; min-height: 18px; }").arg(color.name()));
}

pdf::PDFFireFormFields::Settings FormFieldPropertiesDialog::getSettings() const
{
    pdf::PDFFireFormFields::Settings settings = m_settings;
    settings.name = m_nameEdit->text().trimmed();
    settings.toolTip = m_toolTipEdit->text().trimmed();
    settings.required = m_requiredCheckBox->isChecked() && m_settings.type != Type::Signature;
    settings.readOnly = m_readOnlyCheckBox->isChecked();

    if (m_fontSizeSpinBox)
    {
        settings.fontSize = m_fontSizeSpinBox->value();
    }
    if (m_alignmentComboBox)
    {
        settings.alignment = m_alignmentComboBox->currentIndex();
    }
    if (m_maxLengthSpinBox)
    {
        settings.maxLength = m_maxLengthSpinBox->value();
    }
    if (m_dateFormatComboBox)
    {
        settings.dateFormat = m_dateFormatComboBox->currentText().trimmed();
    }
    if (m_multilineCheckBox)
    {
        settings.type = m_multilineCheckBox->isChecked() ? Type::MultilineText : Type::Text;
    }
    if (m_multilineValueEdit)
    {
        settings.value = m_multilineValueEdit->toPlainText();
        if (settings.type != Type::MultilineText)
        {
            // One line - the line breaks become spaces
            settings.value.replace(QChar('\n'), QChar(' '));
        }
    }
    if (m_valueEdit)
    {
        settings.value = m_valueEdit->text();
    }
    if (m_optionsEdit)
    {
        settings.options.clear();
        settings.exportValues.clear();
        for (const QString& line : m_optionsEdit->toPlainText().split(QChar('\n')))
        {
            if (!line.trimmed().isEmpty())
            {
                settings.options << line.trimmed();
            }
        }

        // An item, which was in the list before, keeps its export value; a new item
        // is exported as its text
        if (!m_settings.exportValues.isEmpty())
        {
            for (const QString& option : settings.options)
            {
                const qsizetype index = m_settings.options.indexOf(option);
                settings.exportValues << ((index >= 0 && index < m_settings.exportValues.size()) ? m_settings.exportValues[index] : option);
            }
        }
    }
    if (m_editableCheckBox)
    {
        settings.editable = m_editableCheckBox->isChecked();
    }
    if (m_exportValueEdit)
    {
        // A value is a name in PDF - spaces and special characters are replaced
        QString exportValue = m_exportValueEdit->text().trimmed();
        exportValue.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
        settings.exportValue = exportValue.isEmpty() ? m_settings.exportValue : exportValue;
        settings.checked = m_checkedCheckBox->isChecked();
    }

    if (m_fontComboBox)
    {
        settings.fontName = m_fontComboBox->currentData().toByteArray();
    }
    settings.textColor = m_textColor;
    settings.borderStyle = pdf::PDFFireFormFields::BorderStyle(m_borderStyleComboBox->currentIndex());
    if (m_scrollCheckBox)
    {
        settings.doNotScroll = !m_scrollCheckBox->isChecked();
    }
    if (m_passwordCheckBox)
    {
        settings.password = m_passwordCheckBox->isChecked();
    }
    if (m_combCheckBox)
    {
        settings.comb = m_combCheckBox->isChecked() && m_combCheckBox->isEnabled();
    }
    if (m_spellCheckBox)
    {
        settings.doNotSpellCheck = !m_spellCheckBox->isChecked();
    }
    settings.borderColor = m_borderCheckBox->isChecked() ? m_borderColor : QColor();
    settings.borderWidth = m_borderWidthSpinBox->value();
    settings.backgroundColor = m_backgroundCheckBox->isChecked() ? m_backgroundColor : QColor();
    return settings;
}

void FormFieldPropertiesDialog::accept()
{
    const pdf::PDFFireFormFields::Settings settings = getSettings();

    if (settings.name.isEmpty())
    {
        QMessageBox::warning(this, tr("Field Name"), tr("The field must have a name."));
        m_nameEdit->setFocus();
        return;
    }

    if (settings.name.contains(QChar('.')))
    {
        QMessageBox::warning(this, tr("Field Name"), tr("The name of a field cannot contain a dot."));
        m_nameEdit->setFocus();
        return;
    }

    // Two fields with the same name would share their value - except radio buttons,
    // whose group is chosen by the name
    if (settings.type != Type::RadioButton && settings.name.compare(m_settings.name, Qt::CaseInsensitive) != 0 &&
        m_usedNames.contains(settings.name, Qt::CaseInsensitive))
    {
        QMessageBox::warning(this, tr("Field Name"), tr("Another field is already named '%1'. Choose another name.").arg(settings.name));
        m_nameEdit->setFocus();
        return;
    }

    if (m_settings.type == Type::ComboBox || m_settings.type == Type::ListBox)
    {
        if (settings.options.isEmpty())
        {
            QMessageBox::warning(this, tr("Items"), tr("Write the items of the list, one on a line."));
            m_optionsEdit->setFocus();
            return;
        }
        if (!settings.value.isEmpty() && !settings.options.contains(settings.value) && !settings.editable)
        {
            QMessageBox::warning(this, tr("Default Item"), tr("The default item must be one of the items."));
            m_valueEdit->setFocus();
            return;
        }
    }

    QDialog::accept();
}

}   // namespace pdfplugin
