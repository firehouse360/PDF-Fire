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

#ifndef PDFFIREINLINETEXTEDITOR_H
#define PDFFIREINLINETEXTEDITOR_H

#include "pdfwidgetsglobal.h"

#include <QPlainTextEdit>
#include <QTextEdit>

namespace pdf
{

/// PDF Fire: edits a line of the text of a page in place, like a word processor:
/// a caret in the text, selection by the mouse and the keyboard, clipboard, undo.
/// The editor is put exactly over the text (it covers the text with the colour of
/// the paper, and shows the same text in the font of the document). The text is
/// written back the same way as by the edit dialog, so the original font and the
/// spacing of the untouched characters are kept.
///
/// Enter or a click outside commits the text, Esc cancels the editing.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFFireInlineTextEditor : public QPlainTextEdit
{
    Q_OBJECT

private:
    using BaseClass = QPlainTextEdit;

public:
    explicit PDFFireInlineTextEditor(QWidget* parent);

    /// Starts the editing
    /// \param text Text to be edited
    /// \param font Font, in which the text is shown (pixel size set)
    /// \param rect Rectangle of the text in the coordinates of the parent widget
    /// \param clickPosition Position of the click, which started the editing (the caret is
    ///        put there), in the coordinates of the parent widget; invalid - at the end
    void start(const QString& text, const QFont& font, const QRect& rect, QPoint clickPosition);

    /// Finishes the editing - the text is committed (if not cancelled before)
    void finish();

signals:
    void committed(const QString& text);
    void cancelled();

protected:
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void focusOutEvent(QFocusEvent* event) override;

private:
    void updateWidth();

    bool m_isFinished = false;
    QRect m_originalRect;
};

/// PDF Fire: edits a paragraph of the page in place - several lines, which are
/// wrapped at the width of the paragraph while typing, as in a word processor. The
/// lines, as they are wrapped in the editor, are written back as the lines of the
/// paragraph. Enter starts a new line, Ctrl+Enter or a click outside commits the
/// text, Esc cancels the editing.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFFireInlineParagraphEditor : public QTextEdit
{
    Q_OBJECT

private:
    using BaseClass = QTextEdit;

public:
    explicit PDFFireInlineParagraphEditor(QWidget* parent);

    /// Starts the editing
    /// \param text Text of the paragraph (a line feed is a line break of the user)
    /// \param font Font, in which the text is shown
    /// \param rect Rectangle of the paragraph (the width is the width of the lines)
    /// \param lineHeight Distance of the lines (pixels)
    /// \param firstLineIndent Indentation of the first line (pixels)
    /// \param clickPosition Position of the click, which started the editing
    void start(const QString& text, const QFont& font, const QRect& rect, qreal lineHeight, qreal firstLineIndent, QPoint clickPosition);

    /// Returns the lines, as they are wrapped in the editor (an empty line is a
    /// line, which the user left empty)
    QStringList getLines() const;

    void finish();

signals:
    void committed(const QStringList& lines);
    void cancelled();

protected:
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void focusOutEvent(QFocusEvent* event) override;

private:
    void updateFormat();
    void updateHeight();

    bool m_isFinished = false;
    qreal m_lineHeight = 0.0;
    qreal m_firstLineIndent = 0.0;
    int m_lineWidth = 0;
};

}   // namespace pdf

#endif // PDFFIREINLINETEXTEDITOR_H
