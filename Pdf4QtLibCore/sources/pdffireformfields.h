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

#ifndef PDFFIREFORMFIELDS_H
#define PDFFIREFORMFIELDS_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdftextlayoutgenerator.h"

#include <QColor>
#include <QRectF>
#include <QStringList>

#include <optional>
#include <vector>

namespace pdf
{
class PDFDocumentBuilder;
class PDFObjectStorage;

/// PDF Fire: the form maker - creates the fields of an interactive form (text
/// fields, check boxes, radio buttons, lists, dates and signature fields), edits,
/// moves and removes them. The fields are written the standard way (a field
/// merged with its widget, an appearance stream for every state, the fonts in
/// the default resources of the form), so the form can be filled in any reader,
/// not only in PDF Fire.
class PDF4QTLIBCORESHARED_EXPORT PDFFireFormFields
{
public:
    enum class Type
    {
        Text,
        MultilineText,
        Date,
        CheckBox,
        RadioButton,
        ComboBox,
        ListBox,
        Signature
    };

    struct Settings
    {
        Type type = Type::Text;
        QString name;                   ///< Name of the field (of the group, for a radio button)
        QString toolTip;                ///< Text shown when the mouse is over the field
        bool required = false;
        bool readOnly = false;
        PDFReal fontSize = 0.0;         ///< 0 - the size follows the height of the field
        int alignment = 0;              ///< 0 left, 1 center, 2 right
        int maxLength = 0;              ///< 0 - no limit (text fields)
        QString value;                  ///< Initial value (text fields, lists)
        bool checked = false;           ///< Initially checked (check box, radio button)
        QString exportValue;            ///< Value of the checked state (check box, radio button)
        QStringList options;            ///< Items of a list (as they are shown)
        QStringList exportValues;       ///< Values of the items, when they differ from the shown texts (empty otherwise)
        bool editable = false;          ///< A combo box, in which any text can be typed
        QString dateFormat;             ///< Format of a date field, e.g. "mm/dd/yyyy"
        QColor borderColor = Qt::black; ///< Invalid - no border
        QColor backgroundColor;         ///< Invalid - transparent
        PDFReal borderWidth = 1.0;
    };

    /// Returns the default settings of a new field of the given type (border,
    /// background, the default date format and the like)
    static Settings getDefaultSettings(Type type);

    /// Returns the default size of a field placed by a single click
    static QSizeF getDefaultSize(Type type);

    /// Returns the user-readable name of the type
    static QString getTypeName(Type type);

    /// Creates a field with one widget on the page. A radio button is added to the
    /// group of the same name, if it exists, otherwise the group is created.
    /// \param builder Builder
    /// \param page Page
    /// \param rect Rectangle of the widget, in the coordinates of the page
    /// \param settings Settings (the name must be set)
    /// \returns Reference to the widget
    static PDFObjectReference createField(PDFDocumentBuilder* builder, PDFObjectReference page, QRectF rect, const Settings& settings);

    /// Reads the settings of the field of the widget. Returns nothing, if the
    /// widget is not a widget of a field of a supported type.
    static std::optional<Settings> readField(const PDFObjectStorage* storage, PDFObjectReference widget);

    /// Changes the settings of the field of the widget (the type is kept) and
    /// recreates the appearance of the widget
    static void updateField(PDFDocumentBuilder* builder, PDFObjectReference widget, const Settings& settings);

    /// Moves or resizes the widget and recreates its appearance
    static void setWidgetRect(PDFDocumentBuilder* builder, PDFObjectReference widget, QRectF rect);

    /// Copies the field of the widget to the rectangle (with a new name)
    static PDFObjectReference duplicateField(PDFDocumentBuilder* builder, PDFObjectReference widget, QRectF rect);

    /// Removes the widget, and its field, if the field has no other widget
    static void removeWidget(PDFDocumentBuilder* builder, PDFObjectReference widget);

    /// Returns the rectangle of the widget
    static QRectF getWidgetRect(const PDFObjectStorage* storage, PDFObjectReference widget);

    /// Returns the widgets of the fields on the page, in the order of the page
    static std::vector<PDFObjectReference> getWidgets(const PDFObjectStorage* storage, PDFObjectReference page);

    /// Returns the full names of all the fields of the form
    static QStringList getFieldNames(const PDFObjectStorage* storage);

    /// Returns a name, which is not used by any field: the base followed by
    /// the first free number ("Text1", "Text2", ...)
    static QString createUniqueName(const PDFObjectStorage* storage, QString base);

    /// Sorts the widgets of the page, so the Tab key goes through them by rows,
    /// from the top to the bottom and from the left to the right
    static void sortTabOrderByRows(PDFDocumentBuilder* builder, PDFObjectReference page);

    /// A field found by the detection of the empty places of a form
    struct DetectedField
    {
        Type type = Type::Text;
        QRectF rect;
        QString label;  ///< Text before the place ("Name:"), used to name the field
    };

    /// A run of text on a line, as the detection needs it
    struct TextRun
    {
        QString text;                   ///< Characters of the run
        std::vector<QRectF> characters; ///< Bounding box of every character (page coordinates)
        PDFReal fontSize = 0.0;
        PDFReal baseline = 0.0;         ///< Y coordinate of the baseline
    };

    /// Finds the empty places of a form on a page: runs of underscores
    /// ("Name: ________"), empty check boxes (glyphs like ☐) and thin
    /// horizontal lines and small squares drawn on the page. A place, which
    /// overlaps an existing widget, is not returned.
    /// \param runs Text of the page, line by line
    /// \param horizontalLines Thin horizontal lines drawn on the page
    /// \param squares Small squares drawn on the page
    /// \param existingWidgets Rectangles of the existing widgets
    static std::vector<DetectedField> detectFields(const std::vector<TextRun>& runs,
                                                   const std::vector<QRectF>& horizontalLines,
                                                   const std::vector<QRectF>& squares,
                                                   const std::vector<QRectF>& existingWidgets);

    /// Creates a name of a field from its label ("First name:" -> "First name")
    static QString createNameFromLabel(QString label);

    /// Creates the appearance content stream of a widget - used by the field
    /// creation, exposed for the tests
    static QByteArray createAppearanceContent(const Settings& settings, QSizeF size, bool on);

    /// Width of the text in the standard font Helvetica, in the units of the font size
    static PDFReal getHelveticaTextWidth(const QString& text);

private:
    static PDFObjectReference getFieldOfWidget(const PDFObjectStorage* storage, PDFObjectReference widget);
    static void ensureAcroForm(PDFDocumentBuilder* builder);
    static void addToAcroFormFields(PDFDocumentBuilder* builder, PDFObjectReference field);
    static void removeFromAcroFormFields(PDFDocumentBuilder* builder, PDFObjectReference field);
    static void addToPageAnnotations(PDFDocumentBuilder* builder, PDFObjectReference page, PDFObjectReference widget);
    static void writeWidgetAppearance(PDFDocumentBuilder* builder, PDFObjectReference widget, const Settings& settings, QRectF rect, const QByteArray& onStateName);
    static void writeFieldEntries(PDFDocumentBuilder* builder, PDFObjectReference field, const Settings& settings, bool writeValue);
    static QByteArray getOnStateName(const PDFObjectStorage* storage, PDFObjectReference widget);
};

/// PDF Fire: reads a page for the detection of the empty places of a form - its
/// text, line by line, and the thin lines and small squares drawn on it
class PDF4QTLIBCORESHARED_EXPORT PDFFireFormFieldScanner : public PDFTextLayoutGenerator
{
    using BaseClass = PDFTextLayoutGenerator;

public:
    using BaseClass::BaseClass;

    struct Result
    {
        std::vector<PDFFireFormFields::TextRun> runs;
        std::vector<QRectF> horizontalLines;
        std::vector<QRectF> squares;
    };

    /// Processes the page and returns what was found
    Result scan();

protected:
    virtual bool isContentKindSuppressed(ContentKind kind) const override;
    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override;

private:
    std::vector<QRectF> m_horizontalLines;
    std::vector<QRectF> m_squares;
};

}   // namespace pdf

#endif // PDFFIREFORMFIELDS_H
