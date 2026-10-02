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

#include "pdffireg2p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace pdfviewer
{

namespace
{

// The phoneme classes of misaki (US English)
const QString VOWELS = QStringLiteral("AIOQWYaiuæɑɒɔəɛɜɪʊʌᵻ");
const QString CONSONANTS = QStringLiteral("bdfhjklmnpstvwzðŋɡɹɾʃʒʤʧθ");
const QString US_TAUS = QStringLiteral("AIOWYiuæɑəɛɪɹʊʌ");
const QString NON_QUOTE_PUNCTS = QStringLiteral(";:,.!?—…");
const QChar PRIMARY_STRESS(0x02C8);     // ˈ
const QChar SECONDARY_STRESS(0x02CC);   // ˌ

const QHash<QString, QString>& getSymbols()
{
    static const QHash<QString, QString> symbols = { { "%", "percent" }, { "&", "and" }, { "+", "plus" }, { "@", "at" } };
    return symbols;
}

/// Abbreviations, which are read as words (the period is not the end of a sentence).
/// Mostly the ones of fire departments and addresses.
const QHash<QString, QString>& getAbbreviations()
{
    static const QHash<QString, QString> abbreviations = {
        { "Dr", "doctor" }, { "Mr", "mister" }, { "Mrs", "missus" }, { "Ms", "miz" }, { "St", "street" },
        { "Ave", "avenue" }, { "Rd", "road" }, { "Blvd", "boulevard" }, { "Hwy", "highway" }, { "Rt", "route" },
        { "Dept", "department" }, { "Twp", "township" }, { "Co", "company" }, { "Lt", "lieutenant" },
        { "Capt", "captain" }, { "Sgt", "sergeant" }, { "Asst", "assistant" }, { "Chf", "chief" },
        { "etc", "et cetera" }, { "vs", "versus" }, { "approx", "approximately" }, { "Jan", "January" },
        { "Feb", "February" }, { "Mar", "March" }, { "Apr", "April" }, { "Aug", "August" }, { "Sept", "September" },
        { "Sep", "September" }, { "Oct", "October" }, { "Nov", "November" }, { "Dec", "December" },
        { "Mon", "Monday" }, { "Tue", "Tuesday" }, { "Wed", "Wednesday" }, { "Thu", "Thursday" }, { "Fri", "Friday" },
        { "Sat", "Saturday" }, { "Sun", "Sunday" }, { "Jr", "junior" }, { "Sr", "senior" }, { "Pg", "page" },
        { "pg", "page" }, { "Sec", "section" }, { "Art", "article" }
    };
    return abbreviations;
}

/// PDF Fire: words missing in the dictionaries of misaki - mostly the words of fire
/// departments. The phonemes use the alphabet of misaki (I = aɪ, W = aʊ, O = oʊ, A = eɪ).
const QHash<QString, QString>& getAdditionalWords()
{
    static const QHash<QString, QString> words = {
        { "et", "ˈɛt" }, { "cetera", "sˈɛtəɹə" }, { "non", "nˈɑn" }, { "nomex", "nˈOmɛks" },
        { "fireground", "fˈIəɹɡɹˌWnd" }, { "wildland", "wˈIldlˌænd" }, { "hoseline", "hˈOzlˌIn" },
        { "hoselines", "hˈOzlˌInz" }, { "callsign", "kˈɔlsˌIn" }, { "standpipe", "stˈændpˌIp" },
        { "firefighter", "fˈIəɹfˌIɾəɹ" }, { "firefighters", "fˈIəɹfˌIɾəɹz" }, { "firefighting", "fˈIəɹfˌIɾɪŋ" },
        { "apparatus", "ˌæpəɹˈæɾəs" }, { "scba", "ˌɛssˌibˌiˈA" }, { "mayday", "mˈAdˌA" }
    };
    return words;
}

/// PDF Fire: abbreviations, which are expanded also without a period (in addresses
/// and names of townships)
const QHash<QString, QString>& getWordExpansions()
{
    static const QHash<QString, QString> expansions = { { "Twp", "township" }, { "Boro", "borough" }, { "TWP", "township" }, { "BORO", "borough" } };
    return expansions;
}

bool isAllUpper(const QString& word)
{
    return word == word.toUpper();
}

bool isAllLower(const QString& word)
{
    return word == word.toLower();
}

QString capitalize(const QString& word)
{
    return word.isEmpty() ? word : word.left(1).toUpper() + word.mid(1).toLower();
}

bool isAsciiWord(const QString& word)
{
    for (QChar c : word)
    {
        const ushort u = c.unicode();
        if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '\'' || u == '-'))
        {
            return false;
        }
    }
    return !word.isEmpty();
}

bool isAlpha(const QString& word)
{
    for (QChar c : word)
    {
        if (!c.isLetter())
        {
            return false;
        }
    }
    return !word.isEmpty();
}

bool isDigits(const QString& text)
{
    for (QChar c : text)
    {
        if (!c.isDigit())
        {
            return false;
        }
    }
    return !text.isEmpty();
}

}   // namespace

QString PDFFireNumberWords::cardinal(qint64 number)
{
    static const char* const ones[] = { "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                                        "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen" };
    static const char* const tens[] = { "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety" };

    if (number < 0)
    {
        return QStringLiteral("minus ") + cardinal(-number);
    }
    if (number < 20)
    {
        return QString::fromLatin1(ones[number]);
    }
    if (number < 100)
    {
        return QString::fromLatin1(tens[number / 10]) + (number % 10 ? QStringLiteral("-") + QString::fromLatin1(ones[number % 10]) : QString());
    }
    if (number < 1000)
    {
        return QString::fromLatin1(ones[number / 100]) + QStringLiteral(" hundred") + (number % 100 ? QStringLiteral(" ") + cardinal(number % 100) : QString());
    }

    static const std::pair<qint64, const char*> scales[] = { { 1000000000000LL, "trillion" }, { 1000000000LL, "billion" }, { 1000000LL, "million" }, { 1000LL, "thousand" } };
    for (const auto& [scale, name] : scales)
    {
        if (number >= scale)
        {
            return cardinal(number / scale) + QStringLiteral(" ") + QString::fromLatin1(name) + (number % scale ? QStringLiteral(" ") + cardinal(number % scale) : QString());
        }
    }
    return QString();
}

QString PDFFireNumberWords::ordinal(qint64 number)
{
    QString words = cardinal(number);
    static const QHash<QString, QString> irregular = { { "one", "first" }, { "two", "second" }, { "three", "third" }, { "five", "fifth" },
                                                       { "eight", "eighth" }, { "nine", "ninth" }, { "twelve", "twelfth" } };
    const int split = qMax(words.lastIndexOf(QChar(' ')), words.lastIndexOf(QChar('-'))) + 1;
    const QString last = words.mid(split);
    QString lastOrdinal;
    if (irregular.contains(last))
    {
        lastOrdinal = irregular.value(last);
    }
    else if (last.endsWith(QChar('y')))
    {
        lastOrdinal = last.chopped(1) + QStringLiteral("ieth");
    }
    else
    {
        lastOrdinal = last + QStringLiteral("th");
    }
    return words.left(split) + lastOrdinal;
}

QString PDFFireNumberWords::year(qint64 number)
{
    if (number < 1000 || number > 9999 || number % 1000 == 0 || (number >= 2000 && number < 2010))
    {
        return cardinal(number);
    }

    const qint64 high = number / 100;
    const qint64 low = number % 100;
    if (low == 0)
    {
        return cardinal(high) + QStringLiteral(" hundred");
    }
    if (low < 10)
    {
        return cardinal(high) + QStringLiteral(" oh ") + cardinal(low);
    }
    return cardinal(high) + QStringLiteral(" ") + cardinal(low);
}

bool PDFFireG2P::load(const QString& directory, QString* errorMessage)
{
    auto loadDictionary = [&](const QString& fileName, QHash<QString, Entry>& dictionary)
    {
        QFile file(QDir(directory).filePath(fileName));
        if (!file.open(QFile::ReadOnly))
        {
            *errorMessage = QCoreApplication::translate("PDFFireG2P", "The pronunciation dictionary '%1' is missing.").arg(file.fileName());
            return false;
        }

        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        dictionary.reserve(object.size());
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            Entry entry;
            if (it.value().isString())
            {
                entry.defaultPhonemes = it.value().toString();
            }
            else if (it.value().isObject())
            {
                // Pronunciations by the part of speech - without a tagger the default one
                // is used, and the one for the end of a sentence ("None")
                const QJsonObject variants = it.value().toObject();
                entry.defaultPhonemes = variants.value("DEFAULT").toString();
                if (variants.contains("None") && variants.value("None").isString())
                {
                    entry.noneContextPhonemes = variants.value("None").toString();
                }
            }
            if (entry.isValid())
            {
                dictionary.insert(it.key(), entry);
            }
        }
        return !dictionary.isEmpty();
    };

    if (!loadDictionary(QStringLiteral("us_gold.json"), m_golds) || !loadDictionary(QStringLiteral("us_silver.json"), m_silvers))
    {
        return false;
    }

    // PDF Fire: the additional words - they take precedence
    for (auto it = getAdditionalWords().cbegin(); it != getAdditionalWords().cend(); ++it)
    {
        Entry entry;
        entry.defaultPhonemes = it.value();
        m_golds.insert(it.key(), entry);
    }
    return true;
}

const PDFFireG2P::Entry* PDFFireG2P::findEntry(const QHash<QString, Entry>& dictionary, const QString& word) const
{
    // misaki grows the dictionaries by the capitalized and the lowercase variants
    auto it = dictionary.constFind(word);
    if (it != dictionary.constEnd())
    {
        return &it.value();
    }
    if (word.size() < 2)
    {
        return nullptr;
    }
    if (isAllLower(word))
    {
        it = dictionary.constFind(capitalize(word));
    }
    else if (word == capitalize(word))
    {
        it = dictionary.constFind(word.toLower());
    }
    return it != dictionary.constEnd() ? &it.value() : nullptr;
}

QString PDFFireG2P::applyStress(QString phonemes, std::optional<double> stress)
{
    auto hasVowel = [&phonemes]()
    {
        for (QChar c : phonemes)
        {
            if (VOWELS.contains(c))
            {
                return true;
            }
        }
        return false;
    };

    // Moves the stress mark before the first vowel after it
    auto restress = [](const QString& text)
    {
        QList<std::pair<double, QChar>> items;
        for (int i = 0; i < text.size(); ++i)
        {
            items.append({ double(i), text[i] });
        }
        for (int i = 0; i < items.size(); ++i)
        {
            if (items[i].second == PRIMARY_STRESS || items[i].second == SECONDARY_STRESS)
            {
                for (int j = i; j < items.size(); ++j)
                {
                    if (VOWELS.contains(items[j].second))
                    {
                        items[i].first = j - 0.5;
                        break;
                    }
                }
            }
        }
        std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        QString result;
        for (const auto& item : items)
        {
            result += item.second;
        }
        return result;
    };

    const bool hasPrimary = phonemes.contains(PRIMARY_STRESS);
    const bool hasSecondary = phonemes.contains(SECONDARY_STRESS);
    if (!stress.has_value())
    {
        return phonemes;
    }

    const double value = *stress;
    if (value < -1)
    {
        phonemes.remove(PRIMARY_STRESS);
        phonemes.remove(SECONDARY_STRESS);
        return phonemes;
    }
    if (value == -1 || ((value == 0 || value == -0.5) && hasPrimary))
    {
        phonemes.remove(SECONDARY_STRESS);
        phonemes.replace(PRIMARY_STRESS, SECONDARY_STRESS);
        return phonemes;
    }
    if ((value == 0 || value == 0.5 || value == 1) && !hasPrimary && !hasSecondary)
    {
        return hasVowel() ? restress(QString(SECONDARY_STRESS) + phonemes) : phonemes;
    }
    if (value >= 1 && !hasPrimary && hasSecondary)
    {
        phonemes.replace(SECONDARY_STRESS, PRIMARY_STRESS);
        return phonemes;
    }
    if (value > 1 && !hasPrimary && !hasSecondary)
    {
        return hasVowel() ? restress(QString(PRIMARY_STRESS) + phonemes) : phonemes;
    }
    return phonemes;
}

std::optional<QString> PDFFireG2P::getNNP(const QString& word) const
{
    // The word is spelled - the names of the letters
    QString phonemes;
    for (QChar c : word)
    {
        if (!c.isLetter())
        {
            continue;
        }
        const Entry* entry = findEntry(m_golds, QString(c.toUpper()));
        if (!entry)
        {
            return std::nullopt;
        }
        phonemes += entry->defaultPhonemes;
    }
    if (phonemes.isEmpty())
    {
        return std::nullopt;
    }

    phonemes = applyStress(phonemes, 0);
    const int last = phonemes.lastIndexOf(SECONDARY_STRESS);
    if (last >= 0)
    {
        phonemes[last] = PRIMARY_STRESS;
    }
    return phonemes;
}

bool PDFFireG2P::isKnown(const QString& word) const
{
    if (findEntry(m_golds, word) || findEntry(m_silvers, word) || getSymbols().contains(word))
    {
        return true;
    }
    if (!isAlpha(word) || !isAsciiWord(word))
    {
        return false;
    }
    if (word.size() == 1)
    {
        return true;
    }
    if (isAllUpper(word) && findEntry(m_golds, word.toLower()))
    {
        return true;
    }
    return word.mid(1) == word.mid(1).toUpper();
}

std::optional<QString> PDFFireG2P::lookup(QString word, std::optional<double> stress, const Context& context, int* rating) const
{
    if (isAllUpper(word) && !m_golds.contains(word))
    {
        word = word.toLower();
    }

    const Entry* entry = findEntry(m_golds, word);
    int entryRating = 4;
    if (!entry)
    {
        entry = findEntry(m_silvers, word);
        entryRating = 3;
    }

    if (!entry)
    {
        if (rating)
        {
            *rating = 3;
        }
        return getNNP(word);
    }

    if (rating)
    {
        *rating = entryRating;
    }
    const QString phonemes = (!context.futureVowel.has_value() && entry->noneContextPhonemes) ? *entry->noneContextPhonemes : entry->defaultPhonemes;
    return applyStress(phonemes, stress);
}

QString PDFFireG2P::suffixS(const QString& stem)
{
    // https://en.wiktionary.org/wiki/-s
    if (stem.isEmpty())
    {
        return stem;
    }
    const QChar last = stem.back();
    if (QStringLiteral("ptkfθ").contains(last))
    {
        return stem + QStringLiteral("s");
    }
    if (QStringLiteral("szʃʒʧʤ").contains(last))
    {
        return stem + QStringLiteral("ᵻz");
    }
    return stem + QStringLiteral("z");
}

QString PDFFireG2P::suffixEd(const QString& stem)
{
    // https://en.wiktionary.org/wiki/-ed
    if (stem.isEmpty())
    {
        return stem;
    }
    const QChar last = stem.back();
    if (QStringLiteral("pkfθʃsʧ").contains(last))
    {
        return stem + QStringLiteral("t");
    }
    if (last == QChar('d'))
    {
        return stem + QStringLiteral("ᵻd");
    }
    if (last != QChar('t'))
    {
        return stem + QStringLiteral("d");
    }
    if (stem.size() < 2)
    {
        return stem + QStringLiteral("ɪd");
    }
    if (US_TAUS.contains(stem[stem.size() - 2]))
    {
        return stem.chopped(1) + QStringLiteral("ɾᵻd");
    }
    return stem + QStringLiteral("ᵻd");
}

QString PDFFireG2P::suffixIng(const QString& stem)
{
    // https://en.wiktionary.org/wiki/-ing
    if (stem.isEmpty())
    {
        return stem;
    }
    if (stem.size() > 1 && stem.back() == QChar('t') && US_TAUS.contains(stem[stem.size() - 2]))
    {
        return stem.chopped(1) + QStringLiteral("ɾɪŋ");
    }
    return stem + QStringLiteral("ɪŋ");
}

std::optional<QString> PDFFireG2P::stemS(const QString& word, std::optional<double> stress, const Context& context) const
{
    if (word.size() < 3 || !word.endsWith(QChar('s')))
    {
        return std::nullopt;
    }

    QString stem;
    if (!word.endsWith(QStringLiteral("ss")) && isKnown(word.chopped(1)))
    {
        stem = word.chopped(1);
    }
    else if ((word.endsWith(QStringLiteral("'s")) || (word.size() > 4 && word.endsWith(QStringLiteral("es")) && !word.endsWith(QStringLiteral("ies")))) && isKnown(word.chopped(2)))
    {
        stem = word.chopped(2);
    }
    else if (word.size() > 4 && word.endsWith(QStringLiteral("ies")) && isKnown(word.chopped(3) + QStringLiteral("y")))
    {
        stem = word.chopped(3) + QStringLiteral("y");
    }
    else
    {
        return std::nullopt;
    }

    const std::optional<QString> phonemes = lookup(stem, stress, context, nullptr);
    return phonemes ? std::optional<QString>(suffixS(*phonemes)) : std::nullopt;
}

std::optional<QString> PDFFireG2P::stemEd(const QString& word, std::optional<double> stress, const Context& context) const
{
    if (word.size() < 4 || !word.endsWith(QChar('d')))
    {
        return std::nullopt;
    }

    QString stem;
    if (!word.endsWith(QStringLiteral("dd")) && isKnown(word.chopped(1)))
    {
        stem = word.chopped(1);
    }
    else if (word.size() > 4 && word.endsWith(QStringLiteral("ed")) && !word.endsWith(QStringLiteral("eed")) && isKnown(word.chopped(2)))
    {
        stem = word.chopped(2);
    }
    else if (word.size() > 4 && word.endsWith(QStringLiteral("ied")) && isKnown(word.chopped(3) + QStringLiteral("y")))
    {
        // PDF Fire: specified, notified, verified - the stem ends with y
        stem = word.chopped(3) + QStringLiteral("y");
    }
    else
    {
        return std::nullopt;
    }

    const std::optional<QString> phonemes = lookup(stem, stress, context, nullptr);
    return phonemes ? std::optional<QString>(suffixEd(*phonemes)) : std::nullopt;
}

std::optional<QString> PDFFireG2P::stemIng(const QString& word, std::optional<double> stress, const Context& context) const
{
    if (word.size() < 5 || !word.endsWith(QStringLiteral("ing")))
    {
        return std::nullopt;
    }

    static const QRegularExpression doubledConsonant(QStringLiteral("([bcdgklmnprstvxz])\\1ing$|cking$"));
    QString stem;
    if (word.size() > 5 && isKnown(word.chopped(3)))
    {
        stem = word.chopped(3);
    }
    else if (isKnown(word.chopped(3) + QStringLiteral("e")))
    {
        stem = word.chopped(3) + QStringLiteral("e");
    }
    else if (word.size() > 5 && doubledConsonant.match(word).hasMatch() && isKnown(word.chopped(4)))
    {
        stem = word.chopped(4);
    }
    else
    {
        return std::nullopt;
    }

    const std::optional<QString> phonemes = lookup(stem, stress, context, nullptr);
    return phonemes ? std::optional<QString>(suffixIng(*phonemes)) : std::nullopt;
}

std::optional<QString> PDFFireG2P::getSpecialCase(const QString& word, std::optional<double> stress, const Context& context) const
{
    Q_UNUSED(stress);

    if (getSymbols().contains(word))
    {
        return lookup(getSymbols().value(word), std::nullopt, context, nullptr);
    }
    if (word == QStringLiteral("a") || word == QStringLiteral("A"))
    {
        return QStringLiteral("ɐ");
    }
    if (word == QStringLiteral("am") || word == QStringLiteral("Am") || word == QStringLiteral("AM"))
    {
        if (!context.futureVowel.has_value() || word != QStringLiteral("am"))
        {
            return findEntry(m_golds, QStringLiteral("am"))->defaultPhonemes;
        }
        return QStringLiteral("ɐm");
    }
    if (word == QStringLiteral("an") || word == QStringLiteral("An") || word == QStringLiteral("AN"))
    {
        return QStringLiteral("ɐn");
    }
    if (word == QStringLiteral("I"))
    {
        return QString(SECONDARY_STRESS) + QStringLiteral("I");
    }
    if (word == QStringLiteral("to") || word == QStringLiteral("To") || word == QStringLiteral("TO"))
    {
        if (!context.futureVowel.has_value())
        {
            return findEntry(m_golds, QStringLiteral("to"))->defaultPhonemes;
        }
        return *context.futureVowel ? QStringLiteral("tʊ") : QStringLiteral("tə");
    }
    if (word == QStringLiteral("in") || word == QStringLiteral("In") || word == QStringLiteral("IN"))
    {
        return QString(PRIMARY_STRESS) + QStringLiteral("ɪn");
    }
    if (word == QStringLiteral("the") || word == QStringLiteral("The") || word == QStringLiteral("THE"))
    {
        return (context.futureVowel.has_value() && *context.futureVowel) ? QStringLiteral("ði") : QStringLiteral("ðə");
    }
    return std::nullopt;
}

std::optional<QString> PDFFireG2P::getWord(QString word, std::optional<double> stress, const Context& context) const
{
    if (std::optional<QString> special = getSpecialCase(word, stress, context))
    {
        return special;
    }

    // A capitalized or an uppercase word, which is known in the lowercase
    const QString lower = word.toLower();
    if (word.size() > 1 && isAlpha(QString(word).remove(QChar('\''))) && word != lower &&
        !m_golds.contains(word) && !m_silvers.contains(word) && (word == word.toUpper() || word.mid(1) == word.mid(1).toLower()) &&
        (findEntry(m_golds, lower) || findEntry(m_silvers, lower) || stemS(lower, stress, context) || stemEd(lower, stress, context) || stemIng(lower, stress, context)))
    {
        word = lower;
    }

    if (isKnown(word))
    {
        return lookup(word, stress, context, nullptr);
    }
    if (word.endsWith(QStringLiteral("s'")) && isKnown(word.chopped(2) + QStringLiteral("'s")))
    {
        return lookup(word.chopped(2) + QStringLiteral("'s"), stress, context, nullptr);
    }
    if (word.endsWith(QChar('\'')) && isKnown(word.chopped(1)))
    {
        return lookup(word.chopped(1), stress, context, nullptr);
    }
    if (std::optional<QString> phonemes = stemS(word, stress, context))
    {
        return phonemes;
    }
    if (std::optional<QString> phonemes = stemEd(word, stress, context))
    {
        return phonemes;
    }
    if (std::optional<QString> phonemes = stemIng(word, stress.has_value() ? stress : std::optional<double>(0.5), context))
    {
        return phonemes;
    }
    return std::nullopt;
}

QString PDFFireG2P::lookupWords(const QString& words) const
{
    // The words of a number - each one from the dictionary
    QStringList result;
    for (const QString& word : words.split(QRegularExpression(QStringLiteral("[^a-z]+")), Qt::SkipEmptyParts))
    {
        if (word == QStringLiteral("and"))
        {
            continue;
        }
        if (std::optional<QString> phonemes = lookup(word, std::nullopt, Context(), nullptr))
        {
            result << *phonemes;
        }
    }
    return result.join(QChar(' '));
}

std::optional<QString> PDFFireG2P::getNumber(QString word) const
{
    // Currency, the suffix (1st, 1990s, 5's), a negative number, a decimal number,
    // a year (a four digit number), a number with the separators of thousands
    bool isCurrency = false;
    if (word.startsWith(QChar('$')))
    {
        isCurrency = true;
        word.remove(0, 1);
    }

    static const QRegularExpression suffixExpression(QStringLiteral("[a-z']+$"));
    const QRegularExpressionMatch suffixMatch = suffixExpression.match(word);
    const QString suffix = suffixMatch.hasMatch() ? suffixMatch.captured() : QString();
    word.chop(suffix.size());

    QString prefix;
    if (word.startsWith(QChar('-')))
    {
        prefix = lookupWords(QStringLiteral("minus")) + QStringLiteral(" ");
        word.remove(0, 1);
    }

    const QString plain = QString(word).remove(QChar(','));
    QString words;
    bool isOk = false;
    if (isCurrency)
    {
        const QStringList parts = plain.split(QChar('.'));
        const qint64 dollars = parts.value(0).toLongLong(&isOk);
        const qint64 cents = parts.size() > 1 ? parts.value(1).left(2).leftJustified(2, QChar('0')).toLongLong() : 0;
        words = PDFFireNumberWords::cardinal(dollars) + (dollars == 1 ? QStringLiteral(" dollar") : QStringLiteral(" dollars"));
        if (cents > 0)
        {
            words += QStringLiteral(" ") + PDFFireNumberWords::cardinal(cents) + (cents == 1 ? QStringLiteral(" cent") : QStringLiteral(" cents"));
        }
    }
    else if (isDigits(plain) && (suffix == QStringLiteral("st") || suffix == QStringLiteral("nd") || suffix == QStringLiteral("rd") || suffix == QStringLiteral("th")))
    {
        words = PDFFireNumberWords::ordinal(plain.toLongLong(&isOk));
    }
    else if (plain.count(QChar('.')) > 1)
    {
        // A section number (3.2.1) - the numbers one by one
        QStringList parts;
        for (const QString& part : plain.split(QChar('.'), Qt::SkipEmptyParts))
        {
            parts << PDFFireNumberWords::cardinal(part.toLongLong());
        }
        words = parts.join(QStringLiteral(" "));
        isOk = !parts.isEmpty();
    }
    else if (plain.contains(QChar('.')))
    {
        const QStringList parts = plain.split(QChar('.'));
        words = parts.value(0).isEmpty() ? QString() : PDFFireNumberWords::cardinal(parts.value(0).toLongLong());
        words += QStringLiteral(" point");
        for (QChar digit : parts.value(1))
        {
            words += QStringLiteral(" ") + PDFFireNumberWords::cardinal(digit.digitValue());
        }
        isOk = true;
    }
    else if (isDigits(word) && word.size() == 4)
    {
        words = PDFFireNumberWords::year(word.toLongLong(&isOk));
    }
    else if (isDigits(plain) && plain.size() > 1 && plain.startsWith(QChar('0')))
    {
        // A code (0815) - the digits one by one
        QStringList digits;
        for (QChar digit : plain)
        {
            digits << PDFFireNumberWords::cardinal(digit.digitValue());
        }
        words = digits.join(QStringLiteral(" "));
        isOk = true;
    }
    else if (isDigits(plain) && plain.size() <= 15)
    {
        words = PDFFireNumberWords::cardinal(plain.toLongLong(&isOk));
    }

    if (!isOk || words.isEmpty())
    {
        return std::nullopt;
    }

    QString phonemes = prefix + lookupWords(words);
    if (suffix == QStringLiteral("s") || suffix == QStringLiteral("'s"))
    {
        phonemes = suffixS(phonemes);
    }
    return phonemes;
}

std::optional<QString> PDFFireG2P::getUnknownWord(const QString& word, const Context& context) const
{
    // PDF Fire: a compound of two known words (firehouse, standpipe), else the word
    // is spelled (an abbreviation, a code)
    const QString lower = word.toLower();
    if (lower.size() >= 6 && isAsciiWord(lower))
    {
        for (int split = lower.size() - 3; split >= 3; --split)
        {
            const QString left = lower.left(split);
            const QString right = lower.mid(split);
            if ((findEntry(m_golds, left) || findEntry(m_silvers, left)) && (findEntry(m_golds, right) || findEntry(m_silvers, right) || stemS(right, std::nullopt, context)))
            {
                std::optional<QString> leftPhonemes = lookup(left, std::nullopt, context, nullptr);
                std::optional<QString> rightPhonemes = findEntry(m_golds, right) || findEntry(m_silvers, right) ? lookup(right, -0.5, context, nullptr) : stemS(right, -0.5, context);
                if (leftPhonemes && rightPhonemes)
                {
                    return *leftPhonemes + *rightPhonemes;
                }
            }
        }
    }

    if (isAlpha(word))
    {
        return getNNP(word);
    }
    return std::nullopt;
}

QString PDFFireG2P::normalize(const QString& text)
{
    // Ligatures of the PDF (ﬁ), the full width forms - compatibility decomposition
    QString result = text.normalized(QString::NormalizationForm_KC);
    result.replace(QChar(0x2018), QChar('\'')).replace(QChar(0x2019), QChar('\''));
    result.replace(QChar(0x00AD), QString());                       // soft hyphen
    result.replace(QStringLiteral("..."), QString(QChar(0x2026)));  // …
    result.replace(QChar(0x2022), QChar(','));                      // bullet
    result.replace(QChar(0x00A0), QChar(' '));

    // A dash between words is a pause, a hyphen inside a word joins the words
    static const QRegularExpression dash(QStringLiteral("\\s+[-\u2013\u2014]+\\s+|\\s+[\u2013\u2014]|[\u2013\u2014]\\s+|[\u2013\u2014]"));
    result.replace(dash, QStringLiteral(" \u2014 "));

    // Time of day (10:30) - hours and minutes, not a pause
    static const QRegularExpression time(QStringLiteral("\\b(\\d{1,2}):(\\d{2})\\b"));
    QRegularExpressionMatch match;
    int offset = 0;
    while ((match = time.match(result, offset)).hasMatch())
    {
        const int hours = match.captured(1).toInt();
        const int minutes = match.captured(2).toInt();
        QString words = PDFFireNumberWords::cardinal(hours);
        if (minutes == 0)
        {
            words += QStringLiteral(" o'clock");
        }
        else if (minutes < 10)
        {
            words += QStringLiteral(" oh ") + PDFFireNumberWords::cardinal(minutes);
        }
        else
        {
            words += QStringLiteral(" ") + PDFFireNumberWords::cardinal(minutes);
        }
        result.replace(match.capturedStart(), match.capturedLength(), words);
        offset = match.capturedStart() + words.size();
    }
    return result;
}

QList<PDFFireG2P::Token> PDFFireG2P::tokenize(const QString& text)
{
    // An abbreviation with periods (U.S.), a number (with the currency, the separators,
    // the suffix), a word (with apostrophes), a punctuation mark, a symbol
    static const QRegularExpression expression(QStringLiteral(
        "(?<dotted>\\b(?:[A-Za-z]\\.){2,})"
        "|(?<number>\\$?-?\\d[\\d,]*(?:\\.\\d+)*(?:st|nd|rd|th|s|'s)?(?![A-Za-z]))"
        "|(?<word>[A-Za-z]+(?:'[A-Za-z]+)*'?)"
        "|(?<punct>[;:,.!?\u2014\u2026\"\u201C\u201D()])"
        "|(?<symbol>[%&+@])"));

    QList<Token> tokens;
    QRegularExpressionMatchIterator it = expression.globalMatch(text);
    int lastEnd = 0;
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        if (!tokens.isEmpty() && match.capturedStart() > lastEnd)
        {
            // Whitespace or an ignored character (a slash, a hyphen) separate the words
            tokens.back().hasWhitespace = true;
        }
        lastEnd = match.capturedEnd();

        Token token;
        token.text = match.captured();
        if (!match.captured(QStringLiteral("punct")).isEmpty())
        {
            token.isPunctuation = true;
            if (token.text == QStringLiteral("("))
            {
                token.text = QString(QChar(0x201C));
            }
            else if (token.text == QStringLiteral(")"))
            {
                token.text = QString(QChar(0x201D));
            }
        }
        else if (!match.captured(QStringLiteral("dotted")).isEmpty())
        {
            token.isSpelled = true;
        }
        else if (!match.captured(QStringLiteral("word")).isEmpty())
        {
            const QString expansion = getWordExpansions().value(token.text);
            if (!expansion.isEmpty())
            {
                token.text = expansion;
                tokens << token;
                continue;
            }

            // An abbreviation, which is read as a word - its period is not a pause
            const QString abbreviation = getAbbreviations().value(token.text);
            if (!abbreviation.isEmpty() && text.mid(match.capturedEnd(), 1) == QStringLiteral("."))
            {
                const QRegularExpressionMatch next = it.hasNext() ? it.peekNext() : QRegularExpressionMatch();
                const bool isSentenceEnd = !next.hasMatch() || (next.captured().front().isUpper() && token.text != QStringLiteral("Dr") &&
                                                                token.text != QStringLiteral("St") && token.text != QStringLiteral("Mr") &&
                                                                token.text != QStringLiteral("Mrs") && token.text != QStringLiteral("Ms") &&
                                                                token.text != QStringLiteral("Lt") && token.text != QStringLiteral("Capt"));
                if (!isSentenceEnd)
                {
                    lastEnd = match.capturedEnd() + 1;
                    if (it.hasNext())
                    {
                        // The period is skipped
                        QRegularExpressionMatch peek = it.peekNext();
                        if (peek.captured() == QStringLiteral(".") && peek.capturedStart() == match.capturedEnd())
                        {
                            it.next();
                        }
                    }
                }
                for (const QString& part : abbreviation.split(QChar(' ')))
                {
                    Token abbreviationToken;
                    abbreviationToken.text = part;
                    abbreviationToken.hasWhitespace = true;
                    tokens << abbreviationToken;
                }
                tokens.back().hasWhitespace = false;
                continue;
            }
        }
        tokens << token;
    }
    if (!tokens.isEmpty() && lastEnd < text.size())
    {
        tokens.back().hasWhitespace = true;
    }
    return tokens;
}

QString PDFFireG2P::phonemize(const QString& text, QStringList* unknownWords) const
{
    QList<Token> tokens = tokenize(normalize(text));

    // From the end - the pronunciation of a word depends on the next one (the / thee)
    Context context;
    for (int i = tokens.size() - 1; i >= 0; --i)
    {
        Token& token = tokens[i];
        std::optional<QString> phonemes;
        if (token.isPunctuation)
        {
            phonemes = token.text;
        }
        else if (token.isSpelled)
        {
            phonemes = getNNP(token.text);
        }
        else
        {
            const QString& word = token.text;
            const std::optional<double> stress = isAllLower(word) ? std::nullopt : std::optional<double>(isAllUpper(word) && word.size() > 1 ? 2.0 : 0.5);
            const bool isNumber = word.contains(QRegularExpression(QStringLiteral("\\d")));
            phonemes = isNumber ? getNumber(word) : getWord(word, stress, context);
            if (!phonemes && !isNumber)
            {
                phonemes = getUnknownWord(word, context);
                if (unknownWords)
                {
                    unknownWords->append(word);
                }
            }
        }

        token.phonemes = phonemes.value_or(QString());
        token.hasPhonemes = phonemes.has_value();

        // The context for the previous word: does this one start with a vowel?
        if (!token.phonemes.isEmpty())
        {
            for (QChar c : token.phonemes)
            {
                if (NON_QUOTE_PUNCTS.contains(c))
                {
                    context.futureVowel = std::nullopt;
                    break;
                }
                if (VOWELS.contains(c))
                {
                    context.futureVowel = true;
                    break;
                }
                if (CONSONANTS.contains(c))
                {
                    context.futureVowel = false;
                    break;
                }
            }
        }
        context.futureTo = token.text.compare(QStringLiteral("to"), Qt::CaseInsensitive) == 0;
    }

    QString result;
    for (const Token& token : tokens)
    {
        if (token.phonemes.isEmpty())
        {
            continue;
        }
        if (!result.isEmpty() && !result.back().isSpace() && !token.isPunctuation)
        {
            // A word after a punctuation mark or a word is separated by a space
            result += QChar(' ');
        }
        result += token.phonemes;
        if (token.hasWhitespace && !result.back().isSpace())
        {
            result += QChar(' ');
        }
    }

    // The flap and the glottal stop of Kokoro 1.0
    result.replace(QChar(0x027E), QChar('T')).replace(QChar(0x0294), QChar('t'));
    return result.trimmed();
}

QStringList PDFFireG2P::splitSentences(const QString& text)
{
    // At the end of a sentence, followed by whitespace; a long sentence is split
    // further at the commas, so a chunk is never longer than the model reads
    static const QRegularExpression sentenceEnd(QStringLiteral("(?<=[.!?\u2026])[\"\u201D')]*\\s+"));
    QStringList result;
    for (QString sentence : text.split(sentenceEnd, Qt::SkipEmptyParts))
    {
        sentence = sentence.simplified();
        while (sentence.size() > 300)
        {
            int split = sentence.lastIndexOf(QRegularExpression(QStringLiteral("[,;:]\\s")), 300);
            if (split < 60)
            {
                split = sentence.lastIndexOf(QChar(' '), 300);
            }
            if (split < 60)
            {
                split = 300;
            }
            result << sentence.left(split + 1).trimmed();
            sentence = sentence.mid(split + 1).trimmed();
        }
        if (!sentence.isEmpty())
        {
            result << sentence;
        }
    }
    return result;
}

}   // namespace pdfviewer
