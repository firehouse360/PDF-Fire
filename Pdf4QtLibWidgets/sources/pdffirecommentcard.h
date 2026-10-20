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

#ifndef PDFFIRECOMMENTCARD_H
#define PDFFIRECOMMENTCARD_H

#include "pdfwidgetsglobal.h"

#include <QFrame>
#include <QString>

class QLabel;
class QPushButton;
class QPlainTextEdit;

namespace pdf
{

/// PDF Fire: the labelled card, in which the text of a comment is written - a new sticky note,
/// a reply, or the edited text of a comment. The card shows the author in bold and the kind of the
/// comment ("Sticky note") as its label, a text box of several lines, and the buttons Post and
/// Cancel. Ctrl+Enter posts, Esc cancels, Enter starts a new line. The card is not modal - it is
/// a child widget put next to the clicked point of the page view, so the document stays usable.
/// The colours are taken from the palette, so the card follows the dark and the light theme.
///
/// Layout ported from PdfCraft crates/ui-egui/src/comments.rs composer (MIT OR Apache-2.0,
/// Copyright (c) 2026 ArtCraft Team and the PdfCraft contributors).
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFFireCommentCard : public QFrame
{
    Q_OBJECT

private:
    using BaseClass = QFrame;

public:
    /// Creates the card (it is not shown yet, see showAt).
    /// \param author Author shown in the header (bold)
    /// \param label Kind of the comment shown next to the author ("Sticky note")
    /// \param text Initial text (the edited text, or empty for a new comment)
    /// \param placeholder Hint shown in the empty text box ("Add a comment")
    /// \param parent Parent widget
    explicit PDFFireCommentCard(const QString& author,
                                const QString& label,
                                const QString& text,
                                const QString& placeholder,
                                QWidget* parent);

    /// Shows the card as a child of \p host, 12 pixels to the right of \p anchor (host coordinates),
    /// moved inside the host, if it would not fit. The text box gets the focus.
    void showAt(QWidget* host, const QPoint& anchor);

    /// Makes the card a part of a layout (the comments panel): no fixed width, no shadow
    void setEmbedded();

    /// Sets the text of the Post button (for example "Save" when a comment is edited)
    void setPostButtonText(const QString& text);

    /// Text written by the user
    QString getText() const;

    /// Posts the text (as the Post button does). An empty text is not posted.
    void post();

    /// Cancels the card (as the Cancel button does)
    void cancel();

    /// Returns the text box (for tests)
    QPlainTextEdit* getEditor() const { return m_editor; }

signals:
    /// The user posted the text. The card is closed and deleted later.
    void posted(QString text);

    /// The user cancelled the card. The card is closed and deleted later.
    void cancelled();

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

    // The mouse events on the card must not get to the page view under it (a click
    // on the margin of the card would place another note)
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;

private:
    void updateStyle();
    void updateButtons();
    void close(bool isPosted);

    QLabel* m_authorLabel;
    QLabel* m_kindLabel;
    QPlainTextEdit* m_editor;
    QPushButton* m_cancelButton;
    QPushButton* m_postButton;
    bool m_isClosed = false;
    bool m_isUpdatingStyle = false;
};

}   // namespace pdf

#endif // PDFFIRECOMMENTCARD_H
