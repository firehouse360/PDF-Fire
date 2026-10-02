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

#ifndef PDFFIREFORMDESIGNTOOL_H
#define PDFFIREFORMDESIGNTOOL_H

#include "pdfwidgettool.h"
#include "pdffireformfields.h"

#include <array>
#include <functional>
#include <map>
#include <optional>

namespace pdf
{
class PDFDocumentBuilder;

/// PDF Fire: the tool of the form maker. When a type of a field is chosen, a click
/// places a field of the default size, and a drag places a field of the dragged size.
/// The fields are shown with their names; a click selects a field, which can then be
/// moved and resized by the mouse (or moved by the arrow keys), deleted by the Delete
/// key, copied by Ctrl+D, and its properties opened by a double click.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFFireFormDesignTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFFireFormDesignTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QObject* parent);

    /// Sets the type of the fields placed by the mouse. Nothing - the fields
    /// are only selected and edited.
    void setPlacedType(std::optional<PDFFireFormFields::Type> type);
    std::optional<PDFFireFormFields::Type> getPlacedType() const { return m_placedType; }

    /// Returns the selected widget - nothing, if the widget is not in the document
    /// any more (an undo can remove it)
    PDFObjectReference getSelectedWidget() const { return isSelectionValid() ? m_selectedWidget : PDFObjectReference(); }
    void selectWidget(PDFObjectReference widget);

    /// Changes the document: the change is made by the builder, then the
    /// modified document is set to the application (the view is kept)
    bool modify(const std::function<void(PDFDocumentBuilder*)>& change);

    void deleteSelected();
    void duplicateSelected();

    /// PDF Fire: copies the selected field (Ctrl+C)
    void copySelected();

    /// PDF Fire: pastes a copy of the copied field (Ctrl+V) - at the mouse, when it is
    /// over the page of the field, else under the field (each paste a step further)
    void paste();
    void copySelectedToAllPages();

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

signals:
    void selectionChanged();
    void propertiesRequested();
    void contextMenuRequested(QPoint globalPosition);

protected:
    virtual void setActiveImpl(bool active) override;

private:
    enum class DragMode
    {
        None,
        Create,
        Move,
        Resize
    };

    struct HitResult
    {
        PDFObjectReference widget;
        PDFInteger pageIndex = -1;
        int handle = -1;    ///< Index of the resize handle of the selected widget, or -1
    };

    HitResult hitTest(QPoint position) const;
    bool isSelectionValid() const;
    void dropInvalidSelection();
    PDFObjectReference getPageReference(PDFInteger pageIndex) const;
    QString getWidgetLabel(PDFObjectReference widget) const;
    QRectF getResizedRect() const;
    void updateCursor(QPoint position);
    void setSelection(PDFObjectReference widget, PDFInteger pageIndex);
    QString createFieldName(PDFFireFormFields::Type type) const;

    /// Points of the page rectangle, where the resize handles are
    static std::array<QPointF, 8> getHandlePoints(const QRectF& rect);

    PDFToolManager* m_toolManager;
    std::optional<PDFFireFormFields::Type> m_placedType;
    PDFObjectReference m_selectedWidget;
    PDFInteger m_selectedPageIndex = -1;

    // PDF Fire: Ctrl+C / Ctrl+V - the copied field, and where the mouse is
    PDFObjectReference m_copiedWidget;
    PDFInteger m_copiedPageIndex = -1;
    int m_pasteCount = 0;
    QPoint m_lastMousePosition;

    DragMode m_dragMode = DragMode::None;
    PDFInteger m_dragPageIndex = -1;
    QPointF m_dragStart;
    QPointF m_dragCurrent;
    QRectF m_dragOriginalRect;
    int m_dragHandle = -1;
    bool m_dragMoved = false;

    /// Transformations of the pages, as they were drawn last time (for hit tests)
    mutable std::map<PDFInteger, QTransform> m_pageTransforms;
};

}   // namespace pdf

#endif // PDFFIREFORMDESIGNTOOL_H
