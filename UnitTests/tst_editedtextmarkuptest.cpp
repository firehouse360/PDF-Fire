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

#include "pdffireeditedtext.h"

#include <QtTest>

using namespace pdf;

class EditedTextMarkupTest : public QObject
{
    Q_OBJECT

private slots:
    void plainText_data();
    void plainText();
    void unchangedTextKeepsMarkup();
    void edit_data();
    void edit();
    void fontSize();
};

void EditedTextMarkupTest::plainText_data()
{
    QTest::addColumn<QString>("markup");
    QTest::addColumn<QString>("plainText");

    QTest::newRow("characters") << "Hello" << "Hello";
    QTest::newRow("escaped") << "a &lt; b &amp; c &gt; d &quot;e&quot;" << "a < b & c > d \"e\"";
    QTest::newRow("state commands are invisible") << "<tf font=\"F1\" size=\"12\"/><fill g=\"0\"/>Hello<tc v=\"0.5\"/> World" << "Hello World";
    QTest::newRow("small kerning is invisible") << "He<space advance=\"-20\"/>llo<space advance=\"35\"/>!" << "Hello!";
    QTest::newRow("a word gap is a space") << "Hello<space advance=\"-278\"/>World" << "Hello World";
    QTest::newRow("a word gap after a space adds nothing") << "Hello <space advance=\"-278\"/>World" << "Hello World";
    QTest::newRow("first position is invisible") << "<tpos x=\"72\" y=\"700\"/>Hello" << "Hello";
    QTest::newRow("new line") << "<tpos x=\"72\" y=\"700\"/>First<tpos x=\"72\" y=\"686\"/>Second" << "First\nSecond";
    QTest::newRow("same line, another position") << "<tpos x=\"72\" y=\"700\"/>First<tpos x=\"140\" y=\"700\"/>Second" << "First Second";
    QTest::newRow("every character positioned") << "<tpos x=\"100\" y=\"700\"/>u<tpos x=\"106\" y=\"700\"/>s<tpos x=\"111\" y=\"700\"/>e<tpos x=\"121\" y=\"700\"/>o<tpos x=\"127\" y=\"700\"/>f<tpos x=\"131\" y=\"700\"/>."
                                                 << "use of.";
    QTest::newRow("pieces of words positioned") << "<tpos x=\"100\" y=\"700\"/>netw<tpos x=\"124\" y=\"700\"/>orks,<tpos x=\"158\" y=\"700\"/>inc<tpos x=\"175\" y=\"700\"/>luding"
                                                << "networks, including";
    QTest::newRow("matrix is a position") << "<tmatrix m11=\"1\" m12=\"0\" m21=\"0\" m22=\"1\" x=\"72\" y=\"700\"/>A<tmatrix m11=\"1\" m12=\"0\" m21=\"0\" m22=\"1\" x=\"72\" y=\"600\"/>B" << "A\nB";
    QTest::newRow("ligature") << "Opera<character cid=\"31\" text=\"ti\"/>ng" << "Operating";
    QTest::newRow("glyph without unicode") << "A<character cid=\"17\"/>B" << QString("A") + PDFEditedTextMarkup::UNKNOWN_CHARACTER + QString("B");
}

void EditedTextMarkupTest::plainText()
{
    QFETCH(QString, markup);
    QFETCH(QString, plainText);

    const PDFEditedTextMarkup model(markup);
    QCOMPARE(model.getPlainText(), plainText);
    QCOMPARE(model.getMarkup(), markup);
}

void EditedTextMarkupTest::unchangedTextKeepsMarkup()
{
    const QString markup = "<tf font=\"F1\" size=\"12\"/><tpos x=\"72\" y=\"700\"/>He<space advance=\"-20\"/>llo<space advance=\"-278\"/>W&amp;B<tpos x=\"72\" y=\"686\"/>Next<character cid=\"5\"/>";
    const PDFEditedTextMarkup model(markup);
    QCOMPARE(model.getMarkupWithPlainText(model.getPlainText()), markup);
}

void EditedTextMarkupTest::edit_data()
{
    QTest::addColumn<QString>("markup");
    QTest::addColumn<QString>("newPlainText");
    QTest::addColumn<QString>("expectedMarkup");

    const QString start = "<tf font=\"F1\" size=\"12\"/><tpos x=\"72\" y=\"700\"/>";

    QTest::newRow("append") << start + "Hello" << "Hello!" << start + "Hello!";
    QTest::newRow("insert at the start stays positioned and styled") << start + "World" << "Hello World" << start + "Hello World";
    QTest::newRow("insert in the middle") << start + "Helo" << "Hello" << start + "Hello";
    QTest::newRow("delete") << start + "Hello World" << "Hello" << start + "Hello";
    QTest::newRow("replace a word") << start + "Hello World" << "Hello There" << start + "Hello There";
    QTest::newRow("typed special characters are escaped") << start + "a b" << "a <&> b" << start + "a &lt;&amp;&gt; b";

    // The kerning of the text, which was not edited, is not touched
    QTest::newRow("kerning before the edit is kept") << start + "A<space advance=\"-20\"/>V and more" << "AV and less"
                                                     << start + "A<space advance=\"-20\"/>V and less";
    QTest::newRow("kerning after the edit is kept") << start + "more and A<space advance=\"-20\"/>V" << "less and AV"
                                                    << start + "less and A<space advance=\"-20\"/>V";
    QTest::newRow("kerning at a pure insertion is kept") << start + "l<space advance=\"-2\"/> and" << "ls and"
                                                        << start + "ls<space advance=\"-2\"/> and";
    QTest::newRow("kerning inside the replaced text is dropped") << start + "xA<space advance=\"-20\"/>Vx" << "xWWx" << start + "xWWx";

    // A gap between words made by kerning: if it is not edited, it stays a kerning
    QTest::newRow("word gap is kept") << start + "Hello<space advance=\"-278\"/>World" << "Hello World!"
                                      << start + "Hello<space advance=\"-278\"/>World!";
    QTest::newRow("typing after a word gap keeps the gap") << start + "Hello<space advance=\"-278\"/>World" << "Hello my World"
                                                           << start + "Hello<space advance=\"-278\"/>my World";
    QTest::newRow("replaced word gap becomes typed text") << start + "Hello<space advance=\"-278\"/>World" << "Hello-World"
                                                          << start + "Hello-World";

    // The second line keeps its position, whatever happens to the first one
    const QString secondLine = "<tpos x=\"72\" y=\"686\"/>Second";
    QTest::newRow("edit of the first line") << start + "First" + secondLine << "First line\nSecond" << start + "First line" + secondLine;
    QTest::newRow("edit of the second line") << start + "First" + secondLine << "First\nSecond line" << start + "First" + secondLine + " line";
    QTest::newRow("a removed line break keeps the position of the line") << start + "First" + secondLine << "FirstSecond" << start + "First" + secondLine;
    QTest::newRow("a typed line break is a space") << start + "OneTwo" << "One\nTwo" << start + "One Two";

    // A change of the style in the text: the typed text continues the text before it
    QTest::newRow("typing before a color change") << start + "black<fill r=\"1\" g=\"0\" b=\"0\"/>red" << "black!red"
                                                   << start + "black!<fill r=\"1\" g=\"0\" b=\"0\"/>red";
    QTest::newRow("a style command inside replaced text is kept") << start + "ab<fill r=\"1\" g=\"0\" b=\"0\"/>cd" << "aXd"
                                                                  << start + "aX<fill r=\"1\" g=\"0\" b=\"0\"/>d";

    QTest::newRow("glyph without unicode is kept") << start + "A<character cid=\"17\"/>B" << QString("A") + PDFEditedTextMarkup::UNKNOWN_CHARACTER + QString("BC")
                                                   << start + "A<character cid=\"17\"/>BC";
    const QString ligature = "<character cid=\"31\" text=\"ti\"/>";
    QTest::newRow("ligature kept when not edited") << start + "Opera" + ligature + "ng" << "Operating!" << start + "Opera" + ligature + "ng!";
    QTest::newRow("edit before the ligature") << start + "Opera" + ligature + "ng" << "Cooperating" << start + "Coopera" + ligature + "ng";
    QTest::newRow("edit inside the ligature replaces it") << start + "Opera" + ligature + "ng" << "Operatng" << start + "Operatng";
    QTest::newRow("edit ending inside the ligature") << start + "Opera" + ligature + "ng" << "Operaxing" << start + "Operaxing";
    QTest::newRow("everything deleted") << start + "Hello" << "" << start;
    QTest::newRow("everything replaced") << start + "Hello" << "Bye" << start + "Bye";
}

void EditedTextMarkupTest::edit()
{
    QFETCH(QString, markup);
    QFETCH(QString, newPlainText);
    QFETCH(QString, expectedMarkup);

    const PDFEditedTextMarkup model(markup);
    const QString newMarkup = model.getMarkupWithPlainText(newPlainText);
    QCOMPARE(newMarkup, expectedMarkup);

    // The new markup reads as the new text (a typed line break is a space)
    QString expectedPlainText = newPlainText;
    if (QByteArray(QTest::currentDataTag()) == "a typed line break is a space")
    {
        expectedPlainText.replace(QChar('\n'), QChar(' '));
    }
    else if (QByteArray(QTest::currentDataTag()) == "a removed line break keeps the position of the line")
    {
        expectedPlainText = PDFEditedTextMarkup(markup).getPlainText();
    }
    QCOMPARE(PDFEditedTextMarkup(newMarkup).getPlainText(), expectedPlainText);
}

void EditedTextMarkupTest::fontSize()
{
    const PDFEditedTextMarkup noFont("Hello");
    QCOMPARE(noFont.getFirstFontSize(), 0.0);
    QCOMPARE(noFont.getMarkupWithScaledFontSize(2.0), QString("Hello"));

    const PDFEditedTextMarkup model("<tf font=\"F1\" size=\"12\"/>Big<tf font=\"F2\" size=\"8\"/>small");
    QCOMPARE(model.getFirstFontSize(), 12.0);
    QCOMPARE(model.getMarkupWithScaledFontSize(1.5), QString("<tf font=\"F1\" size=\"18\"/>Big<tf font=\"F2\" size=\"12\"/>small"));
    QCOMPARE(PDFEditedTextMarkup(model.getMarkupWithScaledFontSize(1.5)).getPlainText(), QString("Bigsmall"));
}

QTEST_APPLESS_MAIN(EditedTextMarkupTest)

#include "tst_editedtextmarkuptest.moc"
