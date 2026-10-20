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

#ifndef PDFFIRECOMMENTSPANEL_H
#define PDFFIRECOMMENTSPANEL_H

#include "pdfviewerglobal.h"
#include "pdfannotation.h"
#include "pdfobject.h"

#include <QWidget>
#include <QPointer>
#include <QDateTime>

#include <vector>
#include <set>

class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;
class QPlainTextEdit;

namespace pdf
{
class PDFDocument;
class PDFDrawWidgetProxy;
class PDFWidgetAnnotationManager;
}

namespace pdfviewer
{

/// PDF Fire: one comment of the comments panel - a comment on a page with its thread of replies
struct PDFFireCommentItem
{
    pdf::PDFObjectReference reference;
    pdf::PDFInteger pageIndex = -1;
    pdf::AnnotationType type = pdf::AnnotationType::Invalid;
    QString author;
    QString text;
    QString typeName;
    QDateTime dateTime;
    QColor color;
    QString state;      ///< Review status (Accepted, Rejected, Cancelled, Completed), empty = none
    bool isLocked = false;
    std::vector<PDFFireCommentItem> replies; ///< Replies of the thread in the order of their dates (without status replies)
};

/// PDF Fire: builds the threads of the comments of a document (independent of the user interface, so it can be tested)
class PDF4QTLIBGUILIBSHARED_EXPORT PDFFireCommentsModel
{
public:
    /// Builds the list of the comments of the document (sorted by pages, then by position on the page)
    static std::vector<PDFFireCommentItem> build(const pdf::PDFDocument* document);

    /// Number of the comments and replies (status replies are not counted)
    static int count(const std::vector<PDFFireCommentItem>& items);

    /// Returns true, if the comment or one of its replies contains the text (case insensitive)
    static bool matches(const PDFFireCommentItem& item, const QString& text);

    /// Name of the kind of the comment ("Sticky note", "Highlight"...)
    static QString getTypeName(pdf::AnnotationType type);
};

/// PDF Fire: the comments panel of the sidebar - cards of the comments grouped by pages, with the author,
/// the date, the text, the threaded replies, a reply box, the review status and a menu (Edit, Reply,
/// Set Status, Copy Text, Delete). It replaces the flat tree of the notes.
///
/// Layout ported from PdfCraft crates/ui-egui/src/comments_panel.rs (MIT OR Apache-2.0,
/// Copyright (c) 2026 ArtCraft Team and the PdfCraft contributors).
class PDF4QTLIBGUILIBSHARED_EXPORT PDFFireCommentsPanel : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFFireCommentsPanel(pdf::PDFDrawWidgetProxy* proxy, QWidget* parent);
    virtual ~PDFFireCommentsPanel() override;

    /// Sets the document - the panel is rebuilt (later, in the event loop, so a card,
    /// from which the document was modified, is not deleted during its own event)
    void setDocument(const pdf::PDFDocument* document);

    /// Number of comments and replies
    int getCommentCount() const { return PDFFireCommentsModel::count(m_items); }

    /// Selects the comment (the card gets the accent bar). The page is shown, if \p navigate is true.
    void selectComment(pdf::PDFObjectReference reference, bool navigate);

    /// Selected comment
    pdf::PDFObjectReference getSelectedComment() const { return m_selected; }

    /// Rebuilds the panel at once (for tests)
    void rebuildNow();

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void scheduleRebuild();
    void rebuildCards();
    void updateHeader();
    void onAnnotationSelectionChanged();
    void showCommentMenu(const PDFFireCommentItem& item, QWidget* button);
    void startInlineEdit(pdf::PDFObjectReference reference);
    void postReply(pdf::PDFObjectReference reference, QPlainTextEdit* editor);
    void togglePage(pdf::PDFInteger pageIndex);
    const PDFFireCommentItem* findItem(pdf::PDFObjectReference reference, bool* isReply = nullptr) const;

    QWidget* createCard(const PDFFireCommentItem& item);
    QWidget* createReplyRow(const PDFFireCommentItem& reply, QWidget* parent);
    QLabel* createAvatar(const QString& author, const QColor& color, int size, QWidget* parent) const;
    QString getCardStyleSheet(bool isSelected) const;
    bool isDarkTheme() const;

    pdf::PDFWidgetAnnotationManager* getAnnotationManager() const;

    pdf::PDFDrawWidgetProxy* m_proxy;
    const pdf::PDFDocument* m_document = nullptr;
    std::vector<PDFFireCommentItem> m_items;
    pdf::PDFObjectReference m_selected;
    pdf::PDFObjectReference m_editedComment;
    std::set<pdf::PDFInteger> m_collapsedPages;
    bool m_isRebuildScheduled = false;
    bool m_isSelecting = false;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_countLabel = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QPointer<QWidget> m_content;
    std::vector<std::pair<pdf::PDFObjectReference, QPointer<QWidget>>> m_cards;
};

}   // namespace pdfviewer

#endif // PDFFIRECOMMENTSPANEL_H
