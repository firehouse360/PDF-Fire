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

#include "pdffireformfields.h"
#include "pdffirepageoperations.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfform.h"
#include "pdfencoding.h"
#include "pdfcms.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"

#include <QtTest>
#include <QBuffer>

using namespace pdf;

class FormFieldsTest : public QObject
{
    Q_OBJECT

private slots:
    void allTypesRoundTrip();
    void fieldsAreParsedAsAForm();
    void radioGroup();
    void updateKeepsTypeAndChangesSettings();
    void moveAndResize();
    void removeField();
    void removeRadioButtons();
    void duplicate();
    void uniqueNames();
    void existingIndirectAnnotsAreKept();
    void tabOrderByRows();
    void appearanceOfText();
    void detectUnderscores();
    void detectBoxesAndLines();
    void detectSkipsUnderlinesAndExistingWidgets();
    void nameFromLabel();
    void nullInAnnotationsIsKept();
    void radioValueCannotCollide();
    void removingChosenRadioButton();
    void choiceExportValues();
    void nonFiniteRectangleIsRefused();
    void winAnsiCharactersInAppearance();
    void scannerUsesTheTransformation();
    void writeSample();

private:
    static PDFDocument blank();
    static QByteArray write(const PDFDocument& document);
    static PDFDocument read(const QByteArray& data);
    static PDFObjectReference firstPage(const PDFDocument& document);
    static PDFFireFormFields::Settings settings(PDFFireFormFields::Type type, const QString& name);
    static size_t countWidgets(const PDFForm& form);
};

size_t FormFieldsTest::countWidgets(const PDFForm& form)
{
    size_t count = 0;
    std::function<void(const PDFFormField*)> add = [&](const PDFFormField* field)
    {
        count += field->getWidgets().size();
        for (const PDFFormFieldPointer& child : field->getChildFields())
        {
            add(child.get());
        }
    };
    for (const PDFFormFieldPointer& field : form.getFormFields())
    {
        add(field.get());
    }
    return count;
}

PDFDocument FormFieldsTest::blank()
{
    return PDFPageOperations::createBlankDocument(QSizeF(612, 792));
}

QByteArray FormFieldsTest::write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    PDFDocumentWriter(nullptr).write(&buffer, &document);
    return buffer.data();
}

PDFDocument FormFieldsTest::read(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(data);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        qWarning() << reader.getErrorMessage();
    }
    return document;
}

PDFObjectReference FormFieldsTest::firstPage(const PDFDocument& document)
{
    return document.getCatalog()->getPage(0)->getPageReference();
}

PDFFireFormFields::Settings FormFieldsTest::settings(PDFFireFormFields::Type type, const QString& name)
{
    PDFFireFormFields::Settings result = PDFFireFormFields::getDefaultSettings(type);
    result.name = name;
    return result;
}

void FormFieldsTest::allTypesRoundTrip()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = firstPage(document);

    using Type = PDFFireFormFields::Type;

    PDFFireFormFields::Settings text = settings(Type::Text, "Name");
    text.value = "John (Smith)";
    text.toolTip = "Your full name";
    text.required = true;
    text.maxLength = 30;
    text.alignment = 1;
    text.fontSize = 11;

    PDFFireFormFields::Settings notes = settings(Type::MultilineText, "Notes");
    notes.value = "First line\nSecond line";
    notes.backgroundColor = QColor(255, 255, 200);

    PDFFireFormFields::Settings date = settings(Type::Date, "Date");
    date.dateFormat = "dd.mm.yyyy";

    PDFFireFormFields::Settings check = settings(Type::CheckBox, "Agree");
    check.checked = true;
    check.exportValue = "On";

    PDFFireFormFields::Settings combo = settings(Type::ComboBox, "Rank");
    combo.options = QStringList{ "Chief", "Captain", "Firefighter" };
    combo.value = "Captain";
    combo.editable = true;

    PDFFireFormFields::Settings list = settings(Type::ListBox, "Station");
    list.options = QStringList{ "13", "14" };
    list.readOnly = true;

    PDFFireFormFields::Settings signature = settings(Type::Signature, "Signature");

    const std::vector<std::pair<PDFFireFormFields::Settings, QRectF>> fields = {
        { text, QRectF(50, 700, 200, 20) },
        { notes, QRectF(50, 600, 200, 60) },
        { date, QRectF(50, 560, 100, 20) },
        { check, QRectF(50, 530, 14, 14) },
        { combo, QRectF(50, 500, 150, 20) },
        { list, QRectF(50, 420, 150, 60) },
        { signature, QRectF(50, 360, 200, 36) },
    };

    std::vector<PDFObjectReference> widgets;
    for (const auto& [fieldSettings, rect] : fields)
    {
        widgets.push_back(PDFFireFormFields::createField(&builder, page, rect, fieldSettings));
    }

    const PDFDocument result = read(write(builder.build()));
    const PDFObjectReference resultPage = firstPage(result);
    QCOMPARE(PDFFireFormFields::getWidgets(&result.getStorage(), resultPage), widgets);

    for (size_t i = 0; i < fields.size(); ++i)
    {
        const PDFFireFormFields::Settings& expected = fields[i].first;
        const std::optional<PDFFireFormFields::Settings> actual = PDFFireFormFields::readField(&result.getStorage(), widgets[i]);
        QVERIFY2(actual.has_value(), qPrintable(expected.name));
        QCOMPARE(actual->type, expected.type);
        QCOMPARE(actual->name, expected.name);
        QCOMPARE(actual->toolTip, expected.toolTip);
        QCOMPARE(actual->required, expected.required);
        QCOMPARE(actual->readOnly, expected.readOnly);
        QCOMPARE(actual->maxLength, expected.maxLength);
        QCOMPARE(actual->alignment, expected.alignment);
        QCOMPARE(actual->value, expected.value);
        QCOMPARE(actual->checked, expected.checked);
        QCOMPARE(actual->options, expected.options);
        QCOMPARE(actual->editable, expected.editable);
        QCOMPARE(actual->dateFormat, expected.dateFormat);
        QCOMPARE(actual->backgroundColor, expected.backgroundColor);
        QCOMPARE(actual->borderColor.isValid(), expected.borderColor.isValid());
        QCOMPARE(PDFFireFormFields::getWidgetRect(&result.getStorage(), widgets[i]), fields[i].second);
        if (expected.type != Type::CheckBox)
        {
            QCOMPARE(actual->fontSize, expected.fontSize);
        }
    }

    QCOMPARE(PDFFireFormFields::readField(&result.getStorage(), widgets[3])->exportValue, QString("On"));
}

void FormFieldsTest::fieldsAreParsedAsAForm()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = firstPage(document);
    PDFFireFormFields::createField(&builder, page, QRectF(50, 700, 200, 20), settings(PDFFireFormFields::Type::Text, "Name"));
    PDFFireFormFields::createField(&builder, page, QRectF(50, 650, 14, 14), settings(PDFFireFormFields::Type::CheckBox, "Agree"));
    PDFFireFormFields::createField(&builder, page, QRectF(50, 600, 200, 36), settings(PDFFireFormFields::Type::Signature, "Sign"));

    const PDFDocument result = read(write(builder.build()));
    const PDFForm form = PDFForm::parse(&result, result.getCatalog()->getFormObject());
    QCOMPARE(form.getFormType(), PDFForm::FormType::AcroForm);
    QCOMPARE(form.getFormFields().size(), size_t(3));
    QCOMPARE(form.getFormFields()[0]->getFieldType(), PDFFormField::FieldType::Text);
    QCOMPARE(form.getFormFields()[0]->getName(PDFFormField::FullyQualified), QString("Name"));
    QCOMPARE(form.getFormFields()[1]->getFieldType(), PDFFormField::FieldType::Button);
    QCOMPARE(form.getFormFields()[2]->getFieldType(), PDFFormField::FieldType::Signature);
    QCOMPARE(countWidgets(form), size_t(3));

    // The form has the fonts of the fields
    const PDFDictionary* acroForm = result.getStorage().getDictionaryFromObject(result.getCatalog()->getFormObject());
    QVERIFY(acroForm);
    const PDFDictionary* resources = result.getStorage().getDictionaryFromObject(acroForm->get("DR"));
    QVERIFY(resources);
    const PDFDictionary* fonts = result.getStorage().getDictionaryFromObject(resources->get("Font"));
    QVERIFY(fonts && fonts->hasKey("Helv") && fonts->hasKey("ZaDb"));
}

void FormFieldsTest::radioGroup()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = firstPage(document);

    PDFFireFormFields::Settings radio = settings(PDFFireFormFields::Type::RadioButton, "Shift");
    radio.exportValue = "Day";
    const PDFObjectReference day = PDFFireFormFields::createField(&builder, page, QRectF(50, 700, 14, 14), radio);
    radio.exportValue = "Night";
    radio.checked = true;
    const PDFObjectReference night = PDFFireFormFields::createField(&builder, page, QRectF(100, 700, 14, 14), radio);
    radio.exportValue = "Day"; // already used - gets another value
    radio.checked = false;
    const PDFObjectReference third = PDFFireFormFields::createField(&builder, page, QRectF(150, 700, 14, 14), radio);

    const PDFDocument result = read(write(builder.build()));
    const PDFForm form = PDFForm::parse(&result, result.getCatalog()->getFormObject());
    QCOMPARE(form.getFormFields().size(), size_t(1));
    QCOMPARE(countWidgets(form), size_t(3));

    const auto daySettings = PDFFireFormFields::readField(&result.getStorage(), day);
    const auto nightSettings = PDFFireFormFields::readField(&result.getStorage(), night);
    const auto thirdSettings = PDFFireFormFields::readField(&result.getStorage(), third);
    QCOMPARE(daySettings->type, PDFFireFormFields::Type::RadioButton);
    QCOMPARE(daySettings->name, QString("Shift"));
    QCOMPARE(daySettings->exportValue, QString("Day"));
    QCOMPARE(nightSettings->exportValue, QString("Night"));
    QVERIFY(thirdSettings->exportValue != QString("Day") && thirdSettings->exportValue != QString("Night"));
    QVERIFY(!daySettings->checked);
    QVERIFY(nightSettings->checked);

    // Checking another button unchecks the others
    PDFDocument modified = result;
    PDFDocumentBuilder modifier(&modified);
    PDFFireFormFields::Settings checkDay = *daySettings;
    checkDay.checked = true;
    PDFFireFormFields::updateField(&modifier, day, checkDay);
    const PDFDocument after = read(write(modifier.build()));
    QVERIFY(PDFFireFormFields::readField(&after.getStorage(), day)->checked);
    QVERIFY(!PDFFireFormFields::readField(&after.getStorage(), night)->checked);
}

void FormFieldsTest::updateKeepsTypeAndChangesSettings()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference widget = PDFFireFormFields::createField(&builder, firstPage(document), QRectF(50, 700, 200, 20), settings(PDFFireFormFields::Type::Text, "Name"));

    PDFFireFormFields::Settings changed = settings(PDFFireFormFields::Type::CheckBox, "Full name");
    changed.required = true;
    changed.readOnly = true;
    changed.value = "Mike";
    changed.borderColor = QColor();
    PDFFireFormFields::updateField(&builder, widget, changed);

    const PDFDocument result = read(write(builder.build()));
    const auto actual = PDFFireFormFields::readField(&result.getStorage(), widget);
    QCOMPARE(actual->type, PDFFireFormFields::Type::Text);
    QCOMPARE(actual->name, QString("Full name"));
    QVERIFY(actual->required);
    QVERIFY(actual->readOnly);
    QCOMPARE(actual->value, QString("Mike"));
    QVERIFY(!actual->borderColor.isValid());
}

void FormFieldsTest::moveAndResize()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference widget = PDFFireFormFields::createField(&builder, firstPage(document), QRectF(50, 700, 200, 20), settings(PDFFireFormFields::Type::Text, "Name"));
    PDFFireFormFields::setWidgetRect(&builder, widget, QRectF(100, 300, 120, 40));

    const PDFDocument result = read(write(builder.build()));
    QCOMPARE(PDFFireFormFields::getWidgetRect(&result.getStorage(), widget), QRectF(100, 300, 120, 40));

    // The appearance follows the new size
    const PDFDictionary* dictionary = result.getStorage().getDictionaryFromObject(result.getStorage().getObjectByReference(widget));
    const PDFDictionary* appearance = result.getStorage().getDictionaryFromObject(dictionary->get("AP"));
    const PDFObject& normal = result.getStorage().getObject(appearance->get("N"));
    QVERIFY(normal.isStream());
    PDFDocumentDataLoaderDecorator loader(&result.getStorage());
    QCOMPARE(loader.readRectangle(normal.getStream()->getDictionary()->get("BBox"), QRectF()), QRectF(0, 0, 120, 40));
}

void FormFieldsTest::removeField()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = firstPage(document);
    const PDFObjectReference first = PDFFireFormFields::createField(&builder, page, QRectF(50, 700, 200, 20), settings(PDFFireFormFields::Type::Text, "A"));
    const PDFObjectReference second = PDFFireFormFields::createField(&builder, page, QRectF(50, 650, 200, 20), settings(PDFFireFormFields::Type::Text, "B"));
    PDFFireFormFields::removeWidget(&builder, first);

    const PDFDocument result = read(write(builder.build()));
    QCOMPARE(PDFFireFormFields::getWidgets(&result.getStorage(), firstPage(result)), std::vector<PDFObjectReference>{ second });
    QCOMPARE(PDFFireFormFields::getFieldNames(&result.getStorage()), QStringList{ "B" });
}

void FormFieldsTest::removeRadioButtons()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = firstPage(document);
    PDFFireFormFields::Settings radio = settings(PDFFireFormFields::Type::RadioButton, "Shift");
    const PDFObjectReference a = PDFFireFormFields::createField(&builder, page, QRectF(50, 700, 14, 14), radio);
    const PDFObjectReference b = PDFFireFormFields::createField(&builder, page, QRectF(100, 700, 14, 14), radio);

    PDFFireFormFields::removeWidget(&builder, a);
    PDFDocument afterOne = read(write(builder.build()));
    QCOMPARE(PDFFireFormFields::getFieldNames(&afterOne.getStorage()), QStringList{ "Shift" });
    QCOMPARE(countWidgets(PDFForm::parse(&afterOne, afterOne.getCatalog()->getFormObject())), size_t(1));

    PDFDocumentBuilder builder2(&afterOne);
    PDFFireFormFields::removeWidget(&builder2, b);
    const PDFDocument afterBoth = read(write(builder2.build()));
    QVERIFY(PDFFireFormFields::getFieldNames(&afterBoth.getStorage()).isEmpty());
    QVERIFY(PDFFireFormFields::getWidgets(&afterBoth.getStorage(), firstPage(afterBoth)).empty());
}

void FormFieldsTest::duplicate()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference widget = PDFFireFormFields::createField(&builder, firstPage(document), QRectF(50, 700, 200, 20), settings(PDFFireFormFields::Type::Text, "Name1"));
    const PDFObjectReference copy = PDFFireFormFields::duplicateField(&builder, widget, QRectF(50, 650, 200, 20));
    QVERIFY(copy.isValid());

    const PDFDocument result = read(write(builder.build()));
    QCOMPARE(PDFFireFormFields::getFieldNames(&result.getStorage()), (QStringList{ "Name1", "Name2" }));
}

void FormFieldsTest::uniqueNames()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    QCOMPARE(PDFFireFormFields::createUniqueName(builder.getStorage(), "Text"), QString("Text1"));
    PDFFireFormFields::createField(&builder, firstPage(document), QRectF(50, 700, 200, 20), settings(PDFFireFormFields::Type::Text, "Text1"));
    QCOMPARE(PDFFireFormFields::createUniqueName(builder.getStorage(), "Text"), QString("Text2"));
    QCOMPARE(PDFFireFormFields::createUniqueName(builder.getStorage(), " "), QString("Field1"));
}

void FormFieldsTest::existingIndirectAnnotsAreKept()
{
    // A page with an indirect array of annotations, holding a link
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = firstPage(document);
    PDFDictionaryBuilder link;
    link.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Annot"));
    link.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Link"));
    const PDFObjectReference linkReference = builder.addObject(PDFObject::createDictionary(std::move(link)));
    PDFArrayBuilder annots;
    annots.appendItem(PDFObject::createReference(linkReference));
    const PDFObjectReference annotsReference = builder.addObject(PDFObject::createArray(std::move(annots)));
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Annots");
    factory << annotsReference;
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(page, factory.takeObject());

    const PDFObjectReference widget = PDFFireFormFields::createField(&builder, page, QRectF(50, 700, 200, 20), settings(PDFFireFormFields::Type::Text, "Name"));
    const PDFDocument result = read(write(builder.build()));
    PDFDocumentDataLoaderDecorator loader(&result.getStorage());
    const PDFDictionary* pageDictionary = result.getStorage().getDictionaryFromObject(result.getStorage().getObjectByReference(firstPage(result)));
    QCOMPARE(loader.readReferenceArrayFromDictionary(pageDictionary, "Annots"), (std::vector<PDFObjectReference>{ linkReference, widget }));
}

void FormFieldsTest::tabOrderByRows()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = firstPage(document);
    using Type = PDFFireFormFields::Type;
    // Created in a mixed order: second row right, first row right, second row left, first row left
    const PDFObjectReference rowTwoRight = PDFFireFormFields::createField(&builder, page, QRectF(300, 600, 100, 20), settings(Type::Text, "D"));
    const PDFObjectReference rowOneRight = PDFFireFormFields::createField(&builder, page, QRectF(300, 702, 100, 20), settings(Type::Text, "B"));
    const PDFObjectReference rowTwoLeft = PDFFireFormFields::createField(&builder, page, QRectF(50, 600, 100, 20), settings(Type::Text, "C"));
    const PDFObjectReference rowOneLeft = PDFFireFormFields::createField(&builder, page, QRectF(50, 700, 100, 20), settings(Type::Text, "A"));

    PDFFireFormFields::sortTabOrderByRows(&builder, page);
    const PDFDocument result = read(write(builder.build()));
    QCOMPARE(PDFFireFormFields::getWidgets(&result.getStorage(), firstPage(result)), (std::vector<PDFObjectReference>{ rowOneLeft, rowOneRight, rowTwoLeft, rowTwoRight }));

    const PDFDictionary* pageDictionary = result.getStorage().getDictionaryFromObject(result.getStorage().getObjectByReference(firstPage(result)));
    QCOMPARE(pageDictionary->get("Tabs").getString(), QByteArray("R"));
}

void FormFieldsTest::appearanceOfText()
{
    PDFFireFormFields::Settings text = settings(PDFFireFormFields::Type::Text, "Name");
    text.value = "A (b) \\ c";
    const QByteArray content = PDFFireFormFields::createAppearanceContent(text, QSizeF(200, 20), false);
    QVERIFY(content.contains("/Tx BMC"));
    QVERIFY(content.contains("EMC"));
    QVERIFY(content.contains("(A \\(b\\) \\\\ c) Tj"));
    QVERIFY(content.contains("/Helv"));

    // A long text is made smaller to fit, with the automatic size
    text.value = QString(200, QChar('W'));
    const QByteArray longContent = PDFFireFormFields::createAppearanceContent(text, QSizeF(100, 20), false);
    const QRegularExpressionMatch match = QRegularExpression("/Helv ([0-9.]+) Tf").match(QString::fromLatin1(longContent));
    QVERIFY(match.hasMatch());
    QVERIFY(match.captured(1).toDouble() <= 4.0);

    // Check box: the check mark only in the on state
    PDFFireFormFields::Settings check = settings(PDFFireFormFields::Type::CheckBox, "C");
    QVERIFY(PDFFireFormFields::createAppearanceContent(check, QSizeF(14, 14), true).contains("(4) Tj"));
    QVERIFY(!PDFFireFormFields::createAppearanceContent(check, QSizeF(14, 14), false).contains("(4) Tj"));

    // Signature: a line and an X
    const QByteArray signature = PDFFireFormFields::createAppearanceContent(settings(PDFFireFormFields::Type::Signature, "S"), QSizeF(200, 36), false);
    QVERIFY(signature.contains(" l S"));
    QVERIFY(signature.contains("(X) Tj"));

    QCOMPARE(PDFFireFormFields::getHelveticaTextWidth("Hi"), (722 + 222) / 1000.0);
}

namespace
{

/// A run of text with characters of the given width placed one after another
PDFFireFormFields::TextRun createRun(const QString& text, PDFReal x, PDFReal baseline, PDFReal characterWidth = 5.0, PDFReal fontSize = 10.0)
{
    PDFFireFormFields::TextRun run;
    run.text = text;
    run.fontSize = fontSize;
    run.baseline = baseline;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        run.characters.push_back(QRectF(x + i * characterWidth, baseline - 2.0, characterWidth, fontSize));
    }
    return run;
}

}   // namespace

void FormFieldsTest::detectUnderscores()
{
    const std::vector<PDFFireFormFields::TextRun> runs = {
        createRun("Name: __________   Date: ________", 50, 700),
        createRun("Member signature ______________", 50, 650),
        createRun("A_B and x__y", 50, 600), // fewer than three underscores - no field
    };

    const auto fields = PDFFireFormFields::detectFields(runs, {}, {}, {});
    QCOMPARE(fields.size(), size_t(3));

    QCOMPARE(fields[0].type, PDFFireFormFields::Type::Text);
    QCOMPARE(fields[0].label, QString("Name"));
    QCOMPARE(fields[0].rect.left(), 50.0 + 6 * 5.0);
    QCOMPARE(fields[0].rect.right(), 50.0 + 16 * 5.0);
    QVERIFY(fields[0].rect.top() < 700.0 && fields[0].rect.bottom() > 700.0);

    QCOMPARE(fields[1].type, PDFFireFormFields::Type::Date);
    QCOMPARE(fields[1].label, QString("Date"));

    QCOMPARE(fields[2].type, PDFFireFormFields::Type::Signature);
    QCOMPARE(fields[2].label, QString("Member signature"));
}

void FormFieldsTest::detectBoxesAndLines()
{
    const std::vector<PDFFireFormFields::TextRun> runs = {
        createRun(QString::fromUtf8("☐ Yes  ☐ No"), 50, 700),
        createRun("Address:", 50, 650),
        createRun("Certified", 82, 600),
    };

    const std::vector<QRectF> lines = { QRectF(100, 649, 200, 0.5) };
    const std::vector<QRectF> squares = { QRectF(65, 596, 10, 10) };

    const auto fields = PDFFireFormFields::detectFields(runs, lines, squares, {});
    QCOMPARE(fields.size(), size_t(4));
    QCOMPARE(fields[0].type, PDFFireFormFields::Type::CheckBox);
    QCOMPARE(fields[0].label, QString("Yes"));
    QCOMPARE(fields[1].type, PDFFireFormFields::Type::CheckBox);
    QCOMPARE(fields[1].label, QString("No"));
    QCOMPARE(fields[2].type, PDFFireFormFields::Type::CheckBox);
    QCOMPARE(fields[2].label, QString("Certified"));
    QCOMPARE(fields[3].type, PDFFireFormFields::Type::Text);
    QCOMPARE(fields[3].label, QString("Address"));
    QCOMPARE(fields[3].rect.left(), 100.0);
    QCOMPARE(fields[3].rect.right(), 300.0);
    QVERIFY(fields[3].rect.bottom() > 649.0);
}

void FormFieldsTest::detectSkipsUnderlinesAndExistingWidgets()
{
    const std::vector<PDFFireFormFields::TextRun> runs = {
        createRun("Underlined heading", 100, 702),
        createRun("Name: __________", 50, 650),
    };

    // A line under a text is an underline, and a place with a widget is skipped
    const std::vector<QRectF> lines = { QRectF(100, 700, 90, 0.5) };
    const std::vector<QRectF> widgets = { QRectF(80, 647, 50, 14) };
    QVERIFY(PDFFireFormFields::detectFields(runs, lines, {}, widgets).empty());
}

void FormFieldsTest::nameFromLabel()
{
    QCOMPARE(PDFFireFormFields::createNameFromLabel("First name:"), QString("First name"));
    QCOMPARE(PDFFireFormFields::createNameFromLabel("  ___ "), QString("Field"));
    QCOMPARE(PDFFireFormFields::createNameFromLabel("Date (mm/dd/yyyy)"), QString("Date mm dd yyyy"));
}

void FormFieldsTest::nullInAnnotationsIsKept()
{
    // A null in the annotations of a page (left by a deleted annotation) - the loader
    // of PDF4QT returns no references at all for such an array
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = firstPage(document);
    PDFDictionaryBuilder link;
    link.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Annot"));
    link.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Link"));
    const PDFObjectReference linkReference = builder.addObject(PDFObject::createDictionary(std::move(link)));
    PDFArrayBuilder annots;
    annots.appendItem(PDFObject::createReference(linkReference));
    annots.appendItem(PDFObject());
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Annots");
    factory << PDFObject::createArray(std::move(annots));
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(page, factory.takeObject());

    const PDFObjectReference a = PDFFireFormFields::createField(&builder, page, QRectF(50, 600, 100, 20), settings(PDFFireFormFields::Type::Text, "A"));
    const PDFObjectReference b = PDFFireFormFields::createField(&builder, page, QRectF(50, 700, 100, 20), settings(PDFFireFormFields::Type::Text, "B"));
    QCOMPARE(PDFFireFormFields::getWidgets(builder.getStorage(), page), (std::vector<PDFObjectReference>{ a, b }));

    PDFFireFormFields::sortTabOrderByRows(&builder, page);
    PDFFireFormFields::removeWidget(&builder, a);

    const PDFDictionary* pageDictionary = builder.getStorage()->getDictionaryFromObject(builder.getStorage()->getObjectByReference(page));
    const PDFArray* array = pageDictionary->get("Annots").getArray();
    QCOMPARE(array->getCount(), size_t(3));
    QCOMPARE(array->getItem(0).getReference(), linkReference);
    QVERIFY(array->getItem(1).isNull());
    QCOMPARE(array->getItem(2).getReference(), b);
}

void FormFieldsTest::radioValueCannotCollide()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = firstPage(document);
    PDFFireFormFields::Settings radio = settings(PDFFireFormFields::Type::RadioButton, "Shift");
    radio.exportValue = "Day";
    PDFFireFormFields::createField(&builder, page, QRectF(50, 700, 14, 14), radio);
    radio.exportValue = "Night";
    const PDFObjectReference night = PDFFireFormFields::createField(&builder, page, QRectF(100, 700, 14, 14), radio);

    PDFFireFormFields::Settings changed = *PDFFireFormFields::readField(builder.getStorage(), night);
    changed.exportValue = "Day";
    PDFFireFormFields::updateField(&builder, night, changed);
    QCOMPARE(PDFFireFormFields::readField(builder.getStorage(), night)->exportValue, QString("Night"));

    // A checked radio button keeps the value given to it when it is created
    PDFFireFormFields::Settings checked = radio;
    checked.exportValue = "Day";
    checked.checked = true;
    const PDFObjectReference third = PDFFireFormFields::createField(&builder, page, QRectF(150, 700, 14, 14), checked);
    const auto thirdSettings = PDFFireFormFields::readField(builder.getStorage(), third);
    QVERIFY(thirdSettings->exportValue != QString("Day") && thirdSettings->exportValue != QString("Night"));
    QVERIFY(thirdSettings->checked);
}

void FormFieldsTest::removingChosenRadioButton()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = firstPage(document);
    PDFFireFormFields::Settings radio = settings(PDFFireFormFields::Type::RadioButton, "Shift");
    radio.exportValue = "Day";
    radio.checked = true;
    const PDFObjectReference day = PDFFireFormFields::createField(&builder, page, QRectF(50, 700, 14, 14), radio);
    radio.exportValue = "Night";
    radio.checked = false;
    const PDFObjectReference night = PDFFireFormFields::createField(&builder, page, QRectF(100, 700, 14, 14), radio);

    PDFFireFormFields::removeWidget(&builder, day);
    const PDFDictionary* nightDictionary = builder.getStorage()->getDictionaryFromObject(builder.getStorage()->getObjectByReference(night));
    const PDFDictionary* group = builder.getStorage()->getDictionaryFromObject(nightDictionary->get("Parent"));
    QCOMPARE(group->get("V").getString(), QByteArray("Off"));
}

void FormFieldsTest::choiceExportValues()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    PDFFireFormFields::Settings combo = settings(PDFFireFormFields::Type::ComboBox, "Country");
    combo.options = QStringList{ "United States", "Canada" };
    combo.exportValues = QStringList{ "US", "CA" };
    combo.value = "Canada";
    const PDFObjectReference widget = PDFFireFormFields::createField(&builder, firstPage(document), QRectF(50, 700, 150, 20), combo);

    const PDFDocument result = read(write(builder.build()));
    const auto actual = PDFFireFormFields::readField(&result.getStorage(), widget);
    QCOMPARE(actual->options, combo.options);
    QCOMPARE(actual->exportValues, combo.exportValues);
    QCOMPARE(actual->value, QString("Canada"));

    // The value saved is the export value
    const PDFDictionary* dictionary = result.getStorage().getDictionaryFromObject(result.getStorage().getObjectByReference(widget));
    QCOMPARE(PDFEncoding::convertTextString(dictionary->get("V").getString()), QString("CA"));

    // Changing only the look keeps the value
    PDFDocument modified = result;
    PDFDocumentBuilder modifier(&modified);
    PDFFireFormFields::Settings look = *actual;
    look.backgroundColor = Qt::yellow;
    PDFFireFormFields::updateField(&modifier, widget, look);
    const PDFDictionary* after = modifier.getStorage()->getDictionaryFromObject(modifier.getStorage()->getObjectByReference(widget));
    QCOMPARE(PDFEncoding::convertTextString(after->get("V").getString()), QString("CA"));
}

void FormFieldsTest::nonFiniteRectangleIsRefused()
{
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFReal infinity = std::numeric_limits<PDFReal>::infinity();
    QVERIFY(!PDFFireFormFields::createField(&builder, firstPage(document), QRectF(0, 0, infinity, 20), settings(PDFFireFormFields::Type::Text, "A")).isValid());
    QVERIFY(!PDFFireFormFields::createField(&builder, firstPage(document), QRectF(0, 0, 0, 0), settings(PDFFireFormFields::Type::Text, "A")).isValid());
}

void FormFieldsTest::winAnsiCharactersInAppearance()
{
    PDFFireFormFields::Settings text = settings(PDFFireFormFields::Type::Text, "Price");
    text.value = QString::fromUtf8("5 \u20AC \u201Cok\u201D \u4E2D");
    const QByteArray content = PDFFireFormFields::createAppearanceContent(text, QSizeF(300, 20), false);
    QVERIFY(content.contains(QByteArray("(5 \x80 \x93ok\x94 ?) Tj")));
}

void FormFieldsTest::scannerUsesTheTransformation()
{
    // The content is drawn with a flipped coordinate system (as Chrome writes it):
    // the line at y = 100 of the content is at y = 692 of the page
    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = firstPage(document);
    QByteArray content = "1 0 0 -1 0 792 cm 0.5 w 100 100 m 300 100 l S 50 50 10 10 re S";
    const PDFObjectReference contents = builder.addObject(PDFObject::createStream(PDFStream(PDFDictionaryBuilder(), std::move(content))));
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Contents");
    factory << contents;
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(page, factory.takeObject());
    const PDFDocument result = builder.build();

    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFOptionalContentActivity activity(&result, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&result), &activity));
    PDFFireFormFieldScanner scanner(PDFRenderer::IgnoreOptionalContent, result.getCatalog()->getPage(0), &result, &fontCache, &cms, &activity,
                                    QTransform(), PDFMeshQualitySettings());
    const PDFFireFormFieldScanner::Result scanned = scanner.scan();

    QCOMPARE(scanned.horizontalLines.size(), size_t(1));
    QVERIFY(qAbs(scanned.horizontalLines.front().center().y() - 692.0) < 1.0);
    QVERIFY(qAbs(scanned.horizontalLines.front().left() - 100.0) < 1.0);
    QCOMPARE(scanned.squares.size(), size_t(1));
    QVERIFY(qAbs(scanned.squares.front().center().y() - (792.0 - 55.0)) < 1.0);
}

void FormFieldsTest::writeSample()
{
    // A sample with every type of field, for checking the form in other readers
    // (written only when PDFFIRE_FORM_SAMPLE names the file)
    const QString fileName = qEnvironmentVariable("PDFFIRE_FORM_SAMPLE");
    if (fileName.isEmpty())
    {
        QSKIP("PDFFIRE_FORM_SAMPLE is not set");
    }

    PDFDocument document = blank();
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = firstPage(document);
    using Type = PDFFireFormFields::Type;

    PDFFireFormFields::Settings text = settings(Type::Text, "Name");
    text.value = "John Smith";
    PDFFireFormFields::createField(&builder, page, QRectF(50, 700, 250, 22), text);
    PDFFireFormFields::Settings notes = settings(Type::MultilineText, "Notes");
    notes.value = "Several lines of text, which are wrapped to the width of the field by the form maker.";
    PDFFireFormFields::createField(&builder, page, QRectF(50, 610, 250, 70), notes);
    PDFFireFormFields::createField(&builder, page, QRectF(50, 570, 120, 22), settings(Type::Date, "Date"));
    PDFFireFormFields::Settings check = settings(Type::CheckBox, "Agree");
    check.checked = true;
    PDFFireFormFields::createField(&builder, page, QRectF(50, 535, 14, 14), check);
    PDFFireFormFields::createField(&builder, page, QRectF(80, 535, 14, 14), settings(Type::CheckBox, "Other"));
    PDFFireFormFields::Settings radio = settings(Type::RadioButton, "Shift");
    radio.exportValue = "Day";
    radio.checked = true;
    PDFFireFormFields::createField(&builder, page, QRectF(50, 505, 14, 14), radio);
    radio.exportValue = "Night";
    radio.checked = false;
    PDFFireFormFields::createField(&builder, page, QRectF(80, 505, 14, 14), radio);
    PDFFireFormFields::Settings combo = settings(Type::ComboBox, "Rank");
    combo.options = QStringList{ "Chief", "Captain", "Firefighter" };
    combo.value = "Captain";
    PDFFireFormFields::createField(&builder, page, QRectF(50, 470, 150, 22), combo);
    PDFFireFormFields::Settings list = settings(Type::ListBox, "Station");
    list.options = QStringList{ "Station 13", "Station 14", "Station 15" };
    list.value = "Station 14";
    PDFFireFormFields::createField(&builder, page, QRectF(50, 400, 150, 50), list);
    PDFFireFormFields::createField(&builder, page, QRectF(50, 330, 250, 40), settings(Type::Signature, "Signature"));

    // A page rotated by 90 degrees: the text of the field must be upright, when shown
    const PDFObjectReference rotatedPage = builder.appendPage(QRectF(0, 0, 612, 792));
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Rotate");
    factory << PDFInteger(90);
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(rotatedPage, factory.takeObject());
    PDFFireFormFields::Settings rotatedText = settings(Type::Text, "Rotated");
    rotatedText.value = "Upright text";
    PDFFireFormFields::createField(&builder, rotatedPage, QRectF(100, 100, 22, 200), rotatedText);
    PDFFireFormFields::Settings rotatedCheck = settings(Type::CheckBox, "RotatedCheck");
    rotatedCheck.checked = true;
    PDFFireFormFields::createField(&builder, rotatedPage, QRectF(100, 330, 14, 14), rotatedCheck);

    QFile file(fileName);
    QVERIFY(file.open(QFile::WriteOnly | QFile::Truncate));
    file.write(write(builder.build()));
}

QTEST_APPLESS_MAIN(FormFieldsTest)

#include "tst_formfieldstest.moc"
