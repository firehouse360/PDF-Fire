// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "createredacteddocumentdialog.h"
#include "ui_createredacteddocumentdialog.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>

#include "pdfwidgetutils.h"

namespace pdfplugin
{

CreateRedactedDocumentDialog::CreateRedactedDocumentDialog(QString fileName, QColor fillColor, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::CreateRedactedDocumentDialog)
{
    ui->setupUi(this);
    ui->fileNameEdit->setText(fileName);
    ui->fillRedactedAreaColorEdit->setText(fillColor.name(QColor::HexRgb));

    // PDF Fire: a plain box, or a box with REDACTED written in its centre (the choice is remembered)
    ui->labelStyleComboBox->addItem(tr("None - plain box"), QString());
    ui->labelStyleComboBox->addItem(tr("REDACTED in white"), QStringLiteral("white"));
    ui->labelStyleComboBox->addItem(tr("REDACTED in red"), QStringLiteral("red"));
    const int labelStyleIndex = ui->labelStyleComboBox->findData(QSettings().value(QStringLiteral("RedactPlugin/LabelStyle"), QString()).toString());
    ui->labelStyleComboBox->setCurrentIndex(qMax(labelStyleIndex, 0));

    connect(ui->copyMetadataCheckBox, &QCheckBox::clicked, this, &CreateRedactedDocumentDialog::updateUi);
    connect(ui->fillRedactedAreaCheckBox, &QCheckBox::clicked, this, &CreateRedactedDocumentDialog::updateUi);

    updateUi();
    setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 300));
    pdf::PDFWidgetUtils::style(this);
}

CreateRedactedDocumentDialog::~CreateRedactedDocumentDialog()
{
    delete ui;
}

QString CreateRedactedDocumentDialog::getFileName() const
{
    return ui->fileNameEdit->text();
}

QColor CreateRedactedDocumentDialog::getRedactColor() const
{
    QColor color;

    if (ui->fillRedactedAreaCheckBox->isChecked())
    {
        color = QColor::fromString(ui->fillRedactedAreaColorEdit->text());
    }

    return color;
}

bool CreateRedactedDocumentDialog::isCopyingTitle() const
{
    return ui->copyTitleCheckBox->isChecked();
}

bool CreateRedactedDocumentDialog::isCopyingMetadata() const
{
    return ui->copyMetadataCheckBox->isChecked();
}

bool CreateRedactedDocumentDialog::isCopyingOutline() const
{
    return ui->copyOutlineCheckBox->isChecked();
}

bool CreateRedactedDocumentDialog::isKeepingText() const
{
    // PDF Fire: new redaction method, which keeps the text outside of the areas
    return ui->keepTextRadioButton->isChecked();
}

QString CreateRedactedDocumentDialog::getLabelText() const
{
    // The word needs the box under it - without the fill it is not written
    if (!ui->fillRedactedAreaCheckBox->isChecked() || ui->labelStyleComboBox->currentData().toString().isEmpty())
    {
        return QString();
    }
    return tr("REDACTED");
}

QColor CreateRedactedDocumentDialog::getLabelColor() const
{
    const QString style = ui->labelStyleComboBox->currentData().toString();
    if (style == QLatin1String("white"))
    {
        return Qt::white;
    }
    if (style == QLatin1String("red"))
    {
        return QColor(220, 0, 0);
    }
    return QColor();
}

void CreateRedactedDocumentDialog::on_selectDirectoryButton_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("File Name"), ui->fileNameEdit->text());
    if (!fileName.isEmpty())
    {
        ui->fileNameEdit->setText(fileName);
    }
}

void CreateRedactedDocumentDialog::updateUi()
{
    if (ui->copyMetadataCheckBox->isChecked())
    {
        ui->copyTitleCheckBox->setChecked(true);
        ui->copyTitleCheckBox->setEnabled(false);
    }
    else
    {
        ui->copyTitleCheckBox->setEnabled(true);
    }

    ui->fillRedactedAreaColorEdit->setEnabled(ui->fillRedactedAreaCheckBox->isChecked());
    ui->labelStyleComboBox->setEnabled(ui->fillRedactedAreaCheckBox->isChecked());
}

void CreateRedactedDocumentDialog::accept()
{
    if (ui->fillRedactedAreaCheckBox->isChecked())
    {
        QColor color = QColor::fromString(ui->fillRedactedAreaColorEdit->text());

        if (!color.isValid())
        {
            QMessageBox::critical(this, tr("Error"), tr("Cannot convert '%1' to color value.").arg(ui->fillRedactedAreaColorEdit->text()));
            return;
        }
    }

    QSettings().setValue(QStringLiteral("RedactPlugin/LabelStyle"), ui->labelStyleComboBox->currentData().toString());
    QDialog::accept();
}

} // namespace pdfplugin


