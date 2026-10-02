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

#include "pdffirenaturalspeech.h"
#include "pdffireg2p.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QFuture>
#include <QtConcurrent/QtConcurrentRun>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pdfviewer
{

namespace
{

QString tr(const char* text)
{
    return QCoreApplication::translate("PDFFireNaturalSpeech", text);
}

/// The model, the vocabulary, the voices and the dictionaries - loaded once, used by
/// one synthesis at a time (the model uses several threads itself)
class KokoroModel
{
public:
    static KokoroModel& instance()
    {
        static KokoroModel model;
        return model;
    }

    QVector<float> synthesize(const QString& text, const QString& voiceId, double speed, QString* errorMessage);

private:
    bool load(QString* errorMessage);
    const QVector<float>* getVoice(const QString& voiceId, QString* errorMessage);
    QVector<float> run(const std::vector<int64_t>& tokens, const QVector<float>& voice, float speed, QString* errorMessage);

    QMutex m_mutex;
    bool m_isLoaded = false;
    QString m_loadError;
    QString m_directory;
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
    std::vector<std::string> m_inputNames;
    std::vector<std::string> m_outputNames;
    ONNXTensorElementDataType m_speedType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    QHash<uint, int64_t> m_vocabulary;
    QHash<QString, QVector<float>> m_voices;
    PDFFireG2P m_g2p;
};

bool KokoroModel::load(QString* errorMessage)
{
    if (m_isLoaded)
    {
        return true;
    }
    if (!m_loadError.isEmpty())
    {
        *errorMessage = m_loadError;
        return false;
    }

    m_directory = PDFFireNaturalSpeech::getVoiceDirectory();
    if (m_directory.isEmpty())
    {
        m_loadError = tr("The natural voices are not installed (the folder of the voices was not found).");
        *errorMessage = m_loadError;
        return false;
    }

    QString g2pError;
    if (!m_g2p.load(m_directory, &g2pError))
    {
        m_loadError = g2pError;
        *errorMessage = m_loadError;
        return false;
    }

    QFile vocabularyFile(QDir(m_directory).filePath(QStringLiteral("vocab.json")));
    if (!vocabularyFile.open(QFile::ReadOnly))
    {
        m_loadError = tr("The vocabulary of the voice model is missing.");
        *errorMessage = m_loadError;
        return false;
    }
    const QJsonObject vocabulary = QJsonDocument::fromJson(vocabularyFile.readAll()).object();
    for (auto it = vocabulary.begin(); it != vocabulary.end(); ++it)
    {
        const QList<uint> codePoints = it.key().toUcs4();
        if (codePoints.size() == 1)
        {
            m_vocabulary.insert(codePoints.front(), it.value().toInteger());
        }
    }

    try
    {
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "pdf-fire");
        Ort::SessionOptions options;
        // The physical cores (the logical ones share the units of the processor)
        const int threads = qEnvironmentVariableIsSet("PDFFIRE_SPEECH_THREADS") ? qEnvironmentVariableIntValue("PDFFIRE_SPEECH_THREADS")
                                                                                 : qBound(1, QThread::idealThreadCount() / 2, 8);
        options.SetIntraOpNumThreads(threads);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        const QString modelName = qEnvironmentVariableIsSet("PDFFIRE_SPEECH_MODEL") ? qEnvironmentVariable("PDFFIRE_SPEECH_MODEL") : QStringLiteral("kokoro.onnx");
#ifdef Q_OS_WIN
        // ONNX Runtime takes the path as a wide string on Windows
        const std::wstring modelPath = QDir::toNativeSeparators(QDir(m_directory).filePath(modelName)).toStdWString();
        m_session = std::make_unique<Ort::Session>(*m_env, modelPath.c_str(), options);
#else
        const QByteArray modelPath = QDir(m_directory).filePath(modelName).toLocal8Bit();
        m_session = std::make_unique<Ort::Session>(*m_env, modelPath.constData(), options);
#endif

        Ort::AllocatorWithDefaultOptions allocator;
        for (size_t i = 0; i < m_session->GetInputCount(); ++i)
        {
            m_inputNames.emplace_back(m_session->GetInputNameAllocated(i, allocator).get());
            if (QByteArray(m_inputNames.back().c_str()).contains("speed"))
            {
                m_speedType = m_session->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetElementType();
            }
        }
        for (size_t i = 0; i < m_session->GetOutputCount(); ++i)
        {
            m_outputNames.emplace_back(m_session->GetOutputNameAllocated(i, allocator).get());
        }
    }
    catch (const Ort::Exception& exception)
    {
        m_loadError = tr("The voice model can't be loaded: %1").arg(QString::fromUtf8(exception.what()));
        *errorMessage = m_loadError;
        return false;
    }

    m_isLoaded = true;
    return true;
}

const QVector<float>* KokoroModel::getVoice(const QString& voiceId, QString* errorMessage)
{
    auto it = m_voices.constFind(voiceId);
    if (it != m_voices.constEnd())
    {
        return &it.value();
    }

    // 510 style vectors of 256 values (one for each length of the text)
    QFile file(QDir(m_directory).filePath(QStringLiteral("voices/%1.bin").arg(voiceId)));
    if (!file.open(QFile::ReadOnly) || file.size() != 510 * 256 * qint64(sizeof(float)))
    {
        *errorMessage = tr("The voice '%1' is missing.").arg(voiceId);
        return nullptr;
    }

    const QByteArray data = file.readAll();
    QVector<float> voice(510 * 256);
    std::memcpy(voice.data(), data.constData(), data.size());
    return &m_voices.insert(voiceId, voice).value();
}

QVector<float> KokoroModel::run(const std::vector<int64_t>& tokens, const QVector<float>& voice, float speed, QString* errorMessage)
{
    // The tokens are padded by 0 at both ends, the style is chosen by the length of the text
    std::vector<int64_t> input;
    input.reserve(tokens.size() + 2);
    input.push_back(0);
    input.insert(input.end(), tokens.begin(), tokens.end());
    input.push_back(0);

    const size_t styleIndex = std::min<size_t>(tokens.size(), 509);
    std::vector<float> style(voice.constData() + styleIndex * 256, voice.constData() + (styleIndex + 1) * 256);
    float speedValue = speed;
    int32_t speedInteger = int32_t(std::lround(speed));

    try
    {
        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 2> tokenShape = { 1, int64_t(input.size()) };
        const std::array<int64_t, 2> styleShape = { 1, 256 };
        const std::array<int64_t, 1> speedShape = { 1 };

        std::vector<Ort::Value> values;
        std::vector<const char*> inputNames;
        for (const std::string& name : m_inputNames)
        {
            const QByteArray inputName(name.c_str());
            if (inputName.contains("style"))
            {
                values.emplace_back(Ort::Value::CreateTensor<float>(memory, style.data(), style.size(), styleShape.data(), styleShape.size()));
            }
            else if (inputName.contains("speed"))
            {
                if (m_speedType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32)
                {
                    values.emplace_back(Ort::Value::CreateTensor<int32_t>(memory, &speedInteger, 1, speedShape.data(), speedShape.size()));
                }
                else
                {
                    values.emplace_back(Ort::Value::CreateTensor<float>(memory, &speedValue, 1, speedShape.data(), speedShape.size()));
                }
            }
            else
            {
                values.emplace_back(Ort::Value::CreateTensor<int64_t>(memory, input.data(), input.size(), tokenShape.data(), tokenShape.size()));
            }
            inputNames.push_back(name.c_str());
        }

        const char* outputName = m_outputNames.front().c_str();
        std::vector<Ort::Value> outputs = m_session->Run(Ort::RunOptions{ nullptr }, inputNames.data(), values.data(), values.size(), &outputName, 1);
        const float* audio = outputs.front().GetTensorData<float>();
        const size_t count = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();
        return QVector<float>(audio, audio + count);
    }
    catch (const Ort::Exception& exception)
    {
        *errorMessage = tr("The speech can't be created: %1").arg(QString::fromUtf8(exception.what()));
        return QVector<float>();
    }
}

QVector<float> KokoroModel::synthesize(const QString& text, const QString& voiceId, double speed, QString* errorMessage)
{
    QMutexLocker locker(&m_mutex);
    if (!load(errorMessage))
    {
        return QVector<float>();
    }

    const QVector<float>* voice = getVoice(voiceId, errorMessage);
    if (!voice)
    {
        return QVector<float>();
    }

    const QString phonemes = m_g2p.phonemize(text);
    std::vector<int64_t> tokens;
    for (uint codePoint : phonemes.toUcs4())
    {
        auto it = m_vocabulary.constFind(codePoint);
        if (it != m_vocabulary.constEnd())
        {
            tokens.push_back(it.value());
        }
    }

    // The model reads at most 510 tokens - a longer text is read in parts (split at a space)
    const int64_t spaceToken = m_vocabulary.value(uint(' '), -1);
    QVector<float> result;
    size_t start = 0;
    while (start < tokens.size())
    {
        size_t end = std::min(tokens.size(), start + 500);
        if (end < tokens.size())
        {
            for (size_t i = end; i > start + 100; --i)
            {
                if (tokens[i - 1] == spaceToken)
                {
                    end = i;
                    break;
                }
            }
        }

        const std::vector<int64_t> part(tokens.begin() + start, tokens.begin() + end);
        const QVector<float> audio = run(part, *voice, float(speed), errorMessage);
        if (audio.isEmpty() && !errorMessage->isEmpty())
        {
            return QVector<float>();
        }
        result += audio;
        start = end;
    }
    return result;
}

}   // namespace

/// PDF Fire: the samples waiting to be played. The audio output reads them; while
/// the next sentence is still being created, it reads silence (so it doesn't stop).
class PDFFireSpeechAudioBuffer : public QIODevice
{
public:
    explicit PDFFireSpeechAudioBuffer(QObject* parent) : QIODevice(parent) { }

    void append(const QByteArray& data)
    {
        QMutexLocker locker(&m_mutex);
        m_data.append(data);
    }

    void clear()
    {
        QMutexLocker locker(&m_mutex);
        m_data.clear();
        m_position = 0;
        m_isFinished = false;
    }

    void setFinished(bool finished)
    {
        QMutexLocker locker(&m_mutex);
        m_isFinished = finished;
    }

    bool isDrained() const
    {
        QMutexLocker locker(&m_mutex);
        return m_isFinished && m_position >= m_data.size();
    }

    virtual bool isSequential() const override { return true; }
    virtual qint64 bytesAvailable() const override
    {
        QMutexLocker locker(&m_mutex);
        return (m_data.size() - m_position) + QIODevice::bytesAvailable() + (m_isFinished ? 0 : 4800);
    }

protected:
    virtual qint64 readData(char* data, qint64 maxSize) override
    {
        QMutexLocker locker(&m_mutex);
        const qint64 available = m_data.size() - m_position;
        if (available > 0)
        {
            const qint64 count = std::min(available, maxSize) & ~qint64(1);
            std::memcpy(data, m_data.constData() + m_position, count);
            m_position += count;
            if (m_position > 1 << 20)
            {
                // The played samples are dropped from time to time
                m_data.remove(0, m_position);
                m_position = 0;
            }
            return count;
        }
        if (!m_isFinished)
        {
            // Silence, until the next sentence is ready
            const qint64 count = std::min<qint64>(maxSize, 960) & ~qint64(1);
            std::memset(data, 0, count);
            return count;
        }
        return 0;
    }

    virtual qint64 writeData(const char*, qint64) override { return -1; }

private:
    mutable QMutex m_mutex;
    QByteArray m_data;
    qint64 m_position = 0;
    bool m_isFinished = false;
};

PDFFireNaturalSpeech::PDFFireNaturalSpeech(QObject* parent) :
    QObject(parent),
    m_generation(std::make_shared<std::atomic<quint64>>(0))
{
    m_audioBuffer = new PDFFireSpeechAudioBuffer(this);
}

PDFFireNaturalSpeech::~PDFFireNaturalSpeech()
{
    // A running synthesis stops at the next sentence, its results are dropped; it is
    // waited for, so it never reports to a destroyed object
    ++(*m_generation);
    for (QFuture<void>& future : m_futures)
    {
        future.waitForFinished();
    }
    if (m_audioSink)
    {
        m_audioSink->stop();
    }
}

QList<PDFFireNaturalSpeech::Voice> PDFFireNaturalSpeech::getVoices()
{
    const QLocale us(QLocale::English, QLocale::UnitedStates);
    const QLocale uk(QLocale::English, QLocale::UnitedKingdom);
    return {
        { QStringLiteral("af_heart"), tr("Heart (female)"), us },
        { QStringLiteral("af_bella"), tr("Bella (female)"), us },
        { QStringLiteral("af_nicole"), tr("Nicole (female, soft)"), us },
        { QStringLiteral("am_michael"), tr("Michael (male)"), us },
        { QStringLiteral("am_fenrir"), tr("Fenrir (male)"), us },
        { QStringLiteral("am_puck"), tr("Puck (male)"), us },
        { QStringLiteral("bf_emma"), tr("Emma (female, British)"), uk }
    };
}

QList<QLocale> PDFFireNaturalSpeech::getLocales()
{
    return { QLocale(QLocale::English, QLocale::UnitedStates), QLocale(QLocale::English, QLocale::UnitedKingdom) };
}

QString PDFFireNaturalSpeech::getVoiceDirectory()
{
    QStringList candidates;
    if (qEnvironmentVariableIsSet("PDFFIRE_VOICE_DIR"))
    {
        candidates << qEnvironmentVariable("PDFFIRE_VOICE_DIR");
    }
    candidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("voice"))   // Windows: next to the program
               << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../share/pdf-fire/voice"));
#ifdef PDFFIRE_DEV_VOICE_DIR
    candidates << QStringLiteral(PDFFIRE_DEV_VOICE_DIR);
#endif

    for (const QString& candidate : candidates)
    {
        if (QFile::exists(QDir(candidate).filePath(QStringLiteral("kokoro.onnx"))))
        {
            return QDir(candidate).absolutePath();
        }
    }
    return QString();
}

QVector<float> PDFFireNaturalSpeech::synthesize(const QString& text, const QString& voiceId, double speed, QString* errorMessage)
{
    return KokoroModel::instance().synthesize(text, voiceId, speed, errorMessage);
}

bool PDFFireNaturalSpeech::saveWav(const QString& fileName, const QVector<float>& samples)
{
    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    const quint32 dataSize = quint32(samples.size() * 2);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + dataSize);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(SAMPLE_RATE) << quint32(SAMPLE_RATE * 2) << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << dataSize;
    for (float sample : samples)
    {
        stream << qint16(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
    }
    return stream.status() == QDataStream::Ok;
}

void PDFFireNaturalSpeech::setState(State state)
{
    if (m_state != state)
    {
        m_state = state;
        Q_EMIT stateChanged(state);
    }
}

void PDFFireNaturalSpeech::say(const QString& text)
{
    stop();

    if (getVoiceDirectory().isEmpty())
    {
        m_errorString = tr("The natural voices are not installed (the folder of the voices was not found).");
        setState(Error);
        Q_EMIT errorOccurred(m_errorString);
        return;
    }

    const quint64 generation = ++(*m_generation);
    m_isGenerationFinished = false;
    m_audioBuffer->clear();

    if (!m_audioSink)
    {
        QAudioFormat format;
        format.setSampleRate(SAMPLE_RATE);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this);
        connect(m_audioSink, &QAudioSink::stateChanged, this, [this](QAudio::State state)
        {
            if (state == QAudio::IdleState)
            {
                onAudioIdle();
            }
        });
    }
    // The tests of the program run without sound (PDFFIRE_SPEECH_MUTE)
    m_audioSink->setVolume(qEnvironmentVariableIsSet("PDFFIRE_SPEECH_MUTE") ? 0.0 : m_volume);
    if (!m_audioBuffer->isOpen())
    {
        m_audioBuffer->open(QIODevice::ReadOnly);
    }
    m_audioSink->start(m_audioBuffer);
    setState(Speaking);

    // The sentences are created one by one in the background, each one is played,
    // as soon as it is ready
    const QStringList sentences = PDFFireG2P::splitSentences(text);
    const QString voiceId = m_voiceId;
    const double speed = m_speed;
    std::shared_ptr<std::atomic<quint64>> currentGeneration = m_generation;
    PDFFireNaturalSpeech* self = this;
    m_futures.erase(std::remove_if(m_futures.begin(), m_futures.end(), [](const QFuture<void>& future) { return future.isFinished(); }), m_futures.end());
    m_futures.push_back(QtConcurrent::run([self, sentences, voiceId, speed, currentGeneration, generation]()
    {
        for (const QString& sentence : sentences)
        {
            if (currentGeneration->load() != generation)
            {
                return;
            }

            QString errorMessage;
            const QVector<float> samples = PDFFireNaturalSpeech::synthesize(sentence, voiceId, speed, &errorMessage);
            if (currentGeneration->load() != generation)
            {
                return;
            }
            if (samples.isEmpty() && !errorMessage.isEmpty())
            {
                QMetaObject::invokeMethod(self, [self, errorMessage, generation]() { self->onGenerationFailed(errorMessage, generation); }, Qt::QueuedConnection);
                return;
            }

            // 16 bit samples, a short pause after the sentence
            QByteArray data((samples.size() + SAMPLE_RATE / 12) * 2, Qt::Uninitialized);
            qint16* output = reinterpret_cast<qint16*>(data.data());
            for (qsizetype i = 0; i < samples.size(); ++i)
            {
                output[i] = qint16(std::lround(std::clamp(samples[i], -1.0f, 1.0f) * 32767.0f));
            }
            std::fill(output + samples.size(), output + samples.size() + SAMPLE_RATE / 12, qint16(0));
            QMetaObject::invokeMethod(self, [self, data, generation]() { self->onAudioReady(data, generation); }, Qt::QueuedConnection);
        }
        if (currentGeneration->load() == generation)
        {
            QMetaObject::invokeMethod(self, [self, generation]() { self->onGenerationFinished(generation); }, Qt::QueuedConnection);
        }
    }));
}

void PDFFireNaturalSpeech::onAudioReady(const QByteArray& samples, quint64 generation)
{
    if (generation == m_generation->load())
    {
        m_audioBuffer->append(samples);
    }
}

void PDFFireNaturalSpeech::onGenerationFinished(quint64 generation)
{
    if (generation == m_generation->load())
    {
        m_isGenerationFinished = true;
        m_audioBuffer->setFinished(true);
    }
}

void PDFFireNaturalSpeech::onGenerationFailed(const QString& errorMessage, quint64 generation)
{
    if (generation != m_generation->load())
    {
        return;
    }

    stop();
    m_errorString = errorMessage;
    setState(Error);
    Q_EMIT errorOccurred(errorMessage);
}

void PDFFireNaturalSpeech::onAudioIdle()
{
    // Everything was created and played - the text is read
    if (m_state == Speaking && m_isGenerationFinished && m_audioBuffer->isDrained())
    {
        m_audioSink->stop();
        setState(Ready);
    }
}

void PDFFireNaturalSpeech::stop()
{
    ++(*m_generation);
    if (m_audioSink)
    {
        m_audioSink->stop();
    }
    m_audioBuffer->clear();
    m_isGenerationFinished = false;
    if (m_state == Speaking || m_state == Paused)
    {
        setState(Ready);
    }
}

void PDFFireNaturalSpeech::pause()
{
    if (m_state == Speaking && m_audioSink)
    {
        m_audioSink->suspend();
        setState(Paused);
    }
}

void PDFFireNaturalSpeech::resume()
{
    if (m_state == Paused && m_audioSink)
    {
        m_audioSink->resume();
        setState(Speaking);
    }
}

void PDFFireNaturalSpeech::setVoice(const QString& voiceId)
{
    m_voiceId = voiceId;
}

void PDFFireNaturalSpeech::setRate(double rate)
{
    // -1..1 -> 0.6..1.5 times the normal speed
    m_speed = rate < 0 ? 1.0 + 0.4 * rate : 1.0 + 0.5 * rate;
}

void PDFFireNaturalSpeech::setVolume(double volume)
{
    m_volume = qBound(0.0, volume, 1.0);
    if (m_audioSink)
    {
        m_audioSink->setVolume(m_volume);
    }
}

}   // namespace pdfviewer
