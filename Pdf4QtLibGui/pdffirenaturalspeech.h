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

#ifndef PDFFIRENATURALSPEECH_H
#define PDFFIRENATURALSPEECH_H

#include "pdfviewerglobal.h"

#include <QLocale>
#include <QObject>
#include <QStringList>
#include <QVector>
#include <QFuture>

#include <atomic>
#include <memory>

class QAudioSink;
class QThread;

namespace pdfviewer
{
class PDFFireSpeechAudioBuffer;

/// PDF Fire: a natural voice of the Kokoro model (Apache 2.0) - the text is
/// converted to phonemes (PDFFireG2P), the model creates the speech (ONNX Runtime,
/// on the processor, offline), and it is played. The speech is created a sentence
/// ahead in a background thread, so it is played without gaps.
///
/// The interface follows QTextToSpeech (say, pause, resume, stop, the state), so
/// the reader of the document uses it the same way.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFFireNaturalSpeech : public QObject
{
    Q_OBJECT

public:
    explicit PDFFireNaturalSpeech(QObject* parent);
    virtual ~PDFFireNaturalSpeech() override;

    enum State
    {
        Ready,
        Speaking,
        Paused,
        Error
    };

    struct Voice
    {
        QString id;         ///< Name of the voice file (af_heart)
        QString name;       ///< Name shown to the user
        QLocale locale;
    };

    /// The voices coming with PDF Fire
    static QList<Voice> getVoices();

    /// The locales of the voices
    static QList<QLocale> getLocales();

    /// Directory of the model, the voices and the dictionaries (empty, when not found)
    static QString getVoiceDirectory();

    /// Creates the speech of the text (in the calling thread) - 24 kHz, mono, samples -1..1
    /// \param text Text
    /// \param voiceId Voice
    /// \param speed Speed (1 = normal)
    /// \param errorMessage Error message, when the speech can't be created
    static QVector<float> synthesize(const QString& text, const QString& voiceId, double speed, QString* errorMessage);

    /// Saves the samples as a WAV file (16 bit, 24 kHz, mono)
    static bool saveWav(const QString& fileName, const QVector<float>& samples);

    static constexpr int SAMPLE_RATE = 24000;

    State state() const { return m_state; }
    QString errorString() const { return m_errorString; }

    void say(const QString& text);
    void stop();
    void pause();
    void resume();

    void setVoice(const QString& voiceId);
    QString voice() const { return m_voiceId; }

    /// Rate -1..1 (0 = normal speed)
    void setRate(double rate);

    /// Volume 0..1
    void setVolume(double volume);

signals:
    void stateChanged(PDFFireNaturalSpeech::State state);
    void errorOccurred(const QString& errorString);

private:
    void setState(State state);
    void onAudioReady(const QByteArray& samples, quint64 generation);
    void onGenerationFinished(quint64 generation);
    void onGenerationFailed(const QString& errorMessage, quint64 generation);
    void onAudioIdle();

    State m_state = Ready;
    QString m_errorString;
    QString m_voiceId = QStringLiteral("af_heart");
    double m_speed = 1.0;
    double m_volume = 1.0;

    QAudioSink* m_audioSink = nullptr;
    PDFFireSpeechAudioBuffer* m_audioBuffer = nullptr;
    std::shared_ptr<std::atomic<quint64>> m_generation;
    bool m_isGenerationFinished = false;
    QList<QFuture<void>> m_futures;     ///< The running syntheses (waited for in the destructor)
};

}   // namespace pdfviewer

#endif // PDFFIRENATURALSPEECH_H
