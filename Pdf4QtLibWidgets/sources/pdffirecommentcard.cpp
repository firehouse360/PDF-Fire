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

#include "pdffirecommentcard.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>

#include "pdfdbgheap.h"

namespace pdf
{

// Ported from PdfCraft crates/ui-egui/src/comments.rs composer (MIT OR Apache-2.0)

/// Accent colour of PDF Fire (the orange of the logo)
static constexpr const char* COMMENT_CARD_ACCENT = "#f2561d";
static constexpr const char* COMMENT_CARD_ACCENT_HOVER = "#ff6a33";
static constexpr const char* COMMENT_CARD_ACCENT_PRESSED = "#d8460f";

PDFFireCommentCard::PDFFireCommentCard(const QString& author,
                                       const QString& label,
                                       const QString& text,
                                       const QString& placeholder,
                                       QWidget* parent) :
    BaseClass(parent),
    m_authorLabel(nullptr),
    m_kindLabel(nullptr),
    m_editor(nullptr),
    m_cancelButton(nullptr),
    m_postButton(nullptr)
{
    setObjectName("pdfFireCommentCard");
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);

    // Header: author (bold) and the kind of the comment (muted)
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(8);
    m_authorLabel = new QLabel(author, this);
    m_authorLabel->setObjectName("pdfFireCommentCardAuthor");
    m_authorLabel->setTextFormat(Qt::PlainText);
    QFont authorFont = m_authorLabel->font();
    authorFont.setBold(true);
    m_authorLabel->setFont(authorFont);
    m_kindLabel = new QLabel(label, this);
    m_kindLabel->setObjectName("pdfFireCommentCardLabel");
    m_kindLabel->setTextFormat(Qt::PlainText);
    QFont kindFont = m_kindLabel->font();
    kindFont.setPointSizeF(kindFont.pointSizeF() * 0.9);
    m_kindLabel->setFont(kindFont);
    headerLayout->addWidget(m_authorLabel);
    headerLayout->addWidget(m_kindLabel);
    headerLayout->addStretch(1);
    layout->addLayout(headerLayout);

    // Text of the comment (several lines)
    m_editor = new QPlainTextEdit(this);
    m_editor->setObjectName("pdfFireCommentCardEditor");
    m_editor->setPlaceholderText(placeholder);
    m_editor->setPlainText(text);
    m_editor->setTabChangesFocus(true);
    const int lineHeight = m_editor->fontMetrics().lineSpacing();
    m_editor->setFixedHeight(lineHeight * 4 + 12);
    m_editor->installEventFilter(this);
    m_editor->moveCursor(QTextCursor::End);
    layout->addWidget(m_editor);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    buttonLayout->addStretch(1);
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setObjectName("pdfFireCommentCardCancel");
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_postButton = new QPushButton(tr("Post"), this);
    m_postButton->setObjectName("pdfFireCommentCardPost");
    m_postButton->setCursor(Qt::PointingHandCursor);
    m_postButton->setToolTip(tr("Post (Ctrl+Enter)"));
    m_cancelButton->setToolTip(tr("Cancel (Esc)"));
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_postButton);
    layout->addLayout(buttonLayout);

    connect(m_postButton, &QPushButton::clicked, this, &PDFFireCommentCard::post);
    connect(m_cancelButton, &QPushButton::clicked, this, &PDFFireCommentCard::cancel);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &PDFFireCommentCard::updateButtons);

    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(18);
    shadow->setOffset(0, 3);
    shadow->setColor(QColor(0, 0, 0, 90));
    setGraphicsEffect(shadow);

    setFixedWidth(qMax(260, fontMetrics().horizontalAdvance(QLatin1Char('M')) * 22));
    updateStyle();
    updateButtons();
}

void PDFFireCommentCard::showAt(QWidget* host, const QPoint& anchor)
{
    if (parentWidget() != host)
    {
        setParent(host);
    }

    adjustSize();
    const QSize size = sizeHint().expandedTo(QSize(width(), 0));
    resize(width(), size.height());

    // 12 pixels to the right of the clicked point, the top a little above it; inside the host
    QPoint position = anchor + QPoint(12, -12);
    const QRect hostRect = host ? host->rect() : QRect();
    if (hostRect.isValid())
    {
        if (position.x() + width() > hostRect.right() - 8)
        {
            // Not enough place on the right - the card is put to the left of the point
            position.setX(anchor.x() - 12 - width());
        }
        position.setX(qBound(hostRect.left() + 8, position.x(), qMax(hostRect.left() + 8, hostRect.right() - 8 - width())));
        position.setY(qBound(hostRect.top() + 8, position.y(), qMax(hostRect.top() + 8, hostRect.bottom() - 8 - height())));
    }

    move(position);
    show();
    raise();
    m_editor->setFocus(Qt::OtherFocusReason);
}

void PDFFireCommentCard::setEmbedded()
{
    setGraphicsEffect(nullptr);
    setMinimumWidth(0);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void PDFFireCommentCard::setPostButtonText(const QString& text)
{
    m_postButton->setText(text);
}

QString PDFFireCommentCard::getText() const
{
    return m_editor->toPlainText();
}

void PDFFireCommentCard::post()
{
    if (m_isClosed || getText().trimmed().isEmpty())
    {
        return;
    }

    close(true);
}

void PDFFireCommentCard::cancel()
{
    if (m_isClosed)
    {
        return;
    }

    close(false);
}

void PDFFireCommentCard::close(bool isPosted)
{
    m_isClosed = true;
    const QString text = getText();
    hide();
    deleteLater();

    if (isPosted)
    {
        Q_EMIT posted(text);
    }
    else
    {
        Q_EMIT cancelled();
    }
}

bool PDFFireCommentCard::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_editor)
    {
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
        {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            const bool isReturn = keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;
            const bool isPostKey = isReturn && (keyEvent->modifiers() & (Qt::ControlModifier | Qt::MetaModifier));
            const bool isCancelKey = keyEvent->key() == Qt::Key_Escape;

            if (isPostKey || isCancelKey)
            {
                // The keys belong to the card - the shortcuts of the application must not get them
                event->accept();
                if (event->type() == QEvent::KeyPress)
                {
                    if (isPostKey)
                    {
                        post();
                    }
                    else
                    {
                        cancel();
                    }
                }
                return true;
            }
        }
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFFireCommentCard::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if ((event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) && !m_isUpdatingStyle)
    {
        updateStyle();
    }
}

void PDFFireCommentCard::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}

void PDFFireCommentCard::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
}

void PDFFireCommentCard::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->accept();
}

void PDFFireCommentCard::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
}

void PDFFireCommentCard::wheelEvent(QWheelEvent* event)
{
    event->accept();
}

void PDFFireCommentCard::updateButtons()
{
    m_postButton->setEnabled(!getText().trimmed().isEmpty());
}

void PDFFireCommentCard::updateStyle()
{
    m_isUpdatingStyle = true;

    const QPalette palette = this->palette();
    const QColor background = palette.color(QPalette::Window);
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::WindowText);
    const QColor border = palette.color(QPalette::Mid);
    const QColor muted = palette.color(QPalette::PlaceholderText);

    QString styleSheet = QStringLiteral(
        "QFrame#pdfFireCommentCard { background: %1; border: 1px solid %2; border-radius: 8px; }"
        "QLabel#pdfFireCommentCardAuthor { color: %3; background: transparent; border: none; }"
        "QLabel#pdfFireCommentCardLabel { color: %4; background: transparent; border: none; }"
        "QPlainTextEdit#pdfFireCommentCardEditor { background: %5; color: %3; border: 1px solid %2; border-radius: 6px; padding: 2px; }"
        "QPlainTextEdit#pdfFireCommentCardEditor:focus { border: 1px solid %6; }"
        "QPushButton#pdfFireCommentCardCancel { background: transparent; color: %3; border: 1px solid %2; border-radius: 13px; padding: 4px 14px; min-height: 18px; }"
        "QPushButton#pdfFireCommentCardCancel:hover { background: %7; }"
        "QPushButton#pdfFireCommentCardPost { background: %6; color: white; border: none; border-radius: 13px; padding: 5px 16px; min-height: 18px; font-weight: bold; }"
        "QPushButton#pdfFireCommentCardPost:hover { background: %8; }"
        "QPushButton#pdfFireCommentCardPost:pressed { background: %9; }"
        "QPushButton#pdfFireCommentCardPost:disabled { background: %10; color: %11; }");

    QColor hover = text;
    hover.setAlphaF(0.08f);
    QColor disabledAccent = QColor(COMMENT_CARD_ACCENT);
    disabledAccent.setAlphaF(0.35f);
    QColor disabledText(Qt::white);
    disabledText.setAlphaF(0.7f);

    auto rgba = [](const QColor& color) { return QString("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alphaF(), 0, 'f', 2); };

    styleSheet = styleSheet.arg(background.name(), border.name(), text.name(), muted.name(), base.name(), QString(COMMENT_CARD_ACCENT),
                                rgba(hover), QString(COMMENT_CARD_ACCENT_HOVER), QString(COMMENT_CARD_ACCENT_PRESSED))
                           .arg(rgba(disabledAccent), rgba(disabledText));
    setStyleSheet(styleSheet);

    m_isUpdatingStyle = false;
}

}   // namespace pdf
