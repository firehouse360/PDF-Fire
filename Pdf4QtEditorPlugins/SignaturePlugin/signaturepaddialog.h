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

#ifndef SIGNATUREPADDIALOG_H
#define SIGNATUREPADDIALOG_H

#include "pdffiresavedsignature.h"

#include <QDialog>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;

namespace pdfplugin
{

/// PDF Fire: a pad, on which a signature is drawn with the mouse (or a pen). Every
/// stroke can be taken back (undo), the pad can be cleared.
class SignaturePadWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SignaturePadWidget(QWidget* parent);

    const std::vector<pdf::PDFFireSavedSignature::Stroke>& getStrokes() const { return m_strokes; }
    bool isEmpty() const { return m_strokes.empty(); }

    /// Takes back the last stroke
    void undo();
    void clear();

    /// Shows a saved drawn signature (to be changed)
    void setStrokes(std::vector<pdf::PDFFireSavedSignature::Stroke> strokes, QSizeF padSize);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

signals:
    void changed();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void tabletEvent(QTabletEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    void addPoint(const QPointF& point, bool isNewStroke);
    void rescale(QSize newSize);

    std::vector<pdf::PDFFireSavedSignature::Stroke> m_strokes;
    QSize m_lastSize;
    bool m_isDrawing = false;
};

/// PDF Fire: a signature or initials for Fill & Sign - drawn on a pad, or typed in a
/// handwriting font. It can be saved for later use (outside of the document).
class SignaturePadDialog : public QDialog
{
    Q_OBJECT

public:
    /// \param role Signature, or initials
    /// \param signature The saved signature to be changed (can be empty)
    explicit SignaturePadDialog(QWidget* parent, pdf::PDFFireSavedSignatureStore::Role role, const pdf::PDFFireSavedSignature& signature);

    pdf::PDFFireSavedSignature getSignature() const;

    /// The user wants to use the signature again (Save for later is checked)
    bool isSaveRequested() const;

    /// Hides the Save for later check box (the dialog changes the saved signature itself)
    void setSaveAlways();

private:
    void updateUi();
    QColor getColor() const;

    pdf::PDFFireSavedSignatureStore::Role m_role;
    QTabWidget* m_tabWidget = nullptr;
    SignaturePadWidget* m_padWidget = nullptr;
    QPushButton* m_undoButton = nullptr;
    QPushButton* m_clearButton = nullptr;
    QLineEdit* m_textEdit = nullptr;
    QComboBox* m_fontCombo = nullptr;
    QLabel* m_typedPreviewLabel = nullptr;
    QComboBox* m_colorCombo = nullptr;
    QCheckBox* m_saveCheckBox = nullptr;
    QPushButton* m_okButton = nullptr;
};

/// PDF Fire: shows the saved signature and initials; they can be changed or deleted
class SavedSignaturesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SavedSignaturesDialog(QWidget* parent);

private:
    void updateUi();
    void edit(pdf::PDFFireSavedSignatureStore::Role role);
    void remove(pdf::PDFFireSavedSignatureStore::Role role);

    QLabel* m_signatureLabel = nullptr;
    QLabel* m_initialsLabel = nullptr;
    QPushButton* m_deleteSignatureButton = nullptr;
    QPushButton* m_deleteInitialsButton = nullptr;
};

}   // namespace pdfplugin

#endif // SIGNATUREPADDIALOG_H
