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

#include "pdffireformdesigntool.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfwidgetutils.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace pdf
{

namespace
{

/// Color of the form maker (the flame of the logo)
const QColor DESIGN_COLOR(232, 89, 12);

/// Size of a resize handle, and the tolerance of hitting it (in pixels)
constexpr int HANDLE_SIZE = 7;

/// Minimal size of a field (in points)
constexpr PDFReal MINIMAL_SIZE = 4.0;

}   // namespace

PDFFireFormDesignTool::PDFFireFormDesignTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QObject* parent) :
    BaseClass(proxy, parent),
    m_toolManager(toolManager)
{
    // The tool changes the document itself - it must not be turned off by that
    setKeepActiveOnReset(true);
}

void PDFFireFormDesignTool::setPlacedType(std::optional<PDFFireFormFields::Type> type)
{
    m_placedType = type;
    m_dragMode = DragMode::None;

    if (isActive())
    {
        if (m_placedType)
        {
            Q_EMIT messageDisplayRequest(tr("Click on the page to place a %1, or drag to give it a size. Click a field to select it. Esc ends the form editing.").arg(PDFFireFormFields::getTypeName(*m_placedType).toLower()), 10000);
        }
        Q_EMIT getProxy()->repaintNeeded();
    }
}

void PDFFireFormDesignTool::selectWidget(PDFObjectReference widget)
{
    PDFInteger pageIndex = -1;
    if (const PDFDocument* document = getDocument())
    {
        for (size_t i = 0; i < document->getCatalog()->getPageCount(); ++i)
        {
            const std::vector<PDFObjectReference> widgets = PDFFireFormFields::getWidgets(&document->getStorage(), getPageReference(PDFInteger(i)));
            if (std::find(widgets.cbegin(), widgets.cend(), widget) != widgets.cend())
            {
                pageIndex = PDFInteger(i);
                break;
            }
        }
    }

    setSelection(pageIndex != -1 ? widget : PDFObjectReference(), pageIndex);
}

void PDFFireFormDesignTool::setSelection(PDFObjectReference widget, PDFInteger pageIndex)
{
    if (m_selectedWidget != widget || m_selectedPageIndex != pageIndex)
    {
        m_selectedWidget = widget;
        m_selectedPageIndex = widget.isValid() ? pageIndex : -1;
        Q_EMIT selectionChanged();
    }
    Q_EMIT getProxy()->repaintNeeded();
}

bool PDFFireFormDesignTool::modify(const std::function<void(PDFDocumentBuilder*)>& change)
{
    if (!getDocument())
    {
        return false;
    }

    PDFDocumentModifier modifier(getDocument());
    change(modifier.getBuilder());

    // The form is parsed again only when the document is reset
    modifier.markReset();
    modifier.markAnnotationsChanged();
    modifier.markFormFieldChanged();

    if (modifier.finalize())
    {
        PDFModifiedDocument::ModificationFlags flags = modifier.getFlags();
        flags.setFlag(PDFModifiedDocument::PreserveUndoRedo);
        flags.setFlag(PDFModifiedDocument::PreserveView);
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, flags));
        return true;
    }

    return false;
}

void PDFFireFormDesignTool::deleteSelected()
{
    dropInvalidSelection();

    const PDFObjectReference widget = m_selectedWidget;
    if (!widget.isValid())
    {
        return;
    }

    setSelection(PDFObjectReference(), -1);
    modify([widget](PDFDocumentBuilder* builder) { PDFFireFormFields::removeWidget(builder, widget); });
}

void PDFFireFormDesignTool::duplicateSelected()
{
    dropInvalidSelection();

    if (!m_selectedWidget.isValid() || !getDocument())
    {
        return;
    }

    // The copy is placed under the original (or above it, at the bottom of the page)
    const PDFObjectReference widget = m_selectedWidget;
    QRectF rect = PDFFireFormFields::getWidgetRect(&getDocument()->getStorage(), widget);
    const PDFReal offset = rect.height() + 6.0;
    rect.translate(0.0, rect.top() - offset >= 0.0 ? -offset : offset);

    PDFObjectReference copy;
    if (modify([&](PDFDocumentBuilder* builder) { copy = PDFFireFormFields::duplicateField(builder, widget, rect); }))
    {
        setSelection(copy, m_selectedPageIndex);
    }
}

void PDFFireFormDesignTool::copySelected()
{
    dropInvalidSelection();
    if (m_selectedWidget.isValid())
    {
        m_copiedWidget = m_selectedWidget;
        m_copiedPageIndex = m_selectedPageIndex;
        m_pasteCount = 0;
    }
}

void PDFFireFormDesignTool::paste()
{
    const PDFDocument* document = getDocument();
    if (!m_copiedWidget.isValid() || !document || !document->getStorage().getObjectByReference(m_copiedWidget).isDictionary())
    {
        // The copied field was deleted (or the document was changed)
        m_copiedWidget = PDFObjectReference();
        return;
    }

    QRectF rect = PDFFireFormFields::getWidgetRect(&document->getStorage(), m_copiedWidget);
    QPointF pagePoint;
    const PDFInteger pageIndex = getProxy()->getPageUnderPoint(m_lastMousePosition, &pagePoint);
    if (pageIndex == m_copiedPageIndex && pageIndex >= 0 && !rect.contains(pagePoint))
    {
        // At the mouse: the top left corner of the copy is under the mouse
        rect.moveTopLeft(QPointF(pagePoint.x(), pagePoint.y() - rect.height()));
    }
    else
    {
        // Under the original, each further paste a step lower
        ++m_pasteCount;
        const PDFReal offset = (rect.height() + 6.0) * m_pasteCount;
        rect.translate(0.0, rect.top() - offset >= 0.0 ? -offset : offset);
    }

    const PDFObjectReference source = m_copiedWidget;
    PDFObjectReference copy;
    if (modify([&](PDFDocumentBuilder* builder) { copy = PDFFireFormFields::duplicateField(builder, source, rect); }))
    {
        setSelection(copy, m_copiedPageIndex);
    }
}

void PDFFireFormDesignTool::copySelectedToAllPages()
{
    dropInvalidSelection();

    const PDFDocument* document = getDocument();
    if (!m_selectedWidget.isValid() || !document)
    {
        return;
    }

    const PDFObjectReference widget = m_selectedWidget;
    const QRectF rect = PDFFireFormFields::getWidgetRect(&document->getStorage(), widget);
    std::optional<PDFFireFormFields::Settings> settings = PDFFireFormFields::readField(&document->getStorage(), widget);
    if (!settings)
    {
        return;
    }

    const PDFInteger sourcePageIndex = m_selectedPageIndex;
    const size_t pageCount = document->getCatalog()->getPageCount();
    QString base = settings->name;
    while (!base.isEmpty() && base.back().isDigit())
    {
        base.chop(1);
    }
    base = base.trimmed();

    modify([&](PDFDocumentBuilder* builder)
    {
        for (size_t i = 0; i < pageCount; ++i)
        {
            if (PDFInteger(i) == sourcePageIndex)
            {
                continue;
            }

            PDFFireFormFields::Settings pageSettings = *settings;
            if (pageSettings.type != PDFFireFormFields::Type::RadioButton)
            {
                pageSettings.name = PDFFireFormFields::createUniqueName(builder->getStorage(), base.isEmpty() ? QStringLiteral("Field") : base);
            }
            pageSettings.checked = false;
            pageSettings.exportValue.clear();
            PDFFireFormFields::createField(builder, document->getCatalog()->getPage(i)->getPageReference(), rect, pageSettings);
        }
    });

    Q_EMIT messageDisplayRequest(tr("The field was copied to the other %1 pages.").arg(pageCount - 1), 5000);
}

bool PDFFireFormDesignTool::isSelectionValid() const
{
    if (!m_selectedWidget.isValid() || !getDocument())
    {
        return false;
    }

    const std::vector<PDFObjectReference> widgets = PDFFireFormFields::getWidgets(&getDocument()->getStorage(), getPageReference(m_selectedPageIndex));
    return std::find(widgets.cbegin(), widgets.cend(), m_selectedWidget) != widgets.cend();
}

void PDFFireFormDesignTool::dropInvalidSelection()
{
    if (m_selectedWidget.isValid() && !isSelectionValid())
    {
        m_dragMode = DragMode::None;
        setSelection(PDFObjectReference(), -1);
    }
}

PDFObjectReference PDFFireFormDesignTool::getPageReference(PDFInteger pageIndex) const
{
    const PDFDocument* document = getDocument();
    if (!document || pageIndex < 0 || size_t(pageIndex) >= document->getCatalog()->getPageCount())
    {
        return PDFObjectReference();
    }
    return document->getCatalog()->getPage(pageIndex)->getPageReference();
}

QString PDFFireFormDesignTool::getWidgetLabel(PDFObjectReference widget) const
{
    const std::optional<PDFFireFormFields::Settings> settings = PDFFireFormFields::readField(&getDocument()->getStorage(), widget);
    if (!settings)
    {
        return tr("(field)");
    }

    QString label = settings->name;
    if (settings->type == PDFFireFormFields::Type::RadioButton && !settings->exportValue.isEmpty())
    {
        label += QStringLiteral(" = ") + settings->exportValue;
    }
    if (settings->required)
    {
        label += QStringLiteral(" *");
    }
    return label;
}

std::array<QPointF, 8> PDFFireFormDesignTool::getHandlePoints(const QRectF& rect)
{
    return { rect.topLeft(), QPointF(rect.center().x(), rect.top()), rect.topRight(), QPointF(rect.right(), rect.center().y()),
             rect.bottomRight(), QPointF(rect.center().x(), rect.bottom()), rect.bottomLeft(), QPointF(rect.left(), rect.center().y()) };
}

PDFFireFormDesignTool::HitResult PDFFireFormDesignTool::hitTest(QPoint position) const
{
    HitResult result;
    const PDFDocument* document = getDocument();
    if (!document)
    {
        return result;
    }

    // The handles of the selected field first
    if (isSelectionValid())
    {
        auto it = m_pageTransforms.find(m_selectedPageIndex);
        if (it != m_pageTransforms.cend())
        {
            const QRectF rect = PDFFireFormFields::getWidgetRect(&document->getStorage(), m_selectedWidget);
            const std::array<QPointF, 8> handles = getHandlePoints(rect);
            for (int i = 0; i < int(handles.size()); ++i)
            {
                const QPointF devicePoint = it->second.map(handles[i]);
                if (qAbs(devicePoint.x() - position.x()) <= HANDLE_SIZE && qAbs(devicePoint.y() - position.y()) <= HANDLE_SIZE)
                {
                    result.widget = m_selectedWidget;
                    result.pageIndex = m_selectedPageIndex;
                    result.handle = i;
                    return result;
                }
            }
        }
    }

    QPointF pagePoint;
    const PDFInteger pageIndex = getProxy()->getPageUnderPoint(position, &pagePoint);
    if (pageIndex == -1)
    {
        return result;
    }

    result.pageIndex = pageIndex;

    // The last widget is on the top
    const std::vector<PDFObjectReference> widgets = PDFFireFormFields::getWidgets(&document->getStorage(), getPageReference(pageIndex));
    for (auto it = widgets.crbegin(); it != widgets.crend(); ++it)
    {
        // Small fields (check boxes) are easier to hit with a margin of a few pixels
        QRectF rect = PDFFireFormFields::getWidgetRect(&document->getStorage(), *it);
        auto transformIt = m_pageTransforms.find(pageIndex);
        if (transformIt != m_pageTransforms.cend())
        {
            const QRectF deviceRect = transformIt->second.mapRect(rect).adjusted(-3, -3, 3, 3);
            if (deviceRect.contains(position))
            {
                result.widget = *it;
                return result;
            }
        }
        else if (rect.contains(pagePoint))
        {
            result.widget = *it;
            return result;
        }
    }

    return result;
}

QRectF PDFFireFormDesignTool::getResizedRect() const
{
    QRectF rect = m_dragOriginalRect;
    switch (m_dragHandle)
    {
        case 0: rect.setTopLeft(m_dragCurrent); break;
        case 1: rect.setTop(m_dragCurrent.y()); break;
        case 2: rect.setTopRight(m_dragCurrent); break;
        case 3: rect.setRight(m_dragCurrent.x()); break;
        case 4: rect.setBottomRight(m_dragCurrent); break;
        case 5: rect.setBottom(m_dragCurrent.y()); break;
        case 6: rect.setBottomLeft(m_dragCurrent); break;
        case 7: rect.setLeft(m_dragCurrent.x()); break;
        default: break;
    }

    rect = rect.normalized();
    rect.setWidth(qMax(rect.width(), MINIMAL_SIZE));
    rect.setHeight(qMax(rect.height(), MINIMAL_SIZE));
    return rect;
}

QString PDFFireFormDesignTool::createFieldName(PDFFireFormFields::Type type) const
{
    QString base;
    switch (type)
    {
        case PDFFireFormFields::Type::Text: base = QStringLiteral("Text"); break;
        case PDFFireFormFields::Type::MultilineText: base = QStringLiteral("Notes"); break;
        case PDFFireFormFields::Type::Date: base = QStringLiteral("Date"); break;
        case PDFFireFormFields::Type::CheckBox: base = QStringLiteral("Check Box"); break;
        case PDFFireFormFields::Type::RadioButton: base = QStringLiteral("Group"); break;
        case PDFFireFormFields::Type::ComboBox: base = QStringLiteral("Dropdown"); break;
        case PDFFireFormFields::Type::ListBox: base = QStringLiteral("List"); break;
        case PDFFireFormFields::Type::Signature: base = QStringLiteral("Signature"); break;
    }
    return PDFFireFormFields::createUniqueName(&getDocument()->getStorage(), base);
}

void PDFFireFormDesignTool::drawPage(QPainter* painter,
                                     PDFInteger pageIndex,
                                     const PDFPrecompiledPage* compiledPage,
                                     PDFTextLayoutGetter& layoutGetter,
                                     const QTransform& pagePointToDevicePointMatrix,
                                     const PDFColorConvertor& convertor,
                                     QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(convertor);
    Q_UNUSED(errors);

    m_pageTransforms[pageIndex] = pagePointToDevicePointMatrix;

    const PDFDocument* document = getDocument();
    if (!document)
    {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QFont labelFont = painter->font();
    labelFont.setPixelSize(PDFWidgetUtils::scaleDPI_y(getProxy()->getWidget(), 11));
    painter->setFont(labelFont);
    const QFontMetrics fontMetrics(labelFont);

    QColor fillColor = DESIGN_COLOR;
    fillColor.setAlpha(28);

    for (PDFObjectReference widget : PDFFireFormFields::getWidgets(&document->getStorage(), getPageReference(pageIndex)))
    {
        QRectF rect = PDFFireFormFields::getWidgetRect(&document->getStorage(), widget);
        const bool isSelected = (widget == m_selectedWidget);

        if (isSelected && m_dragMode == DragMode::Move && m_dragPageIndex == pageIndex)
        {
            rect.translate(m_dragCurrent - m_dragStart);
        }
        else if (isSelected && m_dragMode == DragMode::Resize && m_dragPageIndex == pageIndex)
        {
            rect = getResizedRect();
        }

        const QRectF deviceRect = pagePointToDevicePointMatrix.mapRect(rect);
        painter->setPen(QPen(DESIGN_COLOR, isSelected ? 2.0 : 1.0, isSelected ? Qt::SolidLine : Qt::DashLine));
        painter->setBrush(fillColor);
        painter->drawRect(deviceRect);

        // The name of the field above it
        const QString label = getWidgetLabel(widget);
        const QRectF labelRect(deviceRect.left(), deviceRect.top() - fontMetrics.height() - 2, fontMetrics.horizontalAdvance(label) + 8, fontMetrics.height() + 2);
        painter->setPen(Qt::NoPen);
        painter->setBrush(isSelected ? DESIGN_COLOR : DESIGN_COLOR.lighter(130));
        painter->drawRoundedRect(labelRect, 2, 2);
        painter->setPen(Qt::white);
        painter->drawText(labelRect, Qt::AlignCenter, label);

        if (isSelected)
        {
            painter->setPen(QPen(DESIGN_COLOR, 1.0));
            painter->setBrush(Qt::white);
            for (const QPointF& handle : getHandlePoints(rect))
            {
                const QPointF center = pagePointToDevicePointMatrix.map(handle);
                painter->drawRect(QRectF(center - QPointF(HANDLE_SIZE * 0.5, HANDLE_SIZE * 0.5), QSizeF(HANDLE_SIZE, HANDLE_SIZE)));
            }
        }
    }

    // The field being placed
    if (m_dragMode == DragMode::Create && m_dragPageIndex == pageIndex)
    {
        const QRectF deviceRect = pagePointToDevicePointMatrix.mapRect(QRectF(m_dragStart, m_dragCurrent).normalized());
        painter->setPen(QPen(DESIGN_COLOR, 1.5, Qt::DashLine));
        painter->setBrush(fillColor);
        painter->drawRect(deviceRect);
    }

    painter->restore();
}

void PDFFireFormDesignTool::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    dropInvalidSelection();

    // The keys of the form maker are not taken by the shortcuts of the application
    if (m_selectedWidget.isValid())
    {
        switch (event->key())
        {
            case Qt::Key_Delete:
            case Qt::Key_Backspace:
            case Qt::Key_Left:
            case Qt::Key_Right:
            case Qt::Key_Up:
            case Qt::Key_Down:
            case Qt::Key_Return:
            case Qt::Key_Enter:
                event->accept();
                return;

            case Qt::Key_D:
            case Qt::Key_C:
                if (event->modifiers().testFlag(Qt::ControlModifier))
                {
                    event->accept();
                    return;
                }
                break;

            default:
                break;
        }
    }

    // Ctrl+V pastes the copied field, also when nothing is selected
    if (m_copiedWidget.isValid() && event->key() == Qt::Key_V && event->modifiers().testFlag(Qt::ControlModifier))
    {
        event->accept();
        return;
    }

    event->ignore();
}

void PDFFireFormDesignTool::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
    dropInvalidSelection();

    if (event->key() == Qt::Key_V && event->modifiers().testFlag(Qt::ControlModifier) && m_copiedWidget.isValid())
    {
        event->accept();
        paste();
        return;
    }

    if (!m_selectedWidget.isValid() || !getDocument())
    {
        return;
    }

    QPointF move;
    const PDFReal step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
    switch (event->key())
    {
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            event->accept();
            deleteSelected();
            return;

        case Qt::Key_Return:
        case Qt::Key_Enter:
            event->accept();
            Q_EMIT propertiesRequested();
            return;

        case Qt::Key_D:
            if (event->modifiers().testFlag(Qt::ControlModifier))
            {
                event->accept();
                duplicateSelected();
            }
            return;

        case Qt::Key_C:
            if (event->modifiers().testFlag(Qt::ControlModifier))
            {
                event->accept();
                copySelected();
            }
            return;

        // The y axis of a page goes up
        case Qt::Key_Left: move = QPointF(-step, 0.0); break;
        case Qt::Key_Right: move = QPointF(step, 0.0); break;
        case Qt::Key_Up: move = QPointF(0.0, step); break;
        case Qt::Key_Down: move = QPointF(0.0, -step); break;

        default:
            return;
    }

    event->accept();
    const PDFObjectReference selectedWidget = m_selectedWidget;
    const QRectF rect = PDFFireFormFields::getWidgetRect(&getDocument()->getStorage(), selectedWidget).translated(move);
    modify([&](PDFDocumentBuilder* builder) { PDFFireFormFields::setWidgetRect(builder, selectedWidget, rect); });
}

void PDFFireFormDesignTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (!getDocument())
    {
        return;
    }

    dropInvalidSelection();
    const QPoint position = event->position().toPoint();
    const HitResult hit = hitTest(position);

    if (event->button() == Qt::RightButton)
    {
        event->accept();
        if (hit.widget.isValid())
        {
            setSelection(hit.widget, hit.pageIndex);
            Q_EMIT contextMenuRequested(event->globalPosition().toPoint());
        }
        return;
    }

    if (event->button() != Qt::LeftButton || hit.pageIndex == -1)
    {
        return;
    }

    event->accept();

    QPointF pagePoint;
    getProxy()->getPageUnderPoint(position, &pagePoint);
    m_dragPageIndex = hit.pageIndex;
    m_dragMoved = false;

    if (hit.widget.isValid())
    {
        setSelection(hit.widget, hit.pageIndex);
        m_dragOriginalRect = PDFFireFormFields::getWidgetRect(&getDocument()->getStorage(), hit.widget);
        m_dragHandle = hit.handle;

        // A handle can be at the edge of the page - its point is then its own point
        m_dragStart = (hit.handle >= 0) ? getHandlePoints(m_dragOriginalRect)[hit.handle] : pagePoint;
        m_dragCurrent = m_dragStart;
        m_dragMode = (hit.handle >= 0) ? DragMode::Resize : DragMode::Move;
    }
    else if (m_placedType)
    {
        m_dragStart = pagePoint;
        m_dragCurrent = pagePoint;
        m_dragMode = DragMode::Create;
    }
    else
    {
        setSelection(PDFObjectReference(), -1);
        m_dragMode = DragMode::None;
    }
}

void PDFFireFormDesignTool::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    const HitResult hit = hitTest(event->position().toPoint());
    if (hit.widget.isValid())
    {
        event->accept();
        m_dragMode = DragMode::None;
        setSelection(hit.widget, hit.pageIndex);
        Q_EMIT propertiesRequested();
    }
}

void PDFFireFormDesignTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    const QPoint position = event->position().toPoint();
    m_lastMousePosition = position;
    if (m_dragMode == DragMode::None)
    {
        updateCursor(position);
        return;
    }

    event->accept();

    QPointF pagePoint;
    const PDFInteger pageIndex = getProxy()->getPageUnderPoint(position, &pagePoint);
    if (pageIndex == m_dragPageIndex)
    {
        if (!m_dragMoved)
        {
            // A small shake of the mouse at the click is not a drag
            const QTransform transform = m_pageTransforms.count(pageIndex) ? m_pageTransforms[pageIndex] : QTransform();
            m_dragMoved = QLineF(transform.map(m_dragStart), transform.map(pagePoint)).length() > 3.0;
        }

        if (m_dragMode == DragMode::Resize)
        {
            m_dragCurrent = pagePoint;
        }
        else
        {
            m_dragCurrent = pagePoint;
        }
        Q_EMIT getProxy()->repaintNeeded();
    }
}

void PDFFireFormDesignTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() != Qt::LeftButton || m_dragMode == DragMode::None)
    {
        return;
    }

    event->accept();

    const DragMode dragMode = m_dragMode;
    m_dragMode = DragMode::None;
    const PDFObjectReference page = getPageReference(m_dragPageIndex);

    switch (dragMode)
    {
        case DragMode::Create:
        {
            const PDFFireFormFields::Type type = *m_placedType;
            QRectF rect = QRectF(m_dragStart, m_dragCurrent).normalized();
            if (!m_dragMoved || rect.width() < MINIMAL_SIZE || rect.height() < MINIMAL_SIZE)
            {
                // A click: the field of the default size, its top left corner at the click
                const QSizeF size = PDFFireFormFields::getDefaultSize(type);
                rect = QRectF(QPointF(m_dragStart.x(), m_dragStart.y() - size.height()), size);
            }

            PDFFireFormFields::Settings settings = PDFFireFormFields::getDefaultSettings(type);
            settings.name = createFieldName(type);

            // A radio button placed while another radio button is selected joins its group
            if (type == PDFFireFormFields::Type::RadioButton && m_selectedWidget.isValid())
            {
                const std::optional<PDFFireFormFields::Settings> selected = PDFFireFormFields::readField(&getDocument()->getStorage(), m_selectedWidget);
                if (selected && selected->type == PDFFireFormFields::Type::RadioButton)
                {
                    settings.name = selected->name;
                }
            }
            if (type == PDFFireFormFields::Type::RadioButton)
            {
                settings.exportValue.clear();
            }

            PDFObjectReference created;
            const PDFInteger pageIndex = m_dragPageIndex;
            if (modify([&](PDFDocumentBuilder* builder) { created = PDFFireFormFields::createField(builder, page, rect, settings); }))
            {
                setSelection(created, pageIndex);
            }
            break;
        }

        case DragMode::Move:
        case DragMode::Resize:
        {
            if (!m_dragMoved)
            {
                break;
            }

            const PDFObjectReference selectedWidget = m_selectedWidget;
            const QRectF rect = (dragMode == DragMode::Move) ? m_dragOriginalRect.translated(m_dragCurrent - m_dragStart) : getResizedRect();
            modify([&](PDFDocumentBuilder* builder) { PDFFireFormFields::setWidgetRect(builder, selectedWidget, rect); });
            break;
        }

        case DragMode::None:
            break;
    }

    Q_EMIT getProxy()->repaintNeeded();
}

void PDFFireFormDesignTool::updateCursor(QPoint position)
{
    const HitResult hit = hitTest(position);
    if (hit.handle >= 0)
    {
        // Handles 0, 4 and 2, 6 are corners; the direction on the screen follows
        // the page (the y axis of the page goes up)
        static const Qt::CursorShape shapes[] = { Qt::SizeBDiagCursor, Qt::SizeVerCursor, Qt::SizeFDiagCursor, Qt::SizeHorCursor,
                                                  Qt::SizeBDiagCursor, Qt::SizeVerCursor, Qt::SizeFDiagCursor, Qt::SizeHorCursor };
        setCursor(QCursor(shapes[hit.handle]));
    }
    else if (hit.widget.isValid())
    {
        setCursor(QCursor(Qt::SizeAllCursor));
    }
    else if (m_placedType && hit.pageIndex != -1)
    {
        setCursor(QCursor(Qt::CrossCursor));
    }
    else
    {
        setCursor(QCursor(Qt::ArrowCursor));
    }
}

void PDFFireFormDesignTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    m_dragMode = DragMode::None;
    if (!active)
    {
        m_placedType = std::nullopt;
        setSelection(PDFObjectReference(), -1);
        m_pageTransforms.clear();
    }
    else
    {
        setCursor(QCursor(Qt::ArrowCursor));
        Q_EMIT messageDisplayRequest(tr("Editing the form: the fields are shown with their names. To fill in the form, click Edit Fields again (or press Esc)."), 10000);
    }
}

}   // namespace pdf
