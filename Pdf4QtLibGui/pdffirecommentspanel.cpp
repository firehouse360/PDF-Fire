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

#include "pdffirecommentspanel.h"
#include "pdffiretheme.h"

#include "pdfdocument.h"
#include "pdfannotationmanipulator.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfwidgetannotation.h"
#include "pdffirecommentcard.h"
#include "pdfutils.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QRegularExpression>

#include <map>
#include <array>
#include <functional>
#include <algorithm>

#include "pdfdbgheap.h"

namespace pdfviewer
{

// Ported from PdfCraft crates/ui-egui/src/comments_panel.rs (MIT OR Apache-2.0)

namespace
{

constexpr const char* PROPERTY_REFERENCE_NUMBER = "pdfFireCommentObject";
constexpr const char* PROPERTY_REFERENCE_GENERATION = "pdfFireCommentGeneration";
constexpr const char* PROPERTY_PAGE = "pdfFireCommentPage";

void setReferenceProperty(QObject* object, pdf::PDFObjectReference reference)
{
    object->setProperty(PROPERTY_REFERENCE_NUMBER, QVariant::fromValue<qint64>(reference.objectNumber));
    object->setProperty(PROPERTY_REFERENCE_GENERATION, QVariant::fromValue<qint64>(reference.generation));
}

pdf::PDFObjectReference getReferenceProperty(const QObject* object)
{
    const QVariant number = object->property(PROPERTY_REFERENCE_NUMBER);
    if (!number.isValid())
    {
        return pdf::PDFObjectReference();
    }

    return pdf::PDFObjectReference(number.toLongLong(), object->property(PROPERTY_REFERENCE_GENERATION).toLongLong());
}

QString getInitials(const QString& author)
{
    const QStringList parts = author.split(QRegularExpression("[\\s._-]+"), Qt::SkipEmptyParts);
    QString initials;
    for (const QString& part : parts)
    {
        if (initials.size() == 2)
        {
            break;
        }
        initials += part.front().toUpper();
    }

    return initials.isEmpty() ? QString("?") : initials;
}

QColor getStatusColor(const QString& state)
{
    if (state == "Accepted" || state == "Completed")
    {
        return QColor("#2D9D5B");
    }
    if (state == "Rejected")
    {
        return QColor("#D32F2F");
    }
    return QColor("#8E8E8E");
}

QString getStatusText(const QString& state)
{
    if (state == "Accepted")
    {
        return PDFFireCommentsPanel::tr("Accepted");
    }
    if (state == "Rejected")
    {
        return PDFFireCommentsPanel::tr("Rejected");
    }
    if (state == "Cancelled")
    {
        return PDFFireCommentsPanel::tr("Cancelled");
    }
    if (state == "Completed")
    {
        return PDFFireCommentsPanel::tr("Completed");
    }
    return state;
}

QString getStatusSymbol(const QString& state)
{
    if (state == "Accepted")
    {
        return QString::fromUtf16(u"✔");   // check mark
    }
    if (state == "Rejected")
    {
        return QString::fromUtf16(u"✖");   // heavy multiplication x
    }
    if (state == "Completed")
    {
        return QString::fromUtf16(u"✔");
    }
    return QString::fromUtf16(u"○");       // circle
}

/// The card of a comment - a click selects the comment
class CommentCardFrame : public QFrame
{
public:
    explicit CommentCardFrame(std::function<void(void)> clicked, QWidget* parent) :
        QFrame(parent),
        m_clicked(std::move(clicked))
    {
        setCursor(Qt::PointingHandCursor);
    }

protected:
    virtual void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_clicked)
        {
            event->accept();
            // The panel can be rebuilt by the selection - not during the event of this card
            QTimer::singleShot(0, this, m_clicked);
            return;
        }
        QFrame::mousePressEvent(event);
    }

private:
    std::function<void(void)> m_clicked;
};

}   // namespace

std::vector<PDFFireCommentItem> PDFFireCommentsModel::build(const pdf::PDFDocument* document)
{
    std::vector<PDFFireCommentItem> result;
    if (!document)
    {
        return result;
    }

    const pdf::PDFObjectStorage* storage = &document->getStorage();
    const size_t pageCount = document->getCatalog()->getPageCount();
    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        const pdf::PDFPage* page = document->getCatalog()->getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        std::map<pdf::PDFObjectReference, pdf::PDFAnnotationPtr> annotations;
        std::vector<pdf::PDFObjectReference> order;
        for (const pdf::PDFObjectReference& reference : page->getAnnotations())
        {
            if (annotations.count(reference))
            {
                continue;
            }

            pdf::PDFAnnotationPtr annotation = pdf::PDFAnnotation::parse(storage, reference);
            if (annotation && annotation->asMarkupAnnotation())
            {
                annotations[reference] = annotation;
                order.push_back(reference);
            }
        }

        auto createItem = [pageIndex](const pdf::PDFObjectReference& reference, const pdf::PDFAnnotation* annotation)
        {
            const pdf::PDFMarkupAnnotation* markupAnnotation = annotation->asMarkupAnnotation();
            PDFFireCommentItem item;
            item.reference = reference;
            item.pageIndex = pdf::PDFInteger(pageIndex);
            item.type = annotation->getType();
            item.author = markupAnnotation->getWindowTitle();
            item.text = markupAnnotation->getContents();
            item.typeName = getTypeName(annotation->getType());
            item.dateTime = annotation->getLastModifiedDateTime().isValid() ? annotation->getLastModifiedDateTime() : markupAnnotation->getCreationDate();
            item.isLocked = annotation->getFlags().testFlag(pdf::PDFAnnotation::Locked);
            if (annotation->getType() == pdf::AnnotationType::Text && annotation->getColor().empty())
            {
                item.color = pdf::PDFAnnotationManipulator::getDefaultStickyNoteColor();
            }
            else
            {
                item.color = pdf::PDFAnnotation::getDrawColorFromAnnotationColor(annotation->getColor(), 1.0);
            }
            return item;
        };

        // The root of the thread of a reply (a reply can reply to a reply)
        auto findRoot = [&annotations](pdf::PDFObjectReference reference)
        {
            std::set<pdf::PDFObjectReference> visited;
            while (visited.insert(reference).second)
            {
                auto it = annotations.find(reference);
                if (it == annotations.cend())
                {
                    return pdf::PDFObjectReference();
                }

                const pdf::PDFMarkupAnnotation* markupAnnotation = it->second->asMarkupAnnotation();
                if (!markupAnnotation->isReplyTo())
                {
                    return reference;
                }
                reference = markupAnnotation->getInReplyTo();
            }
            return pdf::PDFObjectReference();
        };

        std::vector<PDFFireCommentItem> pageItems;
        std::map<pdf::PDFObjectReference, size_t> rootIndices;
        std::map<pdf::PDFObjectReference, QRectF> rootRectangles;
        for (const pdf::PDFObjectReference& reference : order)
        {
            const pdf::PDFAnnotation* annotation = annotations[reference].data();
            if (!annotation->isReplyTo())
            {
                rootIndices[reference] = pageItems.size();
                rootRectangles[reference] = annotation->getRectangle().normalized();
                pageItems.push_back(createItem(reference, annotation));
            }
        }

        std::map<pdf::PDFObjectReference, std::vector<const pdf::PDFAnnotation*>> statusReplies;
        for (const pdf::PDFObjectReference& reference : order)
        {
            const pdf::PDFAnnotation* annotation = annotations[reference].data();
            if (!annotation->isReplyTo())
            {
                continue;
            }

            const pdf::PDFObjectReference root = findRoot(reference);
            auto it = rootIndices.find(root);
            if (it == rootIndices.cend())
            {
                continue;
            }

            if (pdf::PDFAnnotationManipulator::isStatusReply(annotation))
            {
                // Only the status of the comment itself is shown (not the status of its replies)
                if (annotation->asMarkupAnnotation()->getInReplyTo() == root)
                {
                    statusReplies[root].push_back(annotation);
                }
                continue;
            }

            pageItems[it->second].replies.push_back(createItem(reference, annotation));
        }

        for (PDFFireCommentItem& item : pageItems)
        {
            item.state = pdf::PDFAnnotationManipulator::getReviewState(statusReplies[item.reference]);
            std::stable_sort(item.replies.begin(), item.replies.end(), [](const PDFFireCommentItem& l, const PDFFireCommentItem& r)
            {
                return l.dateTime.isValid() && r.dateTime.isValid() && l.dateTime < r.dateTime;
            });
        }

        // Top of the page first, then from the left (page coordinates have the y axis upwards)
        std::stable_sort(pageItems.begin(), pageItems.end(), [&rootRectangles](const PDFFireCommentItem& l, const PDFFireCommentItem& r)
        {
            const QRectF& lr = rootRectangles[l.reference];
            const QRectF& rr = rootRectangles[r.reference];
            if (!qFuzzyCompare(lr.bottom(), rr.bottom()))
            {
                return lr.bottom() > rr.bottom();
            }
            return lr.left() < rr.left();
        });

        result.insert(result.end(), pageItems.begin(), pageItems.end());
    }

    return result;
}

int PDFFireCommentsModel::count(const std::vector<PDFFireCommentItem>& items)
{
    int count = 0;
    for (const PDFFireCommentItem& item : items)
    {
        count += 1 + int(item.replies.size());
    }
    return count;
}

bool PDFFireCommentsModel::matches(const PDFFireCommentItem& item, const QString& text)
{
    const QString trimmedText = text.trimmed();
    if (trimmedText.isEmpty())
    {
        return true;
    }

    auto contains = [&trimmedText](const QString& string) { return string.contains(trimmedText, Qt::CaseInsensitive); };
    if (contains(item.author) || contains(item.text) || contains(item.typeName) || contains(item.state))
    {
        return true;
    }

    for (const PDFFireCommentItem& reply : item.replies)
    {
        if (contains(reply.author) || contains(reply.text))
        {
            return true;
        }
    }

    return false;
}

QString PDFFireCommentsModel::getTypeName(pdf::AnnotationType type)
{
    switch (type)
    {
        case pdf::AnnotationType::Text:
            return PDFFireCommentsPanel::tr("Sticky note");
        case pdf::AnnotationType::FreeText:
            return PDFFireCommentsPanel::tr("Text box");
        case pdf::AnnotationType::Line:
            return PDFFireCommentsPanel::tr("Line");
        case pdf::AnnotationType::Square:
            return PDFFireCommentsPanel::tr("Rectangle");
        case pdf::AnnotationType::Circle:
            return PDFFireCommentsPanel::tr("Oval");
        case pdf::AnnotationType::Polygon:
            return PDFFireCommentsPanel::tr("Polygon");
        case pdf::AnnotationType::Polyline:
            return PDFFireCommentsPanel::tr("Polyline");
        case pdf::AnnotationType::Highlight:
            return PDFFireCommentsPanel::tr("Highlight");
        case pdf::AnnotationType::Underline:
            return PDFFireCommentsPanel::tr("Underline");
        case pdf::AnnotationType::Squiggly:
            return PDFFireCommentsPanel::tr("Squiggly underline");
        case pdf::AnnotationType::StrikeOut:
            return PDFFireCommentsPanel::tr("Strikethrough");
        case pdf::AnnotationType::Stamp:
            return PDFFireCommentsPanel::tr("Stamp");
        case pdf::AnnotationType::Caret:
            return PDFFireCommentsPanel::tr("Inserted text");
        case pdf::AnnotationType::Ink:
            return PDFFireCommentsPanel::tr("Drawing");
        case pdf::AnnotationType::FileAttachment:
            return PDFFireCommentsPanel::tr("File attachment");
        case pdf::AnnotationType::Sound:
            return PDFFireCommentsPanel::tr("Sound");
        case pdf::AnnotationType::Redact:
            return PDFFireCommentsPanel::tr("Redaction");
        default:
            break;
    }

    return PDFFireCommentsPanel::tr("Comment");
}

PDFFireCommentsPanel::PDFFireCommentsPanel(pdf::PDFDrawWidgetProxy* proxy, QWidget* parent) :
    BaseClass(parent),
    m_proxy(proxy)
{
    setObjectName("pdfFireCommentsPanel");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header: "Comments N" and the search box
    QWidget* header = new QWidget(this);
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(10, 10, 10, 8);
    headerLayout->setSpacing(8);

    QHBoxLayout* titleLayout = new QHBoxLayout();
    titleLayout->setSpacing(6);
    m_titleLabel = new QLabel(tr("Comments"), header);
    m_titleLabel->setObjectName("pdfFireCommentsTitle");
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.1);
    m_titleLabel->setFont(titleFont);
    m_countLabel = new QLabel(header);
    m_countLabel->setObjectName("pdfFireCommentsCount");
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addWidget(m_countLabel);
    titleLayout->addStretch(1);
    headerLayout->addLayout(titleLayout);

    m_searchEdit = new QLineEdit(header);
    m_searchEdit->setObjectName("pdfFireCommentsSearch");
    m_searchEdit->setPlaceholderText(tr("Search comments"));
    m_searchEdit->setClearButtonEnabled(true);
    headerLayout->addWidget(m_searchEdit);
    layout->addWidget(header);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName("pdfFireCommentsScrollArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_scrollArea, 1);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &PDFFireCommentsPanel::rebuildCards);

    if (pdf::PDFWidgetAnnotationManager* annotationManager = getAnnotationManager())
    {
        connect(annotationManager, &pdf::PDFWidgetAnnotationManager::selectionChanged, this, &PDFFireCommentsPanel::onAnnotationSelectionChanged);
    }

    updateHeader();
    rebuildCards();
}

PDFFireCommentsPanel::~PDFFireCommentsPanel()
{

}

pdf::PDFWidgetAnnotationManager* PDFFireCommentsPanel::getAnnotationManager() const
{
    return m_proxy ? m_proxy->getAnnotationManager() : nullptr;
}

void PDFFireCommentsPanel::setDocument(const pdf::PDFDocument* document)
{
    m_document = document;

    // The annotation manager can be created later, than the panel
    if (pdf::PDFWidgetAnnotationManager* annotationManager = getAnnotationManager())
    {
        connect(annotationManager, &pdf::PDFWidgetAnnotationManager::selectionChanged, this, &PDFFireCommentsPanel::onAnnotationSelectionChanged, Qt::UniqueConnection);
    }

    scheduleRebuild();
}

void PDFFireCommentsPanel::scheduleRebuild()
{
    if (m_isRebuildScheduled)
    {
        return;
    }

    m_isRebuildScheduled = true;
    QTimer::singleShot(0, this, &PDFFireCommentsPanel::rebuildNow);
}

void PDFFireCommentsPanel::rebuildNow()
{
    m_isRebuildScheduled = false;
    m_items = PDFFireCommentsModel::build(m_document);
    m_editedComment = pdf::PDFObjectReference();

    if (m_selected.isValid() && !findItem(m_selected))
    {
        m_selected = pdf::PDFObjectReference();
    }

    updateHeader();
    rebuildCards();
}

void PDFFireCommentsPanel::updateHeader()
{
    m_countLabel->setText(QString::number(getCommentCount()));
}

bool PDFFireCommentsPanel::isDarkTheme() const
{
    return palette().color(QPalette::Window).lightness() < 128;
}

const PDFFireCommentItem* PDFFireCommentsPanel::findItem(pdf::PDFObjectReference reference, bool* isReply) const
{
    for (const PDFFireCommentItem& item : m_items)
    {
        if (item.reference == reference)
        {
            if (isReply)
            {
                *isReply = false;
            }
            return &item;
        }

        for (const PDFFireCommentItem& reply : item.replies)
        {
            if (reply.reference == reference)
            {
                if (isReply)
                {
                    *isReply = true;
                }
                return &reply;
            }
        }
    }

    return nullptr;
}

QString PDFFireCommentsPanel::getCardStyleSheet(bool isSelected) const
{
    const QPalette palette = this->palette();
    QColor hover = palette.color(QPalette::WindowText);
    hover.setAlphaF(isDarkTheme() ? 0.06f : 0.04f);
    const QString accent = PDFFireTheme::getAccentColor().name();
    const QString divider = palette.color(QPalette::Mid).name();

    if (isSelected)
    {
        return QString("QFrame#pdfFireCommentCard { background: rgba(%1, %2, %3, %4); border: none; border-left: 4px solid %5; border-bottom: 1px solid %6; }")
                .arg(hover.red()).arg(hover.green()).arg(hover.blue()).arg(hover.alphaF() * 2.0, 0, 'f', 2).arg(accent, divider);
    }

    return QString("QFrame#pdfFireCommentCard { background: transparent; border: none; border-left: 4px solid transparent; border-bottom: 1px solid %1; }"
                   "QFrame#pdfFireCommentCard:hover { background: rgba(%2, %3, %4, %5); }")
            .arg(divider).arg(hover.red()).arg(hover.green()).arg(hover.blue()).arg(hover.alphaF(), 0, 'f', 2);
}

QLabel* PDFFireCommentsPanel::createAvatar(const QString& author, const QColor& color, int size, QWidget* parent) const
{
    // A round avatar with the initials of the author, in the colour of the comment. A light colour is
    // darkened in the light theme, so the avatar is visible on the white panel.
    QColor background = color.isValid() ? color : palette().color(QPalette::Mid);
    if (!isDarkTheme() && background.lightnessF() > 0.8)
    {
        background = background.darker(125);
    }

    const qreal dpr = devicePixelRatioF();
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawEllipse(QRectF(0.5, 0.5, size - 1.0, size - 1.0));

    const bool isLight = (0.299 * background.redF() + 0.587 * background.greenF() + 0.114 * background.blueF()) > 0.6;
    painter.setPen(isLight ? QColor(30, 30, 30) : QColor(Qt::white));
    QFont font = this->font();
    font.setBold(true);
    font.setPixelSize(qMax(8, int(size * 0.4)));
    painter.setFont(font);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, getInitials(author));
    painter.end();

    QLabel* label = new QLabel(parent);
    label->setPixmap(pixmap);
    label->setFixedSize(size, size);
    label->setToolTip(author);
    return label;
}

QWidget* PDFFireCommentsPanel::createReplyRow(const PDFFireCommentItem& reply, QWidget* parent)
{
    QWidget* row = new QWidget(parent);
    row->setObjectName("pdfFireCommentReply");
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    // The thread line on the left
    QFrame* line = new QFrame(row);
    line->setFixedWidth(2);
    line->setStyleSheet(QString("background: %1; border: none;").arg(palette().color(QPalette::Mid).name()));
    rowLayout->addWidget(line);

    QWidget* body = new QWidget(row);
    QVBoxLayout* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 2, 0, 2);
    bodyLayout->setSpacing(2);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(6);
    headerLayout->addWidget(createAvatar(reply.author, QColor("#8E8E8E"), 20, body));
    QLabel* authorLabel = new QLabel(reply.author.isEmpty() ? tr("Unknown") : reply.author, body);
    authorLabel->setTextFormat(Qt::PlainText);
    QFont authorFont = authorLabel->font();
    authorFont.setBold(true);
    authorLabel->setFont(authorFont);
    headerLayout->addWidget(authorLabel);
    if (reply.dateTime.isValid())
    {
        QLabel* dateLabel = new QLabel(reply.dateTime.toLocalTime().toString("yyyy-MM-dd HH:mm"), body);
        dateLabel->setStyleSheet(QString("color: %1;").arg(palette().color(QPalette::PlaceholderText).name()));
        headerLayout->addWidget(dateLabel);
    }
    headerLayout->addStretch(1);

    QToolButton* menuButton = new QToolButton(body);
    menuButton->setObjectName("pdfFireCommentMenuButton");
    menuButton->setText(QString::fromUtf16(u"…"));
    menuButton->setAutoRaise(true);
    menuButton->setToolTip(tr("More"));
    const pdf::PDFObjectReference reference = reply.reference;
    connect(menuButton, &QToolButton::clicked, this, [this, reference, menuButton]()
    {
        if (const PDFFireCommentItem* item = findItem(reference))
        {
            showCommentMenu(*item, menuButton);
        }
    });
    headerLayout->addWidget(menuButton);
    bodyLayout->addLayout(headerLayout);

    if (m_editedComment == reply.reference)
    {
        pdf::PDFFireCommentCard* editor = new pdf::PDFFireCommentCard(reply.author, tr("Edit reply"), reply.text, tr("Add a reply"), body);
        editor->setEmbedded();
        editor->setPostButtonText(tr("Save"));
        connect(editor, &pdf::PDFFireCommentCard::posted, this, [this, reference](QString text)
        {
            m_editedComment = pdf::PDFObjectReference();
            if (pdf::PDFWidgetAnnotationManager* annotationManager = getAnnotationManager())
            {
                annotationManager->setAnnotationContents(reference, text);
            }
            scheduleRebuild();
        });
        connect(editor, &pdf::PDFFireCommentCard::cancelled, this, [this]() { m_editedComment = pdf::PDFObjectReference(); scheduleRebuild(); });
        bodyLayout->addWidget(editor);
        QTimer::singleShot(0, editor->getEditor(), [editor]() { editor->getEditor()->setFocus(); });
    }
    else
    {
        QLabel* textLabel = new QLabel(reply.text, body);
        textLabel->setTextFormat(Qt::PlainText);
        textLabel->setWordWrap(true);
        bodyLayout->addWidget(textLabel);
    }

    rowLayout->addWidget(body, 1);
    return row;
}

QWidget* PDFFireCommentsPanel::createCard(const PDFFireCommentItem& item)
{
    const pdf::PDFObjectReference reference = item.reference;
    const bool isSelected = m_selected == reference;
    const QColor mutedColor = palette().color(QPalette::PlaceholderText);
    pdf::PDFWidgetAnnotationManager* annotationManager = getAnnotationManager();
    const bool canModify = annotationManager && annotationManager->isModificationAllowed();

    CommentCardFrame* card = new CommentCardFrame([this, reference]() { selectComment(reference, true); }, m_content);
    card->setObjectName("pdfFireCommentCard");
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setStyleSheet(getCardStyleSheet(isSelected));
    setReferenceProperty(card, reference);

    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(8, 10, 8, 10);
    cardLayout->setSpacing(6);

    // Header: avatar, author, date, status, menu
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(8);
    headerLayout->addWidget(createAvatar(item.author, item.color, 26, card));

    QVBoxLayout* nameLayout = new QVBoxLayout();
    nameLayout->setSpacing(0);
    QLabel* authorLabel = new QLabel(item.author.isEmpty() ? tr("Unknown") : item.author, card);
    authorLabel->setObjectName("pdfFireCommentAuthor");
    authorLabel->setTextFormat(Qt::PlainText);
    QFont authorFont = authorLabel->font();
    authorFont.setBold(true);
    authorLabel->setFont(authorFont);
    nameLayout->addWidget(authorLabel);

    QString subtitle = item.typeName;
    if (item.dateTime.isValid())
    {
        subtitle = QString("%1 · %2").arg(item.dateTime.toLocalTime().toString("yyyy-MM-dd HH:mm"), item.typeName);
    }
    QLabel* dateLabel = new QLabel(subtitle, card);
    dateLabel->setObjectName("pdfFireCommentDate");
    dateLabel->setTextFormat(Qt::PlainText);
    dateLabel->setStyleSheet(QString("color: %1;").arg(mutedColor.name()));
    QFont dateFont = dateLabel->font();
    dateFont.setPointSizeF(dateFont.pointSizeF() * 0.9);
    dateLabel->setFont(dateFont);
    nameLayout->addWidget(dateLabel);
    headerLayout->addLayout(nameLayout, 1);

    if (item.isLocked)
    {
        QLabel* lockLabel = new QLabel(QString::fromUtf16(u"\U0001F512"), card);
        lockLabel->setToolTip(tr("Locked"));
        headerLayout->addWidget(lockLabel);
    }

    QToolButton* menuButton = new QToolButton(card);
    menuButton->setObjectName("pdfFireCommentMenuButton");
    menuButton->setText(QString::fromUtf16(u"…"));
    menuButton->setAutoRaise(true);
    menuButton->setToolTip(tr("More"));
    connect(menuButton, &QToolButton::clicked, this, [this, reference, menuButton]()
    {
        if (const PDFFireCommentItem* currentItem = findItem(reference))
        {
            showCommentMenu(*currentItem, menuButton);
        }
    });
    headerLayout->addWidget(menuButton, 0, Qt::AlignTop);
    cardLayout->addLayout(headerLayout);

    // Body (indented under the author)
    QWidget* body = new QWidget(card);
    QVBoxLayout* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(34, 0, 0, 0);
    bodyLayout->setSpacing(6);

    if (m_editedComment == reference)
    {
        pdf::PDFFireCommentCard* editor = new pdf::PDFFireCommentCard(item.author, tr("Edit comment"), item.text, tr("Add a comment"), body);
        editor->setEmbedded();
        editor->setPostButtonText(tr("Save"));
        connect(editor, &pdf::PDFFireCommentCard::posted, this, [this, reference](QString text)
        {
            m_editedComment = pdf::PDFObjectReference();
            if (pdf::PDFWidgetAnnotationManager* manager = getAnnotationManager())
            {
                manager->setAnnotationContents(reference, text);
            }
            scheduleRebuild();
        });
        connect(editor, &pdf::PDFFireCommentCard::cancelled, this, [this]() { m_editedComment = pdf::PDFObjectReference(); scheduleRebuild(); });
        bodyLayout->addWidget(editor);
        QTimer::singleShot(0, editor->getEditor(), [editor]() { editor->getEditor()->setFocus(); });
    }
    else
    {
        QLabel* textLabel = new QLabel(card);
        textLabel->setObjectName("pdfFireCommentText");
        textLabel->setTextFormat(Qt::PlainText);
        textLabel->setWordWrap(true);
        if (item.text.trimmed().isEmpty())
        {
            textLabel->setText(item.typeName);
            QFont font = textLabel->font();
            font.setItalic(true);
            textLabel->setFont(font);
            textLabel->setStyleSheet(QString("color: %1;").arg(mutedColor.name()));
        }
        else
        {
            QString text = item.text.trimmed();
            if (!isSelected)
            {
                // Two lines at most, until the card is selected
                const QStringList lines = text.split('\n');
                const int maximalLength = 140;
                QString shortText = lines.mid(0, 2).join('\n');
                if (shortText.size() > maximalLength)
                {
                    shortText = shortText.left(maximalLength).trimmed();
                }
                if (shortText != text)
                {
                    text = shortText + QChar(0x2026);
                }
            }
            textLabel->setText(text);
        }
        bodyLayout->addWidget(textLabel);
    }

    if (!item.state.isEmpty())
    {
        const QColor statusColor = getStatusColor(item.state);
        QLabel* statusLabel = new QLabel(QString("%1 %2").arg(getStatusSymbol(item.state), getStatusText(item.state)), card);
        statusLabel->setObjectName("pdfFireCommentStatus");
        statusLabel->setStyleSheet(QString("color: %1; font-weight: 600;").arg(statusColor.name()));
        bodyLayout->addWidget(statusLabel);
    }

    for (const PDFFireCommentItem& reply : item.replies)
    {
        bodyLayout->addWidget(createReplyRow(reply, body));
    }

    if (isSelected && canModify)
    {
        // Reply box: Enter posts, Shift+Enter makes a new line
        QPlainTextEdit* replyEdit = new QPlainTextEdit(body);
        replyEdit->setObjectName("pdfFireCommentReplyEdit");
        replyEdit->setPlaceholderText(tr("Add a reply"));
        replyEdit->setTabChangesFocus(true);
        replyEdit->setFixedHeight(replyEdit->fontMetrics().lineSpacing() * 2 + 14);
        setReferenceProperty(replyEdit, reference);
        replyEdit->installEventFilter(this);
        bodyLayout->addWidget(replyEdit);

        QHBoxLayout* replyButtonLayout = new QHBoxLayout();
        replyButtonLayout->addStretch(1);
        QPushButton* postButton = new QPushButton(tr("Post"), body);
        postButton->setObjectName("pdfFireCommentReplyPost");
        postButton->setCursor(Qt::PointingHandCursor);
        postButton->setEnabled(false);
        postButton->setStyleSheet(QString("QPushButton { background: %1; color: white; border: none; border-radius: 12px; padding: 4px 14px; font-weight: bold; }"
                                          "QPushButton:disabled { background: rgba(242, 86, 29, 0.35); color: rgba(255, 255, 255, 0.7); }").arg(PDFFireTheme::getAccentColor().name()));
        connect(replyEdit, &QPlainTextEdit::textChanged, postButton, [replyEdit, postButton]() { postButton->setEnabled(!replyEdit->toPlainText().trimmed().isEmpty()); });
        connect(postButton, &QPushButton::clicked, this, [this, reference, replyEdit]() { postReply(reference, replyEdit); });
        replyButtonLayout->addWidget(postButton);
        bodyLayout->addLayout(replyButtonLayout);
    }

    cardLayout->addWidget(body);
    return card;
}

void PDFFireCommentsPanel::rebuildCards()
{
    const int scrollPosition = m_scrollArea->verticalScrollBar()->value();

    if (m_content)
    {
        // Deleted later - this function can be called from an event of a widget of the old content
        m_content->hide();
        m_content->deleteLater();
    }
    m_cards.clear();

    m_content = new QWidget();
    m_content->setObjectName("pdfFireCommentsContent");
    QVBoxLayout* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const QString searchText = m_searchEdit->text();
    const QColor mutedColor = palette().color(QPalette::PlaceholderText);

    int shownCount = 0;
    pdf::PDFInteger currentPage = -1;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const PDFFireCommentItem& item = m_items[i];
        if (!PDFFireCommentsModel::matches(item, searchText))
        {
            continue;
        }

        if (item.pageIndex != currentPage)
        {
            currentPage = item.pageIndex;

            int pageCount = 0;
            for (const PDFFireCommentItem& pageItem : m_items)
            {
                if (pageItem.pageIndex == currentPage && PDFFireCommentsModel::matches(pageItem, searchText))
                {
                    pageCount += 1 + int(pageItem.replies.size());
                }
            }

            const bool isCollapsed = m_collapsedPages.count(currentPage);
            QPushButton* pageButton = new QPushButton(m_content);
            pageButton->setObjectName("pdfFireCommentsPageHeader");
            pageButton->setFlat(true);
            pageButton->setCursor(Qt::PointingHandCursor);
            pageButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            pageButton->setText(QString("%1  %2   (%3)").arg(isCollapsed ? QString::fromUtf16(u"▸") : QString::fromUtf16(u"▾"), tr("Page %1").arg(currentPage + 1)).arg(pageCount));
            pageButton->setToolTip(isCollapsed ? tr("Show the comments of the page") : tr("Hide the comments of the page"));
            pageButton->setStyleSheet(QString("QPushButton { text-align: left; color: %1; font-weight: 600; padding: 8px 10px 4px 10px; border: none; background: transparent; }").arg(mutedColor.name()));
            const pdf::PDFInteger page = currentPage;
            connect(pageButton, &QToolButton::clicked, this, [this, page]() { togglePage(page); });
            layout->addWidget(pageButton);
        }

        if (m_collapsedPages.count(item.pageIndex))
        {
            continue;
        }

        QWidget* card = createCard(item);
        layout->addWidget(card);
        m_cards.emplace_back(item.reference, card);
        ++shownCount;
    }

    if (m_items.empty() || shownCount == 0)
    {
        QLabel* emptyLabel = new QLabel(m_content);
        emptyLabel->setObjectName("pdfFireCommentsEmpty");
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setWordWrap(true);
        if (m_items.empty())
        {
            emptyLabel->setText(QString("<p><b>%1</b></p><p style='color: %2;'>%3</p>").arg(tr("No comments yet."), mutedColor.name(),
                                                                                            tr("Add a sticky note from the Comment tab, or select text and highlight it.")));
        }
        else if (!m_collapsedPages.empty() && searchText.trimmed().isEmpty())
        {
            emptyLabel->clear();
        }
        else
        {
            emptyLabel->setText(QString("<p style='color: %1;'>%2</p>").arg(mutedColor.name(), tr("No comments match the search.")));
        }
        emptyLabel->setContentsMargins(16, 24, 16, 24);
        layout->addWidget(emptyLabel);
    }

    layout->addStretch(1);
    m_scrollArea->setWidget(m_content);
    m_scrollArea->verticalScrollBar()->setValue(scrollPosition);
}

void PDFFireCommentsPanel::togglePage(pdf::PDFInteger pageIndex)
{
    if (m_collapsedPages.count(pageIndex))
    {
        m_collapsedPages.erase(pageIndex);
    }
    else
    {
        m_collapsedPages.insert(pageIndex);
    }

    rebuildCards();
}

void PDFFireCommentsPanel::selectComment(pdf::PDFObjectReference reference, bool navigate)
{
    bool isReply = false;
    const PDFFireCommentItem* item = findItem(reference, &isReply);
    if (!item)
    {
        return;
    }

    // A reply selects the comment of its thread
    if (isReply)
    {
        for (const PDFFireCommentItem& rootItem : m_items)
        {
            for (const PDFFireCommentItem& reply : rootItem.replies)
            {
                if (reply.reference == reference)
                {
                    item = &rootItem;
                }
            }
        }
    }

    const bool isChanged = m_selected != item->reference;
    m_selected = item->reference;
    const pdf::PDFInteger pageIndex = item->pageIndex;
    const pdf::PDFObjectReference selectedReference = m_selected;

    if (navigate && m_proxy && m_document)
    {
        pdf::PDFTemporaryValueChange<bool> guard(&m_isSelecting, true);

        // Show the comment on its page and select it there
        QRectF rectangle;
        if (pdf::PDFAnnotationPtr annotation = pdf::PDFAnnotation::parse(&m_document->getStorage(), selectedReference))
        {
            rectangle = annotation->getRectangle().normalized();
            if (annotation->getType() == pdf::AnnotationType::Text)
            {
                rectangle.setSize(QSizeF(20.0, 20.0));
            }
        }

        if (rectangle.isValid())
        {
            m_proxy->goToPageAndEnsureVisible(pageIndex, rectangle.adjusted(-20.0, -20.0, 20.0, 20.0));
        }
        else
        {
            m_proxy->goToPage(pageIndex);
        }

        if (pdf::PDFWidgetAnnotationManager* annotationManager = getAnnotationManager())
        {
            annotationManager->setSelectedAnnotations({ selectedReference });
        }
    }

    if (isChanged)
    {
        rebuildCards();
    }

    // The selected card is scrolled into the view
    for (const auto& [cardReference, card] : m_cards)
    {
        if (cardReference == selectedReference && card)
        {
            QWidget* cardWidget = card.data();
            QTimer::singleShot(0, this, [this, cardWidget = QPointer<QWidget>(cardWidget)]()
            {
                if (cardWidget)
                {
                    m_scrollArea->ensureWidgetVisible(cardWidget, 0, 0);
                }
            });
            break;
        }
    }
}

void PDFFireCommentsPanel::onAnnotationSelectionChanged()
{
    pdf::PDFWidgetAnnotationManager* annotationManager = getAnnotationManager();
    if (m_isSelecting || !annotationManager)
    {
        return;
    }

    const std::vector<pdf::PDFObjectReference> selection = annotationManager->getSelectedAnnotations();
    if (selection.size() == 1 && findItem(selection.front()))
    {
        selectComment(selection.front(), false);
    }
}

void PDFFireCommentsPanel::showCommentMenu(const PDFFireCommentItem& item, QWidget* button)
{
    pdf::PDFWidgetAnnotationManager* annotationManager = getAnnotationManager();
    if (!annotationManager)
    {
        return;
    }

    const pdf::PDFObjectReference reference = item.reference;
    const QString text = item.text;
    bool isReply = false;
    findItem(reference, &isReply);
    const bool canModify = annotationManager->canModifyComment(reference);
    const bool canAdd = annotationManager->isModificationAllowed();

    QMenu menu(this);

    QAction* editAction = menu.addAction(tr("Edit"));
    editAction->setEnabled(canModify);
    connect(editAction, &QAction::triggered, this, [this, reference]() { startInlineEdit(reference); });

    QAction* replyAction = menu.addAction(tr("Reply"));
    replyAction->setEnabled(canAdd);
    connect(replyAction, &QAction::triggered, this, [this, reference]()
    {
        selectComment(reference, false);
        // Focus the reply box of the (rebuilt) selected card
        QTimer::singleShot(0, this, [this]()
        {
            if (m_content)
            {
                if (QPlainTextEdit* replyEdit = m_content->findChild<QPlainTextEdit*>("pdfFireCommentReplyEdit"))
                {
                    m_scrollArea->ensureWidgetVisible(replyEdit);
                    replyEdit->setFocus();
                }
            }
        });
    });

    if (!isReply)
    {
        QMenu* statusMenu = menu.addMenu(tr("Set Status"));
        statusMenu->setEnabled(canAdd);
        const std::array<std::pair<QString, QString>, 5> states = { std::make_pair(tr("None"), QString("None")),
                                                                    std::make_pair(tr("Accepted"), QString("Accepted")),
                                                                    std::make_pair(tr("Rejected"), QString("Rejected")),
                                                                    std::make_pair(tr("Cancelled"), QString("Cancelled")),
                                                                    std::make_pair(tr("Completed"), QString("Completed")) };
        for (const auto& [stateText, state] : states)
        {
            const QString currentState = state;
            QAction* stateAction = statusMenu->addAction(stateText);
            stateAction->setCheckable(true);
            stateAction->setChecked(item.state == state || (item.state.isEmpty() && state == "None"));
            connect(stateAction, &QAction::triggered, this, [this, reference, currentState]()
            {
                QTimer::singleShot(0, this, [this, reference, currentState]()
                {
                    if (pdf::PDFWidgetAnnotationManager* manager = getAnnotationManager())
                    {
                        manager->setAnnotationReviewState(reference, currentState);
                    }
                });
            });
        }
    }

    QAction* copyAction = menu.addAction(tr("Copy Text"));
    copyAction->setEnabled(!text.isEmpty());
    connect(copyAction, &QAction::triggered, this, [text]() { QApplication::clipboard()->setText(text); });

    menu.addSeparator();

    QAction* deleteAction = menu.addAction(isReply ? tr("Delete Reply") : tr("Delete"));
    deleteAction->setEnabled(canModify);
    connect(deleteAction, &QAction::triggered, this, [this, reference]()
    {
        QTimer::singleShot(0, this, [this, reference]()
        {
            if (pdf::PDFWidgetAnnotationManager* manager = getAnnotationManager())
            {
                manager->deleteAnnotation(reference);
            }
        });
    });

    menu.exec(button->mapToGlobal(QPoint(0, button->height())));
}

void PDFFireCommentsPanel::startInlineEdit(pdf::PDFObjectReference reference)
{
    bool isReply = false;
    if (!findItem(reference, &isReply))
    {
        return;
    }

    m_editedComment = reference;
    if (!isReply)
    {
        m_selected = reference;
    }
    QTimer::singleShot(0, this, &PDFFireCommentsPanel::rebuildCards);
}

void PDFFireCommentsPanel::postReply(pdf::PDFObjectReference reference, QPlainTextEdit* editor)
{
    const QString text = editor->toPlainText();
    if (text.trimmed().isEmpty())
    {
        return;
    }

    editor->setEnabled(false);

    // The document is modified later - the reply box is deleted, when the panel is rebuilt
    QTimer::singleShot(0, this, [this, reference, text]()
    {
        if (pdf::PDFWidgetAnnotationManager* annotationManager = getAnnotationManager())
        {
            annotationManager->addAnnotationReply(reference, text);
        }
    });
}

bool PDFFireCommentsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)
    {
        QPlainTextEdit* editor = qobject_cast<QPlainTextEdit*>(watched);
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        const bool isReturn = keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;
        if (editor && isReturn && !(keyEvent->modifiers() & Qt::ShiftModifier))
        {
            event->accept();
            if (event->type() == QEvent::KeyPress)
            {
                postReply(getReferenceProperty(editor), editor);
            }
            return true;
        }
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFFireCommentsPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::PaletteChange && m_scrollArea)
    {
        rebuildCards();
    }
}

}   // namespace pdfviewer
