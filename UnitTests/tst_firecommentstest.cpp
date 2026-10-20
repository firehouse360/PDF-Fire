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

// PDF Fire: the comment card (new notes, replies, edited text) and the threaded comments panel

#include <QtTest>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>

#include "pdfdocumentbuilder.h"
#include "pdfannotationmanipulator.h"
#include "pdffirecommentcard.h"
#include "pdffirecommentspanel.h"

using namespace pdf;
using namespace pdfviewer;

class FireCommentsTest : public QObject
{
    Q_OBJECT

private slots:
    void cardPostsWithCtrlEnter();
    void cardCancelsWithEscape();
    void cardDoesNotPostEmptyText();
    void cardPostButton();
    void modelThreads();
    void modelSearch();
    void panelShowsThreads();

private:
    static PDFDocument createDocument(PDFObjectReference* note = nullptr);
};

PDFDocument FireCommentsTest::createDocument(PDFObjectReference* noteReference)
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = PDFAnnotationManipulator::createStickyNote(&builder, page1, QPointF(100, 300), TextAnnotationIcon::Comment, "Mike Davis", "Check the hydrant list", QColor());
    const PDFObjectReference reply = PDFAnnotationManipulator::addReply(&builder, note, "Chief", "Done on Tuesday");
    PDFAnnotationManipulator::addReply(&builder, reply, "Mike Davis", "Thanks");
    PDFAnnotationManipulator::setReviewState(&builder, note, "Completed", "Chief");
    PDFAnnotationManipulator::createStickyNote(&builder, page2, QPointF(50, 350), TextAnnotationIcon::Note, "Captain", "Second page note", QColor(0, 128, 255));
    builder.createAnnotationLink(page2, QRectF(300, 300, 50, 20), QString("https://example.com"), LinkHighlightMode::Invert);

    if (noteReference)
    {
        *noteReference = note;
    }

    return builder.build();
}

void FireCommentsTest::cardPostsWithCtrlEnter()
{
    QWidget host;
    host.resize(800, 600);
    host.show();
    PDFFireCommentCard* card = new PDFFireCommentCard("Mike Davis", "Sticky note", QString(), "Add a comment", &host);
    QSignalSpy postedSpy(card, &PDFFireCommentCard::posted);
    QSignalSpy cancelledSpy(card, &PDFFireCommentCard::cancelled);
    card->showAt(&host, QPoint(100, 100));

    // The card is next to the clicked point, inside the host
    QVERIFY(card->isVisible());
    QVERIFY(card->x() >= 100);
    QVERIFY(host.rect().contains(card->geometry()));

    // The header shows the author and the label
    QStringList labels;
    for (QLabel* label : card->findChildren<QLabel*>())
    {
        labels << label->text();
    }
    QVERIFY(labels.contains("Mike Davis"));
    QVERIFY(labels.contains("Sticky note"));

    QPlainTextEdit* editor = card->getEditor();
    QTest::keyClicks(editor, "First line");
    QTest::keyClick(editor, Qt::Key_Return);   // plain Enter makes a new line
    QTest::keyClicks(editor, "second line");
    QCOMPARE(postedSpy.count(), 0);

    QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(postedSpy.count(), 1);
    QCOMPARE(postedSpy.front().front().toString(), QString("First line\nsecond line"));
    QCOMPARE(cancelledSpy.count(), 0);
    QVERIFY(!card->isVisible());
}

void FireCommentsTest::cardCancelsWithEscape()
{
    QWidget host;
    host.resize(400, 300);
    host.show();
    PDFFireCommentCard* card = new PDFFireCommentCard("Mike Davis", "Reply", "Old text", "Add a reply", &host);
    QSignalSpy postedSpy(card, &PDFFireCommentCard::posted);
    QSignalSpy cancelledSpy(card, &PDFFireCommentCard::cancelled);

    // Near the right edge - the card is moved to the left, so it stays inside
    card->showAt(&host, QPoint(390, 290));
    QVERIFY(host.rect().contains(card->geometry()));
    QCOMPARE(card->getText(), QString("Old text"));

    QTest::keyClick(card->getEditor(), Qt::Key_Escape);
    QCOMPARE(cancelledSpy.count(), 1);
    QCOMPARE(postedSpy.count(), 0);
}

void FireCommentsTest::cardDoesNotPostEmptyText()
{
    QWidget host;
    host.resize(400, 300);
    host.show();
    PDFFireCommentCard* card = new PDFFireCommentCard("A", "Sticky note", QString(), QString(), &host);
    QSignalSpy postedSpy(card, &PDFFireCommentCard::posted);
    card->showAt(&host, QPoint(10, 10));

    QTest::keyClicks(card->getEditor(), "   ");
    QTest::keyClick(card->getEditor(), Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(postedSpy.count(), 0);
    QVERIFY(card->isVisible());

    QPushButton* postButton = card->findChild<QPushButton*>("pdfFireCommentCardPost");
    QVERIFY(postButton);
    QVERIFY(!postButton->isEnabled());
}

void FireCommentsTest::cardPostButton()
{
    QWidget host;
    host.resize(400, 300);
    host.show();
    PDFFireCommentCard* card = new PDFFireCommentCard("A", "Sticky note", QString(), QString(), &host);
    QSignalSpy postedSpy(card, &PDFFireCommentCard::posted);
    card->showAt(&host, QPoint(10, 10));

    QTest::keyClicks(card->getEditor(), "Text");
    QPushButton* postButton = card->findChild<QPushButton*>("pdfFireCommentCardPost");
    QVERIFY(postButton->isEnabled());
    postButton->click();
    QCOMPARE(postedSpy.count(), 1);
    QCOMPARE(postedSpy.front().front().toString(), QString("Text"));
}

void FireCommentsTest::modelThreads()
{
    PDFObjectReference note;
    const PDFDocument document = createDocument(&note);
    const std::vector<PDFFireCommentItem> items = PDFFireCommentsModel::build(&document);

    // Two comments (the link is not a comment); the replies are in the thread of the first one,
    // the status reply is not a reply - it is the status of the comment
    QCOMPARE(items.size(), size_t(2));
    QCOMPARE(items[0].reference, note);
    QCOMPARE(items[0].pageIndex, PDFInteger(0));
    QCOMPARE(items[0].author, QString("Mike Davis"));
    QCOMPARE(items[0].text, QString("Check the hydrant list"));
    QCOMPARE(items[0].state, QString("Completed"));
    QCOMPARE(items[0].replies.size(), size_t(2));
    QCOMPARE(items[0].replies[0].text, QString("Done on Tuesday"));
    QCOMPARE(items[0].replies[1].text, QString("Thanks"));
    QVERIFY(items[0].dateTime.isValid());
    QCOMPARE(items[0].color, PDFAnnotationManipulator::getDefaultStickyNoteColor());
    QCOMPARE(items[1].pageIndex, PDFInteger(1));
    QCOMPARE(items[1].author, QString("Captain"));
    QVERIFY(items[1].state.isEmpty());
    QCOMPARE(PDFFireCommentsModel::count(items), 4);
    QCOMPARE(PDFFireCommentsModel::count({}), 0);
    QVERIFY(PDFFireCommentsModel::build(nullptr).empty());
}

void FireCommentsTest::modelSearch()
{
    const PDFDocument document = createDocument();
    const std::vector<PDFFireCommentItem> items = PDFFireCommentsModel::build(&document);

    QVERIFY(PDFFireCommentsModel::matches(items[0], QString()));
    QVERIFY(PDFFireCommentsModel::matches(items[0], "HYDRANT"));
    QVERIFY(PDFFireCommentsModel::matches(items[0], "tuesday"));   // text of a reply
    QVERIFY(PDFFireCommentsModel::matches(items[0], "chief"));     // author of a reply
    QVERIFY(PDFFireCommentsModel::matches(items[0], "sticky"));    // kind of the comment
    QVERIFY(!PDFFireCommentsModel::matches(items[0], "second page"));
    QVERIFY(PDFFireCommentsModel::matches(items[1], "second page"));
}

void FireCommentsTest::panelShowsThreads()
{
    const PDFDocument document = createDocument();

    PDFFireCommentsPanel panel(nullptr, nullptr);
    panel.resize(320, 600);
    panel.setDocument(&document);
    panel.rebuildNow();

    // The old cards are hidden at once and deleted later - only the shown ones are counted
    auto countShown = [&panel](const char* name)
    {
        int count = 0;
        for (QWidget* widget : panel.findChildren<QWidget*>(name))
        {
            count += widget->isVisibleTo(&panel) ? 1 : 0;
        }
        return count;
    };

    QCOMPARE(panel.getCommentCount(), 4);
    QLabel* countLabel = panel.findChild<QLabel*>("pdfFireCommentsCount");
    QVERIFY(countLabel);
    QCOMPARE(countLabel->text(), QString("4"));
    QCOMPARE(countShown("pdfFireCommentCard"), 2);
    QCOMPARE(countShown("pdfFireCommentReply"), 2);

    QLabel* statusLabel = panel.findChild<QLabel*>("pdfFireCommentStatus");
    QVERIFY(statusLabel);
    QVERIFY(statusLabel->text().contains("Completed"));

    // Search filters the cards
    QLineEdit* search = panel.findChild<QLineEdit*>("pdfFireCommentsSearch");
    QVERIFY(search);
    search->setText("second page");
    QCOMPARE(countShown("pdfFireCommentCard"), 1);

    // An empty document shows the empty state
    panel.setDocument(nullptr);
    search->clear();
    panel.rebuildNow();
    QCOMPARE(panel.getCommentCount(), 0);
    QCOMPARE(countShown("pdfFireCommentsEmpty"), 1);
    QCOMPARE(countShown("pdfFireCommentCard"), 0);
}

QTEST_MAIN(FireCommentsTest)

#include "tst_firecommentstest.moc"
