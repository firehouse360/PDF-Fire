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

#ifndef PDFFIREG2P_H
#define PDFFIREG2P_H

#include "pdfviewerglobal.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

namespace pdfviewer
{

/// PDF Fire: English text to phonemes (grapheme to phoneme) for the natural voices.
/// It is a C++ port of the English G2P of misaki (Apache 2.0, the companion of the
/// Kokoro voices, https://github.com/hexgrad/misaki), without its part of speech
/// tagger and its neural fallback: the words are found in the pronunciation
/// dictionaries of misaki (gold and silver), with the rules for the endings -s,
/// -ed, -ing, numbers, abbreviations and the stress. A word, which is not known,
/// is split into known parts, or spelled.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFFireG2P
{
public:
    /// Loads the dictionaries (us_gold.json, us_silver.json) from the directory
    bool load(const QString& directory, QString* errorMessage);

    bool isLoaded() const { return !m_golds.isEmpty(); }

    /// Converts the text to the phonemes (the alphabet of the Kokoro model)
    /// \param text Text (a sentence or a paragraph)
    /// \param unknownWords Words, which are not in the dictionaries (they were split or spelled)
    QString phonemize(const QString& text, QStringList* unknownWords = nullptr) const;

    /// Splits the text into sentences (the model reads at most 510 phonemes at once)
    static QStringList splitSentences(const QString& text);

private:
    struct Entry
    {
        QString defaultPhonemes;
        std::optional<QString> noneContextPhonemes;    ///< Pronunciation at the end of a sentence
        bool isValid() const { return !defaultPhonemes.isEmpty(); }
    };

    struct Context
    {
        std::optional<bool> futureVowel;    ///< The next word starts with a vowel (none = the end)
        bool futureTo = false;
    };

    struct Token
    {
        QString text;
        QString phonemes;
        bool isPunctuation = false;
        bool isSpelled = false;     ///< An abbreviation with periods (U.S.) - spelled
        bool hasWhitespace = false; ///< Whitespace after the token
        bool hasPhonemes = false;
    };

    std::optional<QString> lookup(QString word, std::optional<double> stress, const Context& context, int* rating) const;
    std::optional<QString> getWord(QString word, std::optional<double> stress, const Context& context) const;
    std::optional<QString> getSpecialCase(const QString& word, std::optional<double> stress, const Context& context) const;
    std::optional<QString> getNNP(const QString& word) const;
    std::optional<QString> stemS(const QString& word, std::optional<double> stress, const Context& context) const;
    std::optional<QString> stemEd(const QString& word, std::optional<double> stress, const Context& context) const;
    std::optional<QString> stemIng(const QString& word, std::optional<double> stress, const Context& context) const;
    std::optional<QString> getNumber(QString word) const;
    std::optional<QString> getUnknownWord(const QString& word, const Context& context) const;
    QString lookupWords(const QString& words) const;
    bool isKnown(const QString& word) const;
    const Entry* findEntry(const QHash<QString, Entry>& dictionary, const QString& word) const;

    static QString applyStress(QString phonemes, std::optional<double> stress);
    static QString suffixS(const QString& stem);
    static QString suffixEd(const QString& stem);
    static QString suffixIng(const QString& stem);
    static QString normalize(const QString& text);
    static QList<Token> tokenize(const QString& text);

    QHash<QString, Entry> m_golds;
    QHash<QString, Entry> m_silvers;
};

/// PDF Fire: English words of numbers (num2words for the cardinal numbers, the
/// ordinal numbers and the years)
class PDF4QTLIBGUILIBSHARED_EXPORT PDFFireNumberWords
{
public:
    static QString cardinal(qint64 number);
    static QString ordinal(qint64 number);
    static QString year(qint64 number);
};

}   // namespace pdfviewer

#endif // PDFFIREG2P_H
