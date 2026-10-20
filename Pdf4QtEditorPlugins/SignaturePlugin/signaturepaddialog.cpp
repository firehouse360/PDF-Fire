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

#include "signaturepaddialog.h"
#include "signdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QTabWidget>
#include <QTabletEvent>
#include <QVBoxLayout>

namespace pdfplugin
{

// ---- SignaturePadWidget ----------------------------------------------------------

SignaturePadWidget::SignaturePadWidget(QWidget* parent) :
    QWidget(parent)
{
    setObjectName("signaturePadWidget");
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_TabletTracking, false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize SignaturePadWidget::sizeHint() const
{
    return QSize(480, 170);
}

QSize SignaturePadWidget::minimumSizeHint() const
{
    return QSize(240, 100);
}

void SignaturePadWidget::undo()
{
    if (!m_strokes.empty())
    {
        m_strokes.pop_back();
        update();
        Q_EMIT changed();
    }
}

void SignaturePadWidget::clear()
{
    if (!m_strokes.empty())
    {
        m_strokes.clear();
        update();
        Q_EMIT changed();
    }
}

void SignaturePadWidget::setStrokes(std::vector<pdf::PDFFireSavedSignature::Stroke> strokes, QSizeF padSize)
{
    // The strokes are in the coordinates of a pad of the given size - they are fitted
    // into this pad, when it gets its size (or now, if it has it)
    m_strokes = std::move(strokes);
    m_lastSize = padSize.toSize();
    if (isVisible())
    {
        rescale(size());
    }
    update();
    Q_EMIT changed();
}

void SignaturePadWidget::rescale(QSize newSize)
{
    if (m_lastSize.isValid() && !m_strokes.empty() && m_lastSize.width() > 0 && m_lastSize.height() > 0 && newSize != m_lastSize)
    {
        const qreal scale = qMin(qreal(newSize.width()) / m_lastSize.width(), qreal(newSize.height()) / m_lastSize.height());
        for (auto& stroke : m_strokes)
        {
            for (QPointF& point : stroke)
            {
                point *= scale;
            }
        }
    }
    m_lastSize = newSize;
}

void SignaturePadWidget::addPoint(const QPointF& point, bool isNewStroke)
{
    if (isNewStroke || m_strokes.empty())
    {
        m_strokes.emplace_back();
    }

    // Points closer than half a pixel add nothing
    pdf::PDFFireSavedSignature::Stroke& stroke = m_strokes.back();
    if (!stroke.empty() && QLineF(stroke.back(), point).length() < 0.5)
    {
        return;
    }
    stroke.push_back(point);
    update();
}

void SignaturePadWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_isDrawing = true;
        addPoint(event->position(), true);
        event->accept();
    }
}

void SignaturePadWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDrawing && event->buttons().testFlag(Qt::LeftButton))
    {
        addPoint(event->position(), false);
        event->accept();
    }
}

void SignaturePadWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_isDrawing && event->button() == Qt::LeftButton)
    {
        m_isDrawing = false;
        event->accept();
        Q_EMIT changed();
    }
}

void SignaturePadWidget::tabletEvent(QTabletEvent* event)
{
    // A pen draws as the mouse does (the mouse events are synthesized otherwise)
    event->ignore();
}

void SignaturePadWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // The drawing grows and shrinks with the pad
    rescale(size());
}

void SignaturePadWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // The colours of the theme - the pad works in the dark and in the light theme
    const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(hasFocus() ? palette().color(QPalette::Highlight) : palette().color(QPalette::Mid), 1.0));
    painter.setBrush(palette().color(QPalette::Base));
    painter.drawRoundedRect(rect, 4, 4);

    // The line to sign on
    const qreal baseline = height() * 0.72;
    painter.setPen(QPen(palette().color(QPalette::Mid), 1.0, Qt::DashLine));
    painter.drawLine(QPointF(width() * 0.06, baseline), QPointF(width() * 0.94, baseline));
    if (m_strokes.empty())
    {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(QRectF(width() * 0.06, baseline - height() * 0.4, width() * 0.88, height() * 0.38), Qt::AlignLeft | Qt::AlignBottom,
                         tr("Draw here with the mouse or a pen"));
    }

    painter.setPen(QPen(palette().color(QPalette::Text), qMax(1.5, height() * 0.025), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const auto& stroke : m_strokes)
    {
        if (stroke.size() == 1)
        {
            painter.drawPoint(stroke.front());
            continue;
        }
        QPainterPath path(stroke.front());
        for (size_t i = 1; i < stroke.size(); ++i)
        {
            path.lineTo(stroke[i]);
        }
        painter.drawPath(path);
    }
}

// ---- SignaturePadDialog ----------------------------------------------------------

SignaturePadDialog::SignaturePadDialog(QWidget* parent, pdf::PDFFireSavedSignatureStore::Role role, const pdf::PDFFireSavedSignature& signature) :
    QDialog(parent),
    m_role(role)
{
    const bool isInitials = role == pdf::PDFFireSavedSignatureStore::Role::Initials;
    setWindowTitle(isInitials ? tr("Initials") : tr("Signature"));
    setObjectName("signaturePadDialog");

    QVBoxLayout* layout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName("signaturePadTabs");
    layout->addWidget(m_tabWidget);

    // Draw
    QWidget* drawPage = new QWidget(m_tabWidget);
    QVBoxLayout* drawLayout = new QVBoxLayout(drawPage);
    m_padWidget = new SignaturePadWidget(drawPage);
    drawLayout->addWidget(m_padWidget, 1);
    QHBoxLayout* drawButtonsLayout = new QHBoxLayout();
    m_undoButton = new QPushButton(tr("Undo"), drawPage);
    m_undoButton->setObjectName("signaturePadUndoButton");
    m_undoButton->setToolTip(tr("Take back the last stroke (Ctrl+Z)"));
    m_clearButton = new QPushButton(tr("Clear"), drawPage);
    m_clearButton->setObjectName("signaturePadClearButton");
    m_clearButton->setToolTip(tr("Start again"));
    drawButtonsLayout->addWidget(m_undoButton);
    drawButtonsLayout->addWidget(m_clearButton);
    drawButtonsLayout->addStretch(1);
    drawLayout->addLayout(drawButtonsLayout);
    m_tabWidget->addTab(drawPage, tr("Draw"));

    // Type
    QWidget* typePage = new QWidget(m_tabWidget);
    QGridLayout* typeLayout = new QGridLayout(typePage);
    m_textEdit = new QLineEdit(typePage);
    m_textEdit->setObjectName("signaturePadTextEdit");
    m_textEdit->setPlaceholderText(isInitials ? tr("Your initials") : tr("Your name"));
    m_textEdit->setMaxLength(60);
    m_fontCombo = new QComboBox(typePage);
    m_fontCombo->setObjectName("signaturePadFontCombo");
    for (const QString& family : SignDialog::getHandwritingFontFamilies())
    {
        m_fontCombo->addItem(family, family);
        m_fontCombo->setItemData(m_fontCombo->count() - 1, QFont(family, 14), Qt::FontRole);
    }
    m_typedPreviewLabel = new QLabel(typePage);
    m_typedPreviewLabel->setObjectName("signaturePadTypedPreview");
    m_typedPreviewLabel->setMinimumSize(360, 110);
    m_typedPreviewLabel->setAlignment(Qt::AlignCenter);
    m_typedPreviewLabel->setFrameShape(QFrame::StyledPanel);
    typeLayout->addWidget(new QLabel(isInitials ? tr("Initials:") : tr("Name:"), typePage), 0, 0);
    typeLayout->addWidget(m_textEdit, 0, 1);
    typeLayout->addWidget(new QLabel(tr("Handwriting:"), typePage), 1, 0);
    typeLayout->addWidget(m_fontCombo, 1, 1);
    typeLayout->addWidget(m_typedPreviewLabel, 2, 0, 1, 2);
    typeLayout->setRowStretch(2, 1);
    m_tabWidget->addTab(typePage, tr("Type"));

    // Ink, save
    QHBoxLayout* inkLayout = new QHBoxLayout();
    m_colorCombo = new QComboBox(this);
    m_colorCombo->setObjectName("signaturePadColorCombo");
    m_colorCombo->addItem(tr("Black"), QColor(Qt::black));
    m_colorCombo->addItem(tr("Blue"), QColor(16, 52, 166));
    m_colorCombo->addItem(tr("Dark blue"), QColor(16, 32, 96));
    inkLayout->addWidget(new QLabel(tr("Ink on the page:"), this));
    inkLayout->addWidget(m_colorCombo);
    inkLayout->addStretch(1);
    layout->addLayout(inkLayout);

    m_saveCheckBox = new QCheckBox(isInitials ? tr("Save for later - next time the initials are placed with one click")
                                              : tr("Save for later - next time the signature is placed with one click"), this);
    m_saveCheckBox->setObjectName("signaturePadSaveCheckBox");
    m_saveCheckBox->setChecked(true);
    layout->addWidget(m_saveCheckBox);

    QLabel* noteLabel = new QLabel(tr("A picture of a signature, written into the page. It is kept on this computer only, never in a document."), this);
    noteLabel->setWordWrap(true);
    noteLabel->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(noteLabel);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(tr("Use"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Ctrl+Z takes back the last stroke (in the Type tab the text field has its own undo)
    QShortcut* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, [this]()
    {
        if (m_tabWidget->currentIndex() == 0)
        {
            m_padWidget->undo();
        }
    });

    connect(m_undoButton, &QPushButton::clicked, m_padWidget, &SignaturePadWidget::undo);
    connect(m_clearButton, &QPushButton::clicked, m_padWidget, &SignaturePadWidget::clear);
    connect(m_padWidget, &SignaturePadWidget::changed, this, &SignaturePadDialog::updateUi);
    connect(m_textEdit, &QLineEdit::textChanged, this, &SignaturePadDialog::updateUi);
    connect(m_fontCombo, &QComboBox::currentIndexChanged, this, &SignaturePadDialog::updateUi);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &SignaturePadDialog::updateUi);

    // The signature being changed
    if (signature.getKind() == pdf::PDFFireSavedSignature::Kind::Typed)
    {
        m_tabWidget->setCurrentIndex(1);
        m_textEdit->setText(signature.getText());
        const int fontIndex = m_fontCombo->findData(signature.getFontFamily());
        if (fontIndex >= 0)
        {
            m_fontCombo->setCurrentIndex(fontIndex);
        }
    }
    if (!signature.isEmpty())
    {
        int colorIndex = -1;
        for (int i = 0; i < m_colorCombo->count(); ++i)
        {
            if (m_colorCombo->itemData(i).value<QColor>() == signature.getColor())
            {
                colorIndex = i;
            }
        }
        if (colorIndex < 0)
        {
            m_colorCombo->addItem(tr("Saved colour"), signature.getColor());
            colorIndex = m_colorCombo->count() - 1;
        }
        m_colorCombo->setCurrentIndex(colorIndex);
    }

    resize(560, 420);
    if (signature.getKind() == pdf::PDFFireSavedSignature::Kind::Drawn)
    {
        m_padWidget->setStrokes(signature.getStrokes(), signature.getPadSize());
    }

    updateUi();
}

QColor SignaturePadDialog::getColor() const
{
    const QColor color = m_colorCombo->currentData().value<QColor>();
    return color.isValid() ? color : QColor(Qt::black);
}

void SignaturePadDialog::updateUi()
{
    const bool isDraw = m_tabWidget->currentIndex() == 0;
    m_undoButton->setEnabled(!m_padWidget->isEmpty());
    m_clearButton->setEnabled(!m_padWidget->isEmpty());

    // The typed signature in the colours of the theme (the ink colour is used on the page)
    pdf::PDFFireSavedSignature typed;
    typed.setText(m_textEdit->text(), m_fontCombo->currentData().toString());
    const QSize previewSize = m_typedPreviewLabel->size().expandedTo(QSize(360, 110));
    const qreal ratio = devicePixelRatioF();
    QImage image = typed.toImage(previewSize * ratio, palette().color(QPalette::Text));
    image.setDevicePixelRatio(ratio);
    m_typedPreviewLabel->setPixmap(QPixmap::fromImage(image));

    m_okButton->setEnabled(isDraw ? !m_padWidget->isEmpty() : !m_textEdit->text().trimmed().isEmpty());
}

pdf::PDFFireSavedSignature SignaturePadDialog::getSignature() const
{
    pdf::PDFFireSavedSignature signature;
    if (m_tabWidget->currentIndex() == 0)
    {
        signature.setStrokes(m_padWidget->getStrokes(), QSizeF(m_padWidget->size()));
    }
    else
    {
        signature.setText(m_textEdit->text().trimmed(), m_fontCombo->currentData().toString());
    }
    signature.setColor(getColor());
    return signature;
}

bool SignaturePadDialog::isSaveRequested() const
{
    return m_saveCheckBox->isChecked();
}

void SignaturePadDialog::setSaveAlways()
{
    m_saveCheckBox->setChecked(true);
    m_saveCheckBox->setVisible(false);
    m_okButton->setText(tr("Save"));
}

// ---- SavedSignaturesDialog -------------------------------------------------------

SavedSignaturesDialog::SavedSignaturesDialog(QWidget* parent) :
    QDialog(parent)
{
    setWindowTitle(tr("Saved Signatures"));
    setObjectName("savedSignaturesDialog");

    QVBoxLayout* layout = new QVBoxLayout(this);
    QGridLayout* grid = new QGridLayout();
    layout->addLayout(grid);

    auto addRow = [this, grid](int row, const QString& title, pdf::PDFFireSavedSignatureStore::Role role, QLabel** label, QPushButton** deleteButton)
    {
        grid->addWidget(new QLabel(title, this), row, 0, Qt::AlignTop);
        *label = new QLabel(this);
        (*label)->setMinimumSize(300, 90);
        (*label)->setAlignment(Qt::AlignCenter);
        (*label)->setFrameShape(QFrame::StyledPanel);
        grid->addWidget(*label, row, 1);
        QVBoxLayout* buttons = new QVBoxLayout();
        QPushButton* editButton = new QPushButton(tr("Change..."), this);
        *deleteButton = new QPushButton(tr("Delete"), this);
        buttons->addWidget(editButton);
        buttons->addWidget(*deleteButton);
        buttons->addStretch(1);
        grid->addLayout(buttons, row, 2);
        connect(editButton, &QPushButton::clicked, this, [this, role]() { edit(role); });
        connect(*deleteButton, &QPushButton::clicked, this, [this, role]() { remove(role); });
    };
    addRow(0, tr("Signature:"), pdf::PDFFireSavedSignatureStore::Role::Signature, &m_signatureLabel, &m_deleteSignatureButton);
    addRow(1, tr("Initials:"), pdf::PDFFireSavedSignatureStore::Role::Initials, &m_initialsLabel, &m_deleteInitialsButton);

    QLabel* locationLabel = new QLabel(tr("Kept on this computer only: %1").arg(QDir::toNativeSeparators(pdf::PDFFireSavedSignatureStore::getFileName())), this);
    locationLabel->setWordWrap(true);
    locationLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    locationLabel->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(locationLabel);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    updateUi();
}

void SavedSignaturesDialog::updateUi()
{
    const qreal ratio = devicePixelRatioF();
    auto show = [this, ratio](QLabel* label, QPushButton* deleteButton, pdf::PDFFireSavedSignatureStore::Role role)
    {
        const pdf::PDFFireSavedSignature signature = pdf::PDFFireSavedSignatureStore::load(role);
        deleteButton->setEnabled(!signature.isEmpty());
        if (signature.isEmpty())
        {
            label->setPixmap(QPixmap());
            label->setText(tr("Nothing saved yet"));
            return;
        }

        QImage image = signature.toImage(QSize(300, 90) * ratio, palette().color(QPalette::Text));
        image.setDevicePixelRatio(ratio);
        label->setText(QString());
        label->setPixmap(QPixmap::fromImage(image));
    };

    SignDialog::getHandwritingFontFamilies();   // the fonts of the typed signatures
    show(m_signatureLabel, m_deleteSignatureButton, pdf::PDFFireSavedSignatureStore::Role::Signature);
    show(m_initialsLabel, m_deleteInitialsButton, pdf::PDFFireSavedSignatureStore::Role::Initials);
}

void SavedSignaturesDialog::edit(pdf::PDFFireSavedSignatureStore::Role role)
{
    SignaturePadDialog dialog(this, role, pdf::PDFFireSavedSignatureStore::load(role));
    dialog.setSaveAlways();
    if (dialog.exec() == QDialog::Accepted)
    {
        if (!pdf::PDFFireSavedSignatureStore::save(role, dialog.getSignature()))
        {
            QMessageBox::critical(this, windowTitle(), tr("The signature cannot be saved to '%1'.").arg(QDir::toNativeSeparators(pdf::PDFFireSavedSignatureStore::getFileName())));
        }
        updateUi();
    }
}

void SavedSignaturesDialog::remove(pdf::PDFFireSavedSignatureStore::Role role)
{
    const QString question = role == pdf::PDFFireSavedSignatureStore::Role::Initials ? tr("Delete the saved initials?") : tr("Delete the saved signature?");
    if (QMessageBox::question(this, windowTitle(), question, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes)
    {
        pdf::PDFFireSavedSignatureStore::remove(role);
        updateUi();
    }
}

}   // namespace pdfplugin
