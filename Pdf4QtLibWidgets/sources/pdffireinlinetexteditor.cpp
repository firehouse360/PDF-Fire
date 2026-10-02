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

#include "pdffireinlinetexteditor.h"

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextLayout>
#include <QTimer>

namespace pdf
{

PDFFireInlineTextEditor::PDFFireInlineTextEditor(QWidget* parent) :
    BaseClass(parent)
{
    setFrameShape(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTabChangesFocus(true);
    document()->setDocumentMargin(0);

    // The editor looks like the paper with the text on it, with a thin frame, so it
    // is clear, which text is being edited. The colours are fixed - the page is paper,
    // not a part of the (possibly dark) user interface.
    setStyleSheet(QStringLiteral("QPlainTextEdit { background: white; color: black; border: 1px solid #E8590C; "
                                 "selection-background-color: #9DC3F2; selection-color: black; }"));
}

void PDFFireInlineTextEditor::start(const QString& text, const QFont& font, const QRect& rect, QPoint clickPosition)
{
    m_isFinished = false;
    m_originalRect = rect;

    setFont(font);
    setPlainText(text);

    // The editor is as high as a line of the text (at least as the text on the page)
    const QFontMetricsF metrics(font);
    const int lineCount = qMax(1, document()->blockCount());
    const int height = qMax(rect.height(), qCeil(metrics.lineSpacing() * lineCount)) + 4;
    setGeometry(rect.left() - 2, rect.top() + (rect.height() - height) / 2, rect.width() + 4, height);
    updateWidth();
    connect(this, &QPlainTextEdit::textChanged, this, &PDFFireInlineTextEditor::updateWidth, Qt::UniqueConnection);

    show();
    raise();
    setFocus(Qt::MouseFocusReason);

    // The caret is put, where the user clicked
    QTextCursor cursor = textCursor();
    if (clickPosition.x() >= 0 && rect.adjusted(-4, -4, 4, 4).contains(clickPosition))
    {
        cursor = cursorForPosition(viewport()->mapFrom(parentWidget(), clickPosition));
    }
    else
    {
        cursor.movePosition(QTextCursor::End);
    }
    setTextCursor(cursor);
}

void PDFFireInlineTextEditor::finish()
{
    if (!m_isFinished)
    {
        m_isFinished = true;
        Q_EMIT committed(toPlainText());
    }
}

void PDFFireInlineTextEditor::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
        case Qt::Key_Escape:
            event->accept();
            if (!m_isFinished)
            {
                m_isFinished = true;
                Q_EMIT cancelled();
            }
            return;

        case Qt::Key_Return:
        case Qt::Key_Enter:
            // A line of a PDF is one line - Enter finishes the editing, as in a cell
            event->accept();
            finish();
            return;

        default:
            break;
    }

    BaseClass::keyPressEvent(event);
}

void PDFFireInlineTextEditor::focusOutEvent(QFocusEvent* event)
{
    BaseClass::focusOutEvent(event);

    // A context menu or a dialog of the editor itself takes the focus only for a while
    if (event->reason() == Qt::PopupFocusReason)
    {
        return;
    }

    // The editing finishes, when the user clicks elsewhere (after the event is processed)
    QTimer::singleShot(0, this, &PDFFireInlineTextEditor::finish);
}

void PDFFireInlineTextEditor::updateWidth()
{
    // The editor grows with the text, it is never narrower than the original text
    const QFontMetricsF metrics(font());
    qreal width = 0.0;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next())
    {
        width = qMax(width, metrics.horizontalAdvance(block.text()));
    }

    const int newWidth = qMax(m_originalRect.width() + 4, qCeil(width + metrics.averageCharWidth() * 2) + 4);
    if (newWidth != this->width())
    {
        resize(newWidth, height());
    }
}

PDFFireInlineParagraphEditor::PDFFireInlineParagraphEditor(QWidget* parent) :
    BaseClass(parent)
{
    setFrameShape(QFrame::NoFrame);
    setAcceptRichText(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTabChangesFocus(true);
    document()->setDocumentMargin(0);
    setStyleSheet(QStringLiteral("QTextEdit { background: white; color: black; border: 1px solid #E8590C; "
                                 "selection-background-color: #9DC3F2; selection-color: black; }"));
}

void PDFFireInlineParagraphEditor::start(const QString& text, const QFont& font, const QRect& rect, qreal lineHeight, qreal firstLineIndent, QPoint clickPosition)
{
    m_isFinished = false;
    m_lineHeight = qMax<qreal>(lineHeight, 1.0);
    m_firstLineIndent = firstLineIndent;
    m_lineWidth = qMax(rect.width(), 10);

    setFont(font);
    document()->setDefaultFont(font);
    setPlainText(text);

    // The lines are wrapped exactly at the width of the lines of the paragraph
    setLineWrapMode(QTextEdit::FixedPixelWidth);
    setLineWrapColumnOrWidth(m_lineWidth);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    updateFormat();
    setGeometry(rect.left() - 2, rect.top() - 2, m_lineWidth + 4 + 2, rect.height() + 4);
    updateHeight();
    connect(this, &QTextEdit::textChanged, this, &PDFFireInlineParagraphEditor::updateHeight, Qt::UniqueConnection);

    show();
    raise();
    setFocus(Qt::MouseFocusReason);

    QTextCursor cursor = textCursor();
    if (clickPosition.x() >= 0 && rect.adjusted(-4, -4, 4, 4).contains(clickPosition))
    {
        cursor = cursorForPosition(viewport()->mapFrom(parentWidget(), clickPosition));
    }
    else
    {
        cursor.movePosition(QTextCursor::End);
    }
    setTextCursor(cursor);
}

void PDFFireInlineParagraphEditor::updateFormat()
{
    // The distance of the lines is the distance of the lines on the page, the first
    // line keeps its indentation
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(m_lineHeight, QTextBlockFormat::FixedHeight);
    format.setTopMargin(0);
    format.setBottomMargin(0);
    cursor.mergeBlockFormat(format);

    QTextCursor firstBlock(document()->begin());
    QTextBlockFormat firstFormat = document()->begin().blockFormat();
    firstFormat.setTextIndent(m_firstLineIndent);
    firstBlock.setBlockFormat(firstFormat);
}

void PDFFireInlineParagraphEditor::updateHeight()
{
    // A new block (Enter) gets the line height too
    const QSignalBlocker blocker(this);
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(m_lineHeight, QTextBlockFormat::FixedHeight);
    cursor.mergeBlockFormat(format);

    const int lineCount = qMax(1, int(getLines().size()));
    const int height = qCeil(m_lineHeight * lineCount) + 6;
    if (height != this->height())
    {
        resize(width(), height);
    }
}

QStringList PDFFireInlineParagraphEditor::getLines() const
{
    QStringList lines;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next())
    {
        const QTextLayout* layout = block.layout();
        if (!layout || layout->lineCount() == 0 || block.text().isEmpty())
        {
            lines << QString();
            continue;
        }

        for (int i = 0; i < layout->lineCount(); ++i)
        {
            const QTextLine line = layout->lineAt(i);
            QString text = block.text().mid(line.textStart(), line.textLength());

            // The space, at which the line is wrapped, is not a part of the line
            while (text.endsWith(QChar(' ')))
            {
                text.chop(1);
            }
            lines << text;
        }
    }
    return lines;
}

void PDFFireInlineParagraphEditor::finish()
{
    if (!m_isFinished)
    {
        m_isFinished = true;
        Q_EMIT committed(getLines());
    }
}

void PDFFireInlineParagraphEditor::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
    {
        event->accept();
        if (!m_isFinished)
        {
            m_isFinished = true;
            Q_EMIT cancelled();
        }
        return;
    }

    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers().testFlag(Qt::ControlModifier))
    {
        event->accept();
        finish();
        return;
    }

    BaseClass::keyPressEvent(event);
}

void PDFFireInlineParagraphEditor::focusOutEvent(QFocusEvent* event)
{
    BaseClass::focusOutEvent(event);
    if (event->reason() == Qt::PopupFocusReason)
    {
        return;
    }
    QTimer::singleShot(0, this, &PDFFireInlineParagraphEditor::finish);
}

}   // namespace pdf
