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

#ifndef PAGEMARKSDIALOGS_H
#define PAGEMARKSDIALOGS_H

#include "pagemarks.h"

#include <QDialog>
#include <QPushButton>

#include <array>
#include <memory>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;
class QTimer;
class QVBoxLayout;

namespace pdfplugin
{

/// A button showing a color, which opens the color dialog
class PageMarksColorButton : public QPushButton
{
    Q_OBJECT

public:
    explicit PageMarksColorButton(QColor color, QWidget* parent);

    QColor getColor() const { return m_color; }
    void setColor(QColor color);

Q_SIGNALS:
    void colorChanged();

private:
    QColor m_color;
};

/// The common part of the dialogs: the settings on the left, the preview of the
/// first chosen page on the right (the page with the mark, rendered by PDF4QT),
/// an error line, OK / Cancel. The mark is written into a copy of the document for
/// the preview, so the preview shows exactly what the command will write.
class PageMarksDialogBase : public QDialog
{
    Q_OBJECT

public:
    explicit PageMarksDialogBase(const pdf::PDFDocument* document, const QString& title, QWidget* parent);

protected:
    /// Writes the mark into the builder (only to the page, when it is given)
    virtual bool write(pdf::PDFDocumentBuilder* builder, QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage) const = 0;

    /// The page range of the dialog (the preview shows its first page)
    virtual QString getPageRange() const = 0;

    QVBoxLayout* getSettingsLayout() const { return m_settingsLayout; }

    /// The preview is rendered again after a short delay (typing does not render
    /// the page after each key)
    void schedulePreview();

    /// Connects the change signals of the widgets to the preview
    void watch(QWidget* widget);

    QComboBox* createFontCombo(pdfplugin::PageMarks::Font font);
    static PageMarks::Font getFont(const QComboBox* comboBox);

    virtual void accept() override;

    const pdf::PDFDocument* m_document;

private:
    void updatePreview();

    QVBoxLayout* m_settingsLayout;
    QLabel* m_previewLabel;
    QLabel* m_errorLabel;
    QPushButton* m_okButton;
    QTimer* m_previewTimer;
};

class WatermarkDialog : public PageMarksDialogBase
{
    Q_OBJECT

public:
    explicit WatermarkDialog(const pdf::PDFDocument* document, QWidget* parent);

    PageMarks::WatermarkSettings getSettings() const;

protected:
    virtual bool write(pdf::PDFDocumentBuilder* builder, QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage) const override;
    virtual QString getPageRange() const override;

private:
    void updateEnabled();
    void browseImage();

    QRadioButton* m_textRadio;
    QRadioButton* m_imageRadio;
    QLineEdit* m_textEdit;
    QComboBox* m_fontCombo;
    QDoubleSpinBox* m_sizeSpin;
    QCheckBox* m_fitCheck;
    PageMarksColorButton* m_colorButton;
    QLineEdit* m_imageEdit;
    QPushButton* m_browseButton;
    QSpinBox* m_scaleSpin;
    QCheckBox* m_relativeCheck;
    QSpinBox* m_opacitySpin;
    QComboBox* m_rotationCombo;
    QSpinBox* m_rotationSpin;
    QComboBox* m_layerCombo;
    QButtonGroup* m_positionGroup;
    QDoubleSpinBox* m_offsetXSpin;
    QDoubleSpinBox* m_offsetYSpin;
    QLineEdit* m_pagesEdit;
    bool m_rotationChosen = false;
    mutable QString m_loadedImageFile;
    mutable QByteArray m_loadedImageData;
};

class HeaderFooterDialog : public PageMarksDialogBase
{
    Q_OBJECT

public:
    explicit HeaderFooterDialog(const pdf::PDFDocument* document, const QString& fileName, QWidget* parent);

    PageMarks::HeaderFooterSettings getSettings() const;

protected:
    virtual bool write(pdf::PDFDocumentBuilder* builder, QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage) const override;
    virtual QString getPageRange() const override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void insertToken(const QString& token);

    QString m_fileName;
    std::array<QLineEdit*, PageMarks::BoxCount> m_boxEdits;
    QLineEdit* m_lastBoxEdit;
    QComboBox* m_fontCombo;
    QDoubleSpinBox* m_sizeSpin;
    PageMarksColorButton* m_colorButton;
    QDoubleSpinBox* m_marginTopSpin;
    QDoubleSpinBox* m_marginBottomSpin;
    QDoubleSpinBox* m_marginLeftSpin;
    QDoubleSpinBox* m_marginRightSpin;
    QComboBox* m_dateFormatCombo;
    QLineEdit* m_pagesEdit;
    QSpinBox* m_startNumberSpin;
    QCheckBox* m_skipFirstCheck;
};

class BatesDialog : public PageMarksDialogBase
{
    Q_OBJECT

public:
    explicit BatesDialog(const pdf::PDFDocument* document, QWidget* parent);

    PageMarks::BatesSettings getSettings() const;

protected:
    virtual bool write(pdf::PDFDocumentBuilder* builder, QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage) const override;
    virtual QString getPageRange() const override;

private:
    void updateSample();

    QLineEdit* m_prefixEdit;
    QLineEdit* m_suffixEdit;
    QLineEdit* m_startEdit;
    QSpinBox* m_digitsSpin;
    QComboBox* m_positionCombo;
    QComboBox* m_fontCombo;
    QDoubleSpinBox* m_sizeSpin;
    PageMarksColorButton* m_colorButton;
    QDoubleSpinBox* m_marginSpin;
    QLineEdit* m_pagesEdit;
    QLabel* m_sampleLabel;
};

/// PDF Fire: a page of another PDF file as the background of the pages (Acrobat's
/// Edit PDF > Background > Add, From file)
class BackgroundDialog : public PageMarksDialogBase
{
    Q_OBJECT

public:
    explicit BackgroundDialog(const pdf::PDFDocument* document, pdf::PDFInteger currentPageIndex, QWidget* parent);

    PageMarks::BackgroundSettings getSettings() const;

    /// The document of the background file (nullptr, if no file is read)
    const pdf::PDFDocument* getSourceDocument() const { return m_sourceDocument.get(); }

protected:
    virtual bool write(pdf::PDFDocumentBuilder* builder, QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage) const override;
    virtual QString getPageRange() const override;

private:
    void browseFile();
    void readSourceFile();
    void updateEnabled();

    pdf::PDFInteger m_currentPageIndex;
    QLineEdit* m_fileEdit;
    QPushButton* m_browseButton;
    QSpinBox* m_sourcePageSpin;
    QLabel* m_sourcePageCountLabel;
    QComboBox* m_sizeCombo;
    QSpinBox* m_scaleSpin;
    QSpinBox* m_opacitySpin;
    QComboBox* m_positionCombo;
    QRadioButton* m_allPagesRadio;
    QRadioButton* m_currentPageRadio;
    QRadioButton* m_rangeRadio;
    QLineEdit* m_pagesEdit;

    /// The file read last (it is read again only when another file is chosen)
    QString m_sourceFileName;
    QString m_sourceError;
    std::shared_ptr<pdf::PDFDocument> m_sourceDocument;
};

/// Which marks are removed (with the numbers of the marks found)
class RemoveMarksDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RemoveMarksDialog(const pdf::PDFDocument* document, QWidget* parent);

    int getKinds() const;

private:
    QCheckBox* m_watermarkCheck;
    QCheckBox* m_headerFooterCheck;
    QCheckBox* m_batesCheck;
    QCheckBox* m_backgroundCheck;
};

}   // namespace pdfplugin

#endif // PAGEMARKSDIALOGS_H
