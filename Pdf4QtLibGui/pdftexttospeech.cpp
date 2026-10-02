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

#include "pdftexttospeech.h"
#include "pdfviewersettings.h"
#include "pdfdrawspacecontroller.h"
#include "pdfcompiler.h"
#include "pdfdrawwidget.h"

#include <QLabel>
#include <QAction>
#include <QSlider>
#include <QComboBox>
#include <QToolButton>
#include <QTextBrowser>
#include "pdffirenaturalspeech.h"

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFTextToSpeech::PDFTextToSpeech(QObject* parent) :
    BaseClass(parent),
    m_textToSpeech(nullptr),
    m_document(nullptr),
    m_proxy(nullptr),
    m_state(Invalid),
    m_initialized(false),
    m_engineListsInitialized(false),
    m_speechLocaleComboBox(nullptr),
    m_speechVoiceComboBox(nullptr),
    m_speechRateEdit(nullptr),
    m_speechVolumeEdit(nullptr),
    m_speechPitchEdit(nullptr),
    m_speechPlayButton(nullptr),
    m_speechPauseButton(nullptr),
    m_speechStopButton(nullptr),
    m_speechSynchronizeButton(nullptr),
    m_speechRateValueLabel(nullptr),
    m_speechPitchValueLabel(nullptr),
    m_speechVolumeValueLabel(nullptr),
    m_speechActualTextBrowser(nullptr)
{

}

bool PDFTextToSpeech::isValid() const
{
    return m_document != nullptr;
}

void PDFTextToSpeech::setDocument(const pdf::PDFModifiedDocument& document)
{
    if (m_document != document)
    {
        stop();
        m_document = document;

        if (m_textToSpeech)
        {
            m_state = m_document ? Ready : NoDocument;
        }
        else
        {
            // Set state to invalid, speech engine is not set
            m_state = Invalid;
        }

        updateUI();
    }
}

void PDFTextToSpeech::updateVoices()
{
    // PDF Fire: the natural voices of the chosen language
    const QString locale = m_speechLocaleComboBox->currentData().toString();
    m_speechVoiceComboBox->setUpdatesEnabled(false);
    m_speechVoiceComboBox->clear();
    for (const PDFFireNaturalSpeech::Voice& voice : PDFFireNaturalSpeech::getVoices())
    {
        if (locale.isEmpty() || voice.locale.name() == locale)
        {
            m_speechVoiceComboBox->addItem(voice.name, voice.id);
        }
    }
    m_speechVoiceComboBox->setUpdatesEnabled(true);
}

void PDFTextToSpeech::setSettings(const PDFViewerSettings* viewerSettings)
{
    Q_ASSERT(viewerSettings);

    if (!m_initialized)
    {
        // This object is not initialized yet
        return;
    }

    // First, stop the engine
    stop();

    delete m_textToSpeech;
    m_textToSpeech = nullptr;

    m_engineListsInitialized = false;
    m_engineErrorMessage = QString();

    const PDFViewerSettings::Settings& settings = viewerSettings->getSettings();

    // PDF Fire: the natural voices (Kokoro) are the only speech engine - they come with
    // PDF Fire and work offline
    m_requestedLocale = settings.m_speechLocale;
    m_requestedVoice = settings.m_speechVoice;

    m_textToSpeech = new PDFFireNaturalSpeech(this);
    connect(m_textToSpeech, &PDFFireNaturalSpeech::stateChanged, this, &PDFTextToSpeech::onEngineStateChanged);
    connect(m_textToSpeech, &PDFFireNaturalSpeech::errorOccurred, this, &PDFTextToSpeech::onEngineError);

    if (!PDFFireNaturalSpeech::getVoiceDirectory().isEmpty())
    {
        m_state = m_document ? Ready : NoDocument;
        onEngineStateChanged();
    }
    else
    {
        m_speechLocaleComboBox->clear();
        m_speechVoiceComboBox->clear();
        onEngineError(tr("The natural voices of PDF Fire are not installed."));
    }

    if (m_textToSpeech)
    {
        setRate(settings.m_speechRate);
        setPitch(settings.m_speechPitch);
        setVolume(settings.m_speechVolume);
    }

    updateUI();
}

void PDFTextToSpeech::updateEngineLists()
{
    Q_ASSERT(m_textToSpeech);

    const QList<QLocale> locales = PDFFireNaturalSpeech::getLocales();
    m_speechLocaleComboBox->setUpdatesEnabled(false);
    m_speechLocaleComboBox->clear();
    for (const QLocale& locale : locales)
    {
        m_speechLocaleComboBox->addItem(QString("%1 (%2)").arg(locale.nativeLanguageName(), locale.nativeTerritoryName()), locale.name());
    }
    m_speechLocaleComboBox->setUpdatesEnabled(true);

    setLocale(m_requestedLocale);
    setVoice(m_requestedVoice);
}

void PDFTextToSpeech::onEngineStateChanged()
{
    if (!m_textToSpeech)
    {
        return;
    }

    const PDFFireNaturalSpeech::State state = m_textToSpeech->state();

    if (state == PDFFireNaturalSpeech::Error)
    {
        onEngineError(m_textToSpeech->errorString());
        return;
    }

    if (!m_engineListsInitialized && state == PDFFireNaturalSpeech::Ready)
    {
        m_engineListsInitialized = true;
        m_engineErrorMessage = QString();
        updateEngineLists();
        updateUI();
    }

    updatePlay();
}

void PDFTextToSpeech::onEngineError(const QString& errorString)
{
    // Jakub Melka: Speech engine failed - for example, it was not possible to connect
    // to the speech daemon. We remember the reason, so it can be displayed to the user.
    m_engineErrorMessage = !errorString.isEmpty() ? errorString : tr("Unknown error of the speech engine.");
    m_state = Error;
    updateUI();
}

void PDFTextToSpeech::setProxy(pdf::PDFDrawWidgetProxy* proxy)
{
    m_proxy = proxy;
    pdf::PDFAsynchronousTextLayoutCompiler* compiler = m_proxy->getTextLayoutCompiler();
    connect(compiler, &pdf::PDFAsynchronousTextLayoutCompiler::textLayoutChanged, this, &PDFTextToSpeech::updatePlay);
}

void PDFTextToSpeech::initializeUI(QComboBox* speechLocaleComboBox,
                                   QComboBox* speechVoiceComboBox,
                                   QSlider* speechRateEdit,
                                   QSlider* speechPitchEdit,
                                   QSlider* speechVolumeEdit,
                                   QToolButton* speechPlayButton,
                                   QToolButton* speechPauseButton,
                                   QToolButton* speechStopButton,
                                   QToolButton* speechSynchronizeButton,
                                   QLabel* speechRateValueLabel,
                                   QLabel* speechPitchValueLabel,
                                   QLabel* speechVolumeValueLabel,
                                   QTextBrowser* speechActualTextBrowser)
{
    Q_ASSERT(speechLocaleComboBox);
    Q_ASSERT(speechVoiceComboBox);
    Q_ASSERT(speechRateEdit);
    Q_ASSERT(speechVolumeEdit);
    Q_ASSERT(speechPitchEdit);
    Q_ASSERT(speechPlayButton);
    Q_ASSERT(speechPauseButton);
    Q_ASSERT(speechStopButton);
    Q_ASSERT(speechSynchronizeButton);

    m_speechLocaleComboBox = speechLocaleComboBox;
    m_speechVoiceComboBox = speechVoiceComboBox;
    m_speechRateEdit = speechRateEdit;
    m_speechVolumeEdit = speechVolumeEdit;
    m_speechPitchEdit = speechPitchEdit;
    m_speechPlayButton = speechPlayButton;
    m_speechPauseButton = speechPauseButton;
    m_speechStopButton = speechStopButton;
    m_speechSynchronizeButton = speechSynchronizeButton;
    m_speechRateValueLabel = speechRateValueLabel;
    m_speechPitchValueLabel = speechPitchValueLabel;
    m_speechVolumeValueLabel = speechVolumeValueLabel;
    m_speechActualTextBrowser = speechActualTextBrowser;

    connect(m_speechLocaleComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFTextToSpeech::onLocaleChanged);
    connect(m_speechVoiceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFTextToSpeech::onVoiceChanged);
    connect(m_speechRateEdit, &QSlider::valueChanged, this, &PDFTextToSpeech::onRateChanged);
    connect(m_speechPitchEdit, &QSlider::valueChanged, this, &PDFTextToSpeech::onPitchChanged);
    connect(m_speechVolumeEdit, &QSlider::valueChanged, this, &PDFTextToSpeech::onVolumeChanged);
    connect(m_speechPlayButton, &QToolButton::clicked, this, &PDFTextToSpeech::onPlayClicked);
    connect(m_speechPauseButton, &QToolButton::clicked, this, &PDFTextToSpeech::onPauseClicked);
    connect(m_speechStopButton, &QToolButton::clicked, this, &PDFTextToSpeech::onStopClicked);

    m_initialized = true;
}

void PDFTextToSpeech::updateUI()
{
    bool enableControls = false;
    bool enablePlay = false;
    bool enablePause = false;
    bool enableStop = false;

    switch (m_state)
    {
        case pdfviewer::PDFTextToSpeech::Invalid:
        {
            enableControls = false;
            enablePlay = false;
            enablePause = false;
            enableStop = false;
            break;
        }

        case pdfviewer::PDFTextToSpeech::NoDocument:
        {
            enableControls = true;
            enablePlay = false;
            enablePause = false;
            enableStop = false;
            break;
        }

        case pdfviewer::PDFTextToSpeech::Ready:
        {
            enableControls = true;
            enablePlay = true;
            enablePause = false;
            enableStop = false;
            break;
        }

        case pdfviewer::PDFTextToSpeech::Playing:
        {
            enableControls = true;
            enablePlay = false;
            enablePause = true;
            enableStop = true;
            break;
        }

        case pdfviewer::PDFTextToSpeech::Paused:
        {
            enableControls = true;
            enablePlay = true;
            enablePause = false;
            enableStop = true;
            break;
        }

        case pdfviewer::PDFTextToSpeech::Error:
        {
            enableControls = false;
            enablePlay = false;
            enablePause = false;
            enableStop = false;
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    m_speechLocaleComboBox->setEnabled(enableControls && m_speechLocaleComboBox->count() > 0);
    m_speechVoiceComboBox->setEnabled(enableControls && m_speechVoiceComboBox->count() > 0);
    m_speechRateEdit->setEnabled(enableControls);
    m_speechVolumeEdit->setEnabled(enableControls);
    // PDF Fire: the natural voices have their own pitch - it can't be changed
    m_speechPitchEdit->setEnabled(false);
    m_speechPitchEdit->setToolTip(tr("The natural voices have their own pitch."));
    // PDF Fire: the play button lights up (the accent colour of a checked button) while
    // the document is read, the pause button while it is paused - a greyed-out play
    // button looked like nothing was happening
    m_speechPlayButton->setCheckable(true);
    m_speechPlayButton->setChecked(m_state == Playing);
    m_speechPlayButton->setEnabled(enablePlay || m_state == Playing);
    m_speechPlayButton->setToolTip(m_state == Playing ? tr("Reading aloud...") : (m_state == Paused ? tr("Continue reading") : tr("Read aloud from the page shown")));
    m_speechPauseButton->setCheckable(true);
    m_speechPauseButton->setChecked(m_state == Paused);
    m_speechPauseButton->setEnabled(enablePause || m_state == Paused);
    m_speechPauseButton->setToolTip(tr("Pause"));
    m_speechStopButton->setToolTip(tr("Stop"));
    m_speechSynchronizeButton->setToolTip(tr("Follow the reading - the pages turn with the voice"));
    m_speechStopButton->setEnabled(enableStop);
    m_speechSynchronizeButton->setEnabled(enableControls);
}

void PDFTextToSpeech::stop()
{
    switch (m_state)
    {
        case Playing:
        case Paused:
        {
            // PDF Fire: the reader stops first - the engine reports, that it is ready,
            // and the reader must not continue with the next text then
            m_state = Ready;
            m_textToSpeech->stop();
            m_currentTextFlowIndex = 0;
            m_currentPage = 0;
            m_currentTextLayout = pdf::PDFTextLayout();
            m_textFlows = pdf::PDFTextFlows();
            m_state = Ready;
            break;
        }

        default:
            break;
    }

    updateUI();
}

void PDFTextToSpeech::setLocale(const QString& locale)
{
    // Jakub Melka: If the locale is not set in the settings, or it is not supported
    // by the engine, then we use the locale of the system, and if even this one is
    // not supported, then the first supported locale is used. Empty locale would mean
    // 'C' locale, for which the engine usually has no voice at all.
    int index = m_speechLocaleComboBox->findData(locale);

    if (index == -1)
    {
        index = m_speechLocaleComboBox->findData(QLocale::system().name());
    }

    if (index == -1 && m_speechLocaleComboBox->count() > 0)
    {
        index = 0;
    }

    m_speechLocaleComboBox->setCurrentIndex(index);

    if (index != -1)
    {
        // Jakub Melka: Signal 'currentIndexChanged' is not emitted, if the index
        // is not changed - so we must update the engine/voices explicitly.
        onLocaleChanged();
    }
    else
    {
        updateVoices();
    }
}

void PDFTextToSpeech::setVoice(const QString& voice)
{
    int index = m_speechVoiceComboBox->findData(voice);

    if (index == -1 && m_speechVoiceComboBox->count() > 0)
    {
        index = 0;
    }

    m_speechVoiceComboBox->setCurrentIndex(index);

    if (index != -1)
    {
        onVoiceChanged();
    }
}

void PDFTextToSpeech::setRate(const double rate)
{
    pdf::PDFLinearInterpolation<double> interpolation(-1.0, 1.0, m_speechRateEdit->minimum(), m_speechRateEdit->maximum());
    m_speechRateEdit->setValue(qRound(interpolation(rate)));
    onRateChanged(m_speechRateEdit->value());
}

void PDFTextToSpeech::setPitch(const double pitch)
{
    pdf::PDFLinearInterpolation<double> interpolation(-1.0, 1.0, m_speechPitchEdit->minimum(), m_speechPitchEdit->maximum());
    m_speechPitchEdit->setValue(qRound(interpolation(pitch)));
    onPitchChanged(m_speechPitchEdit->value());
}

void PDFTextToSpeech::setVolume(const double volume)
{
    pdf::PDFLinearInterpolation<double> interpolation(0.0, 1.0, m_speechVolumeEdit->minimum(), m_speechVolumeEdit->maximum());
    m_speechVolumeEdit->setValue(qRound(interpolation(volume)));
    onVolumeChanged(m_speechVolumeEdit->value());
}

void PDFTextToSpeech::onLocaleChanged()
{
    if (m_textToSpeech)
    {
        updateVoices();

        if (m_speechVoiceComboBox->currentIndex() == -1)
        {
            m_speechVoiceComboBox->setCurrentIndex(0);
        }
    }
}

void PDFTextToSpeech::onVoiceChanged()
{
    if (m_textToSpeech)
    {
        const QString voice = m_speechVoiceComboBox->currentData().toString();
        if (!voice.isEmpty())
        {
            m_textToSpeech->setVoice(voice);
        }
    }
}

void PDFTextToSpeech::onRateChanged(int rate)
{
    if (m_textToSpeech)
    {
        pdf::PDFLinearInterpolation<double> interpolation(m_speechRateEdit->minimum(), m_speechRateEdit->maximum(), -1.0, 1.0);
        double value = interpolation(rate);
        m_textToSpeech->setRate(value);
        m_speechRateValueLabel->setText(QString::number(value, 'f', 2));
    }
}

void PDFTextToSpeech::onPitchChanged(int pitch)
{
    if (m_textToSpeech)
    {
        pdf::PDFLinearInterpolation<double> interpolation(m_speechPitchEdit->minimum(), m_speechPitchEdit->maximum(), -1.0, 1.0);
        double value = interpolation(pitch);
        m_speechPitchValueLabel->setText(QString::number(value, 'f', 2));
    }
}

void PDFTextToSpeech::onVolumeChanged(int volume)
{
    if (m_textToSpeech)
    {
        pdf::PDFLinearInterpolation<double> interpolation(m_speechVolumeEdit->minimum(), m_speechVolumeEdit->maximum(), 0.0, 1.0);
        double value = interpolation(volume);
        m_textToSpeech->setVolume(value);
        m_speechVolumeValueLabel->setText(QString::number(value, 'f', 2));
    }
}

void PDFTextToSpeech::onPlayClicked()
{
    switch (m_state)
    {
        case Paused:
        {
            m_state = Playing;
            m_textToSpeech->resume();
            if (m_textToSpeech->state() == PDFFireNaturalSpeech::Ready)
            {
                updatePlay();
            }
            break;
        }

        case Ready:
        {
            m_state = Playing;
            m_currentTextFlowIndex = std::numeric_limits<size_t>::max();
            m_currentPage = -1;
            updatePlay();
            break;
        }

        default:
            break;
    }

    updateUI();
}

void PDFTextToSpeech::onPauseClicked()
{
    if (m_state == Paused)
    {
        // The lit pause button pressed again - it continues, as play does
        onPlayClicked();
        return;
    }

    if (m_state == Playing)
    {
        m_state = Paused;
        m_textToSpeech->pause();
        updateUI();
    }
}

void PDFTextToSpeech::onStopClicked()
{
    stop();
}

void PDFTextToSpeech::updatePlay()
{
    if (m_state != Playing)
    {
        return;
    }

    Q_ASSERT(m_proxy);
    Q_ASSERT(m_document);

    // Jakub Melka: Check, if we have text layout. If not, then create it and return immediately.
    // Otherwise, check, if we have something to say.
    pdf::PDFAsynchronousTextLayoutCompiler* compiler = m_proxy->getTextLayoutCompiler();
    if (!compiler->isTextLayoutReady())
    {
        compiler->makeTextLayout();
        return;
    }

    PDFFireNaturalSpeech::State state = m_textToSpeech->state();
    if (state == PDFFireNaturalSpeech::Ready)
    {
        if (m_currentPage == -1)
        {
            // Handle starting of document reading
            std::vector<pdf::PDFInteger> currentPages = m_proxy->getWidget()->getDrawWidget()->getCurrentPages();
            if (!currentPages.empty())
            {
                updateToNextPage(currentPages.front());
            }
        }
        else if (++m_currentTextFlowIndex >= m_textFlows.size())
        {
            // Handle transition to next page
            updateToNextPage(m_currentPage + 1);
        }

        if (m_currentTextFlowIndex < m_textFlows.size())
        {
            // Say next thing
            const pdf::PDFTextFlow& textFlow = m_textFlows[m_currentTextFlowIndex];
            QString text = textFlow.getText();
            if (qEnvironmentVariableIsSet("PDFFIRE_DEBUG_SPEECH"))
            {
                qInfo("PDFFIRE_SPEECH page %lld flow %zu/%zu: %s", static_cast<long long>(m_currentPage), m_currentTextFlowIndex + 1, m_textFlows.size(), qPrintable(text.left(60)));
            }
            m_textToSpeech->say(text);
            m_speechActualTextBrowser->setText(text);
        }
        else
        {
            // We are finished the reading
            if (qEnvironmentVariableIsSet("PDFFIRE_DEBUG_SPEECH"))
            {
                qInfo("PDFFIRE_SPEECH finished at page %lld (flows %zu)", static_cast<long long>(m_currentPage), m_textFlows.size());
            }
            m_state = Ready;
        }
    }
    else if (state == PDFFireNaturalSpeech::Error)
    {
        onEngineError(m_textToSpeech->errorString());
    }

    updateUI();
}

/// PDF Fire: how many blocks at the top and at the bottom of a page can be a header / footer
static constexpr size_t PAGE_FURNITURE_EDGE = 3;

/// PDF Fire: the text of a header / footer without the numbers and the separators
/// ("ProRock Fire Association | 1" -> "prorock fire association")
static QString normalizePageFurniture(const QString& text)
{
    QString result;
    for (const QChar character : text.toLower())
    {
        if (character.isLetter())
        {
            result += character;
        }
        else if (character.isSpace() && !result.isEmpty() && !result.endsWith(QChar(' ')))
        {
            result += QChar(' ');
        }
    }
    return result.trimmed();
}

void PDFTextToSpeech::removePageFurniture(pdf::PDFTextFlows& flows, pdf::PDFInteger pageIndex, pdf::PDFInteger pageCount) const
{
    // PDF Fire: the running headers and footers and the page numbers are not read - a
    // footer like "ProRock Fire Association | 1" sounded like the document starting over.
    // A block is page furniture, when it is one of the first (or last) blocks of the page
    // and the neighbouring page has the same text there (numbers ignored), or when it
    // is only a page number ("1", "| 1", "Page 2 of 3").
    if (flows.empty())
    {
        return;
    }

    pdf::PDFAsynchronousTextLayoutCompiler* compiler = m_proxy->getTextLayoutCompiler();
    auto edgeTexts = [compiler, pageCount](pdf::PDFInteger neighbourIndex, bool isTop) -> QStringList
    {
        QStringList texts;
        if (neighbourIndex < 0 || neighbourIndex >= pageCount)
        {
            return texts;
        }
        pdf::PDFTextLayout layout = compiler->getTextLayout(neighbourIndex);
        const pdf::PDFTextFlows neighbourFlows = pdf::PDFTextFlow::createTextFlows(layout, pdf::PDFTextFlow::SeparateBlocks | pdf::PDFTextFlow::RemoveSoftHyphen, neighbourIndex);
        const size_t count = std::min(PAGE_FURNITURE_EDGE, neighbourFlows.size());
        for (size_t i = 0; i < count; ++i)
        {
            const pdf::PDFTextFlow& flow = isTop ? neighbourFlows[i] : neighbourFlows[neighbourFlows.size() - 1 - i];
            texts << normalizePageFurniture(flow.getText());
        }
        return texts;
    };

    const QStringList topTexts = edgeTexts(pageIndex - 1, true) + edgeTexts(pageIndex + 1, true);
    const QStringList bottomTexts = edgeTexts(pageIndex - 1, false) + edgeTexts(pageIndex + 1, false);
    static const QStringList pageWords = { QString(), QStringLiteral("page"), QStringLiteral("page of"), QStringLiteral("of") };

    pdf::PDFTextFlows kept;
    for (size_t i = 0; i < flows.size(); ++i)
    {
        const QString text = flows[i].getText().simplified();
        const QString normalized = normalizePageFurniture(text);
        const bool isTop = i < PAGE_FURNITURE_EDGE;
        const bool isBottom = i + PAGE_FURNITURE_EDGE >= flows.size();
        bool isFurniture = false;

        if ((isTop || isBottom) && text.size() <= 24 && pageWords.contains(normalized))
        {
            // Only a page number
            isFurniture = true;
        }
        else if (text.size() <= 120 && !normalized.isEmpty())
        {
            isFurniture = (isTop && topTexts.contains(normalized)) || (isBottom && bottomTexts.contains(normalized));
        }

        if (!isFurniture)
        {
            kept.push_back(flows[i]);
        }
        else if (qEnvironmentVariableIsSet("PDFFIRE_DEBUG_SPEECH"))
        {
            qInfo("PDFFIRE_SPEECH page %lld skips header/footer: %s", static_cast<long long>(pageIndex), qPrintable(text.left(60)));
        }
    }
    flows = std::move(kept);
}

void PDFTextToSpeech::updateToNextPage(pdf::PDFInteger pageIndex)
{
    Q_ASSERT(m_document);
    Q_ASSERT(m_state = Playing);

    m_currentPage = pageIndex;
    const pdf::PDFInteger pageCount = m_document->getCatalog()->getPageCount();

    pdf::PDFAsynchronousTextLayoutCompiler* compiler = m_proxy->getTextLayoutCompiler();
    Q_ASSERT(compiler->isTextLayoutReady());

    m_currentTextLayout = pdf::PDFTextLayout();
    m_textFlows.clear();
    m_speechActualTextBrowser->clear();

    // Find first nonempty page
    while (m_currentPage < pageCount)
    {
        m_currentTextLayout = compiler->getTextLayout(m_currentPage);
        m_textFlows = pdf::PDFTextFlow::createTextFlows(m_currentTextLayout, pdf::PDFTextFlow::SeparateBlocks | pdf::PDFTextFlow::RemoveSoftHyphen, m_currentPage);
        removePageFurniture(m_textFlows, m_currentPage, pageCount);

        if (!m_textFlows.empty())
        {
            break;
        }

        ++m_currentPage;
    }

    if (m_currentPage < pageCount && m_speechSynchronizeButton->isChecked())
    {
        m_proxy->goToPage(m_currentPage);
    }

    m_currentTextFlowIndex = 0;
}

}   // namespace pdfviewer
