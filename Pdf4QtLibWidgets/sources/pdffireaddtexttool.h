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

#ifndef PDFFIREADDTEXTTOOL_H
#define PDFFIREADDTEXTTOOL_H

#include "pdfwidgettool.h"
#include "pdfannotation.h"

#include <QPlainTextEdit>
#include <QPointer>

class QFontComboBox;
class QSpinBox;
class QPushButton;

namespace pdf
{
class PDFToolManager;

/// PDF Fire: the text box, in which the text of "Add Text" is typed directly on the
/// page. It has no background (the page shows through it) and a thin dashed frame,
/// it grows with the text. Enter starts a new line; Esc, Ctrl+Enter or a click
/// outside finishes the typing.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFFireAddTextEditor : public QPlainTextEdit
{
    Q_OBJECT

private:
    using BaseClass = QPlainTextEdit;

public:
    explicit PDFFireAddTextEditor(QWidget* parent);

    /// Starts the typing at the position (the top left corner of the text, in the
    /// coordinates of the parent widget)
    void start(const QString& text, const QFont& font, const QColor& color, QPoint topLeft, bool coverPage);

    /// Changes the font and the colour while typing
    void setTextStyle(const QFont& font, const QColor& color);

    /// Finishes the typing - the text is committed (only once)
    void finish();

    /// Widgets, which may take the focus without finishing the typing (the style bar)
    void setFocusCompanion(QWidget* widget) { m_focusCompanion = widget; }

signals:
    void committed(const QString& text);

protected:
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void focusOutEvent(QFocusEvent* event) override;

private:
    void updateSize();
    void updateStyleSheet();

    bool m_isFinished = false;
    bool m_coverPage = false;
    QColor m_color = Qt::black;
    QPointer<QWidget> m_focusCompanion;
};

/// PDF Fire: "Add Text" of Acrobat - click anywhere on a page and type. The text is
/// a typewriter annotation (FreeText, /IT /FreeTextTypeWriter, no border), the same
/// as Acrobat's Fill & Sign text, so it is shown and printed by every viewer and it
/// can be changed later: a click on it with this tool opens it for typing again.
/// The selection tool moves it or deletes it, as any comment.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFFireAddTextTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFFireAddTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);
    virtual ~PDFFireAddTextTool() override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;

    /// The style of the new text (font family, size in points, colour)
    const PDFFreeTextStyle& getTextStyle() const { return m_style; }
    void setTextStyle(const PDFFreeTextStyle& style);

    /// Returns true, if the text is being typed now
    bool isEditing() const { return m_editor != nullptr; }

    /// Finishes the typing (the text is written into the document)
    void finishEditing();

protected:
    virtual void setActiveImpl(bool active) override;
    virtual void updateActions() override;

private:
    struct HitAnnotation
    {
        PDFObjectReference annotation;
        QRectF rectangle;
        QString contents;
        PDFFreeTextStyle style;
    };

    /// Finds the typewriter text under the point of the page
    bool findTypewriterText(PDFInteger pageIndex, QPointF pagePoint, HitAnnotation* result) const;

    void startEditing(QWidget* widget, PDFInteger pageIndex, QPointF pagePoint, QPoint devicePoint);
    void commit(const QString& text);
    void showStyleBar();
    void hideStyleBar();
    void updateEditorStyle();
    QFont createDeviceFont(const PDFFreeTextStyle& style) const;
    qreal getDeviceScale(PDFInteger pageIndex) const;

    void loadStyle();
    void saveStyle();

    PDFToolManager* m_toolManager;
    PDFFreeTextStyle m_style;

    // The text being typed
    QPointer<PDFFireAddTextEditor> m_editor;
    PDFInteger m_editPageIndex = -1;
    PDFObjectReference m_editAnnotation;     ///< Changed text (an invalid reference - a new text)
    QPointF m_editPageTopLeft;               ///< The top left corner of the text on the page

    // The bar with the font, the size and the colour
    QPointer<QWidget> m_styleBar;
    QFontComboBox* m_fontComboBox = nullptr;
    QSpinBox* m_sizeSpinBox = nullptr;
    QPushButton* m_colorButton = nullptr;
};

}   // namespace pdf

#endif // PDFFIREADDTEXTTOOL_H
