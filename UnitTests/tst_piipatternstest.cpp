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

// The cases are those of PdfCraft crates/redact/src/patterns.rs (MIT OR Apache-2.0),
// whose matchers PDFFirePiiPatterns ports.

#include "pdffirepiipatterns.h"

#include <QtTest>

using namespace pdf;
using Pattern = PDFFirePiiPatterns::Pattern;

class PiiPatternsTest : public QObject
{
    Q_OBJECT

private slots:
    void socialSecurityNumbers();
    void phoneNumbers();
    void tailOfLongerNumberIsNotPhoneNumber();
    void emails();
    void emailsWithLeadingPunctuation();
    void internationalizedEmails();
    void creditCardsNeedValidChecksum();
    void creditCardFollowedByAnotherNumber();
    void luhn();
    void dates();
    void datesInOtherLanguages();
    void rangesAreUtf16();

private:
    static QStringList found(Pattern pattern, const char* text) { return PDFFirePiiPatterns::findTexts(pattern, QString::fromUtf8(text)); }
};

void PiiPatternsTest::socialSecurityNumbers()
{
    QCOMPARE(found(Pattern::SocialSecurityNumber, "SSN 123-45-6789 and 123 45 6789; invalid 000-12-3456, 666-12-3456, 912-12-3456, 1234-56-7890"),
             QStringList({ "123-45-6789", "123 45 6789" }));
    QCOMPARE(found(Pattern::SocialSecurityNumber, "123-45-67890 or 123456789"), QStringList());
}

void PiiPatternsTest::phoneNumbers()
{
    QCOMPARE(found(Pattern::PhoneNumber, "Call (555) 123-4567 or 555.987.6543, +1 555 222 3333; not 12345 or 5551234567890"),
             QStringList({ "(555) 123-4567", "555.987.6543", "+1 555 222 3333" }));
    QCOMPARE(found(Pattern::PhoneNumber, "Tel. +49 2151 123456."), QStringList({ "+49 2151 123456" }));
    QCOMPARE(found(Pattern::PhoneNumber, "+44 20 7946 0018, +33 1 23 45 67 89, +49 (0) 2151 123456, (0211) 123456, 030 12345678, 01 23 45 67 89"),
             QStringList({ "+44 20 7946 0018", "+33 1 23 45 67 89", "+49 (0) 2151 123456", "(0211) 123456", "030 12345678", "01 23 45 67 89" }));

    // Not phone numbers: day-first dates, decimals, unseparated, too short or too long
    QCOMPARE(found(Pattern::PhoneNumber, "on 01.10.2026 or 09-10-2026, ratio 0.1234567, id 0211123456, +4921511234567, +1 234 567, 0211 12, +49 1234 5678 9012 3456"),
             QStringList());

    // A social security number is not a phone number
    QCOMPARE(found(Pattern::PhoneNumber, "SSN 123-45-6789"), QStringList());
}

void PiiPatternsTest::tailOfLongerNumberIsNotPhoneNumber()
{
    // PdfCraft #1884: the last groups of a French social security number, an IBAN and a
    // SIRET used to be marked as phone numbers (and the start left visible)
    for (const char* text : { "NIR 1 85 05 78 006 084 36 end", "IBAN FR76 3000 6000 0112 3456 7890 189", "SIRET 123 456 782 00010" })
    {
        QCOMPARE(found(Pattern::PhoneNumber, text), QStringList());
    }
    QCOMPARE(found(Pattern::PhoneNumber, "tel 01 23 45 67 89 / +33 1 23 45 67 89"), QStringList({ "01 23 45 67 89", "+33 1 23 45 67 89" }));

    // Nor the head of a run too long to be one
    QCOMPARE(found(Pattern::PhoneNumber, "01 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7"), QStringList());

    // An account number grouped like a US phone number with more groups after it
    QCOMPARE(found(Pattern::PhoneNumber, "Account 555 123 4567 8901"), QStringList());
}

void PiiPatternsTest::emails()
{
    QCOMPARE(found(Pattern::EmailAddress, "Write to ada.lovelace+pdf@example.co.uk, or bob@host (no TLD) or x@y.z."),
             QStringList({ "ada.lovelace+pdf@example.co.uk" }));
}

void PiiPatternsTest::emailsWithLeadingPunctuation()
{
    for (const char* lead : { "_", "-", ".", "%", "+" })
    {
        const QString address = QString::fromLatin1(lead) + "john@example.com";
        QCOMPARE(PDFFirePiiPatterns::findTexts(Pattern::EmailAddress, "Write to " + address + " now"), QStringList({ address }));
    }

    // A run of local characters is matched whole, never from its middle
    QCOMPARE(found(Pattern::EmailAddress, "a.b@x.com, foo_john@example.com"), QStringList({ "a.b@x.com", "foo_john@example.com" }));

    // Punctuation alone is no local part
    QCOMPARE(found(Pattern::EmailAddress, "Write to ..@example.com or -@example.com"), QStringList());
}

void PiiPatternsTest::internationalizedEmails()
{
    QCOMPARE(found(Pattern::EmailAddress, "Yazın: ayşe.yılmaz@örnek.com.tr, ÇAĞLAR@ŞİRKET.COM.TR; müller@bücher.de, иван@пример.рф, josé@correo.es."),
             QString::fromUtf8("ayşe.yılmaz@örnek.com.tr|ÇAĞLAR@ŞİRKET.COM.TR|müller@bücher.de|иван@пример.рф|josé@correo.es").split('|'));

    // Text in scripts without spaces stays out of the address around it
    QCOMPARE(found(Pattern::EmailAddress, "メールはtaro@example.jpまで、联系support@example.cn谢谢"), QStringList({ "taro@example.jp", "support@example.cn" }));

    // Still no address without a dot in the domain, or with a one-letter or numeric TLD
    QCOMPARE(found(Pattern::EmailAddress, "ayşe@örnek, ş@ğ.ü, kişi@alan.123, a@b.c"), QStringList());
}

void PiiPatternsTest::creditCardsNeedValidChecksum()
{
    QCOMPARE(found(Pattern::CreditCardNumber, "Visa 4111 1111 1111 1111, bad 4111 1111 1111 1112, amex 3782-822463-10005"),
             QStringList({ "4111 1111 1111 1111", "3782-822463-10005" }));
    QCOMPARE(found(Pattern::CreditCardNumber, "4111111111111111 and 4111111111111112"), QStringList({ "4111111111111111" }));
}

void PiiPatternsTest::creditCardFollowedByAnotherNumber()
{
    for (const char* tail : { "12/26", "2026", "123", "12 26" })
    {
        QCOMPARE(PDFFirePiiPatterns::findTexts(Pattern::CreditCardNumber, QString("Card 4111 1111 1111 1111 %1").arg(tail)), QStringList({ "4111 1111 1111 1111" }));
    }
    QCOMPARE(found(Pattern::CreditCardNumber, "Card 3782-822463-10005 123"), QStringList({ "3782-822463-10005" }));

    // A run, which is not a card at any group boundary, matches nothing
    QCOMPARE(found(Pattern::CreditCardNumber, "Card 4111 1111 1111 1112 12/26"), QStringList());

    // A longer grouped number doesn't match on a card-shaped prefix
    QCOMPARE(found(Pattern::CreditCardNumber, "IBAN 4111 1111 1111 1111 0000 0000"), QStringList());
}

void PiiPatternsTest::luhn()
{
    QVERIFY(PDFFirePiiPatterns::isLuhnValid("4111 1111 1111 1111"));
    QVERIFY(PDFFirePiiPatterns::isLuhnValid("378282246310005"));
    QVERIFY(PDFFirePiiPatterns::isLuhnValid("5555555555554444"));
    QVERIFY(!PDFFirePiiPatterns::isLuhnValid("4111 1111 1111 1112"));
    QVERIFY(!PDFFirePiiPatterns::isLuhnValid("1234567812345678"));
    QVERIFY(!PDFFirePiiPatterns::isLuhnValid(""));
}

void PiiPatternsTest::dates()
{
    QCOMPARE(found(Pattern::Date, "Due 10/01/2026, 2026-10-01, 1.10.26, March 5, 2024, Jan. 7 2025 and 5 June 2023; not 10/2026 or Mayday 12"),
             QStringList({ "10/01/2026", "2026-10-01", "1.10.26", "March 5, 2024", "Jan. 7 2025", "5 June 2023" }));
}

void PiiPatternsTest::datesInOtherLanguages()
{
    QCOMPARE(found(Pattern::Date, "Tarih: 10 Ekim 2026, 3 Mayıs 2025, 5 Şubat 2024, 5 ŞUBAT 2024, 3 MAYIS 2025, 1 EKİM 2026 ve 9 kasım 2023."),
             QString::fromUtf8("10 Ekim 2026|3 Mayıs 2025|5 Şubat 2024|5 ŞUBAT 2024|3 MAYIS 2025|1 EKİM 2026|9 kasım 2023").split('|'));
    QCOMPARE(found(Pattern::Date, "5 Martı 2024, Ekimde 2026, 3 Mayısta 2025, ara 2024, Kasım ayında, 12 Aralık, Aralık 2024"), QStringList());
    QCOMPARE(found(Pattern::Date, "am 10. Oktober 2026, 3 März 2025, le 14 juillet 2024, 1 août 2023, il 25 dicembre 2022"),
             QString::fromUtf8("10. Oktober 2026|3 März 2025|14 juillet 2024|1 août 2023|25 dicembre 2022").split('|'));
    QCOMPARE(found(Pattern::Date, "Mai 2024, Kapitel 5. Absatz 2024, 5 Oktoberfest 2024"), QStringList());
}

void PiiPatternsTest::rangesAreUtf16()
{
    // A character outside the BMP (two UTF-16 units) before the match
    const QString text = QString::fromUtf8("😀 SSN 123-45-6789");
    const std::vector<PDFFirePiiPatterns::Match> matches = PDFFirePiiPatterns::find(Pattern::SocialSecurityNumber, text);
    QCOMPARE(matches.size(), size_t(1));
    QCOMPARE(text.mid(matches.front().start, matches.front().length), QString("123-45-6789"));
}

QTEST_GUILESS_MAIN(PiiPatternsTest)

#include "tst_piipatternstest.moc"
