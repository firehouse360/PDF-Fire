// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdffireg2p.h"
#include "pdffirenaturalspeech.h"

#include <QtTest>
#include <QElapsedTimer>
#include <QFile>

#include <cmath>

using namespace pdfviewer;

/// PDF Fire: the natural voices - the text to phonemes, and the speech of the model
class NaturalSpeechTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void numberWords();
    void phonemes_data();
    void phonemes();
    void sentences();
    void documentCoverage();
    void synthesis();

private:
    PDFFireG2P m_g2p;
};

void NaturalSpeechTest::initTestCase()
{
    const QString directory = PDFFireNaturalSpeech::getVoiceDirectory();
    QVERIFY2(!directory.isEmpty(), "The voice folder was not found");
    QString errorMessage;
    QVERIFY2(m_g2p.load(directory, &errorMessage), qPrintable(errorMessage));
}

void NaturalSpeechTest::numberWords()
{
    QCOMPARE(PDFFireNumberWords::cardinal(0), QString("zero"));
    QCOMPARE(PDFFireNumberWords::cardinal(21), QString("twenty-one"));
    QCOMPARE(PDFFireNumberWords::cardinal(105), QString("one hundred five"));
    QCOMPARE(PDFFireNumberWords::cardinal(1234567), QString("one million two hundred thirty-four thousand five hundred sixty-seven"));
    QCOMPARE(PDFFireNumberWords::ordinal(1), QString("first"));
    QCOMPARE(PDFFireNumberWords::ordinal(22), QString("twenty-second"));
    QCOMPARE(PDFFireNumberWords::ordinal(40), QString("fortieth"));
    QCOMPARE(PDFFireNumberWords::year(1990), QString("nineteen ninety"));
    QCOMPARE(PDFFireNumberWords::year(2005), QString("two thousand five"));
    QCOMPARE(PDFFireNumberWords::year(2026), QString("twenty twenty-six"));
    QCOMPARE(PDFFireNumberWords::year(1905), QString("nineteen oh five"));
    QCOMPARE(PDFFireNumberWords::year(1400), QString("fourteen hundred"));
}

void NaturalSpeechTest::phonemes_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");

    // The phonemes of misaki (Kokoro 1.0) for the same text
    QTest::newRow("plain") << "The fire department." << QString::fromUtf8("ðə fˈIəɹ dəpˈɑɹtmənt.");
    QTest::newRow("the before vowel") << "the engine" << QString::fromUtf8("ði ˈɛnʤən");
    QTest::newRow("plural") << "trucks" << QString::fromUtf8("tɹˈʌks");
    QTest::newRow("comma") << "Yes, sir." << QString::fromUtf8("jˈɛs, sˌɜɹ.");
    QTest::newRow("-ied") << "verified" << QString::fromUtf8("vˈɛɹəfˌId");
    QTest::newRow("township") << "Franklin Twp" << QString::fromUtf8("fɹˈæŋklən tˈWnʃˌɪp");
    QTest::newRow("fire words") << "fireground" << QString::fromUtf8("fˈIəɹɡɹˌWnd");
    QTest::newRow("abbreviation") << "Lt. Smith" << QString::fromUtf8("lutˈɛnənt smˈɪθ");
}

void NaturalSpeechTest::phonemes()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    QCOMPARE(m_g2p.phonemize(text), expected);
}

void NaturalSpeechTest::sentences()
{
    const QStringList sentences = PDFFireG2P::splitSentences("First sentence. Second one! And a third?");
    QCOMPARE(sentences.size(), 3);

    // Numbers, money, percent, a year, time and acronyms give phonemes, not nothing
    QStringList unknown;
    for (const QString& text : { QString("Call 911 at 10:30 on May 3rd, 2026."), QString("It costs $5.50, a 50% discount."), QString("The VFD and EMS respond.") })
    {
        const QString phonemes = m_g2p.phonemize(text, &unknown);
        QVERIFY2(phonemes.size() > text.size() / 2, qPrintable(text + " -> " + phonemes));
        QVERIFY2(!phonemes.contains(QRegularExpression("[0-9$%]")), qPrintable(phonemes));
    }
}

void NaturalSpeechTest::documentCoverage()
{
    // A text file of a real document (PDFFIRE_SPEECH_TEXT) - the words, which are not in
    // the dictionaries, are reported
    const QString fileName = qEnvironmentVariable("PDFFIRE_SPEECH_TEXT");
    if (fileName.isEmpty())
    {
        QSKIP("PDFFIRE_SPEECH_TEXT is not set");
    }

    QFile file(fileName);
    QVERIFY(file.open(QFile::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());
    QStringList unknown;
    int words = 0;
    for (const QString& sentence : PDFFireG2P::splitSentences(text))
    {
        words += sentence.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
        m_g2p.phonemize(sentence, &unknown);
    }
    unknown.removeDuplicates();
    qInfo().noquote() << "Words:" << words << "unknown (split or spelled):" << unknown.size();
    qInfo().noquote() << unknown.join(", ");
}

void NaturalSpeechTest::synthesis()
{
    const QString text = "Welcome to the Prospect fire department. Please read the standard operating guidelines carefully.";
    QElapsedTimer timer;
    timer.start();
    QString errorMessage;
    const QVector<float> samples = PDFFireNaturalSpeech::synthesize(text, "af_heart", 1.0, &errorMessage);
    const qint64 elapsed = timer.elapsed();
    QVERIFY2(!samples.isEmpty(), qPrintable(errorMessage));

    const double seconds = double(samples.size()) / PDFFireNaturalSpeech::SAMPLE_RATE;
    float peak = 0;
    for (float sample : samples)
    {
        QVERIFY(std::isfinite(sample));
        peak = std::max(peak, std::abs(sample));
    }
    qInfo().noquote() << QString("Speech %1 s created in %2 s (including loading the model), peak %3").arg(seconds, 0, 'f', 2).arg(elapsed / 1000.0, 0, 'f', 2).arg(peak, 0, 'f', 2);
    QVERIFY(seconds > 3.0 && seconds < 15.0);
    QVERIFY(peak > 0.05f);

    // A second sentence, the model is loaded - the speed of the speech creation
    timer.restart();
    const QVector<float> second = PDFFireNaturalSpeech::synthesize("The engine company stretches the attack line.", "am_michael", 1.0, &errorMessage);
    const double secondSeconds = double(second.size()) / PDFFireNaturalSpeech::SAMPLE_RATE;
    qInfo().noquote() << QString("Real-time factor %1 (below 1 = faster than speaking)").arg(timer.elapsed() / 1000.0 / secondSeconds, 0, 'f', 2);
    QVERIFY(!second.isEmpty());

    const QString outputDirectory = qEnvironmentVariable("PDFFIRE_SPEECH_WAV_DIR");
    if (!outputDirectory.isEmpty())
    {
        // A sample of every voice (to listen to them)
        const QString sample = "These Standard Operating Guidelines are subordinate to, and governed by, the Department Bylaws. "
                               "In any conflict, the Bylaws control. Engine 13 responds with four firefighters at 10:30.";
        for (const PDFFireNaturalSpeech::Voice& voice : PDFFireNaturalSpeech::getVoices())
        {
            const QVector<float> voiceSamples = PDFFireNaturalSpeech::synthesize(sample, voice.id, 1.0, &errorMessage);
            QVERIFY2(!voiceSamples.isEmpty(), qPrintable(errorMessage));
            QVERIFY(PDFFireNaturalSpeech::saveWav(QString("%1/%2 - %3.wav").arg(outputDirectory, voice.id, voice.name), voiceSamples));
        }
    }
}

QTEST_GUILESS_MAIN(NaturalSpeechTest)

#include "tst_naturalspeechtest.moc"
