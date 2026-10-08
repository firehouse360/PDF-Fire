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

#include "pdfannotationstyle.h"
#include "pdfwidgetutils.h"

#include <QCheckBox>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QColorDialog>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QPushButton>
#include <QSettings>

#include "pdfdbgheap.h"

namespace pdf
{

static QSettings createSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
}

PDFAnnotationStyle PDFAnnotationStyleSettings::getStyle(const QString& styleId, const PDFAnnotationStyle& defaultStyle)
{
    PDFAnnotationStyle style = defaultStyle;

    QSettings settings = createSettings();
    settings.beginGroup("AnnotationStyles");
    settings.beginGroup(styleId);

    const QColor strokeColor = settings.value("strokeColor", defaultStyle.strokeColor).value<QColor>();
    if (strokeColor.isValid())
    {
        style.strokeColor = strokeColor;
    }

    // Jakub Melka: invalid fill color is a valid value - it means, that the shape
    // is not filled at all. Therefore we must distinguish between "not stored yet"
    // and "stored as invalid".
    if (settings.contains("fillColor"))
    {
        style.fillColor = settings.value("fillColor").value<QColor>();
    }

    style.penWidth = qBound(0.0, settings.value("penWidth", defaultStyle.penWidth).toDouble(), 100.0);

    settings.endGroup();
    settings.endGroup();

    return style;
}

void PDFAnnotationStyleSettings::setStyle(const QString& styleId, const PDFAnnotationStyle& style)
{
    QSettings settings = createSettings();
    settings.beginGroup("AnnotationStyles");
    settings.beginGroup(styleId);
    settings.setValue("strokeColor", style.strokeColor);
    settings.setValue("fillColor", style.fillColor);
    settings.setValue("penWidth", style.penWidth);
    settings.endGroup();
    settings.endGroup();
}

PDFAnnotationStyleWidget::PDFAnnotationStyleWidget(QWidget* parent, StyleItems items, const PDFAnnotationStyle& style) :
    BaseClass(parent, Qt::Tool | Qt::CustomizeWindowHint | Qt::WindowTitleHint),
    m_items(items),
    m_style(style),
    m_strokeColorButton(nullptr),
    m_fillColorButton(nullptr),
    m_fillEnabledCheckBox(nullptr),
    m_penWidthEdit(nullptr)
{
    setWindowTitle(tr("Annotation Style"));

    QFormLayout* layout = new QFormLayout(this);

    if (m_items.testFlag(StrokeColor))
    {
        m_strokeColorButton = new QPushButton(this);
        m_strokeColorButton->setAutoDefault(false);
        connect(m_strokeColorButton, &QPushButton::clicked, this, &PDFAnnotationStyleWidget::onStrokeColorButtonClicked);
        layout->addRow(tr("Color"), m_strokeColorButton);
        layout->addRow(QString(), createColorSwatches(false));
    }

    if (m_items.testFlag(FillColor))
    {
        m_fillEnabledCheckBox = new QCheckBox(tr("Fill"), this);
        m_fillEnabledCheckBox->setChecked(m_style.fillColor.isValid());
        connect(m_fillEnabledCheckBox, &QCheckBox::toggled, this, &PDFAnnotationStyleWidget::onFillEnabledToggled);

        m_fillColorButton = new QPushButton(this);
        m_fillColorButton->setAutoDefault(false);
        m_fillColorButton->setEnabled(m_style.fillColor.isValid());
        connect(m_fillColorButton, &QPushButton::clicked, this, &PDFAnnotationStyleWidget::onFillColorButtonClicked);

        layout->addRow(m_fillEnabledCheckBox, m_fillColorButton);
        layout->addRow(QString(), createColorSwatches(true));
    }

    if (m_items.testFlag(PenWidth))
    {
        m_penWidthEdit = new QDoubleSpinBox(this);
        m_penWidthEdit->setRange(0.0, 100.0);
        m_penWidthEdit->setSingleStep(0.5);
        m_penWidthEdit->setDecimals(1);
        m_penWidthEdit->setValue(m_style.penWidth);
        connect(m_penWidthEdit, &QDoubleSpinBox::valueChanged, this, &PDFAnnotationStyleWidget::onPenWidthChanged);
        layout->addRow(tr("Pen width"), m_penWidthEdit);
    }

    updateColorButtons();
    adjustSize();
}

PDFAnnotationStyleWidget::~PDFAnnotationStyleWidget()
{

}

namespace
{
QPointer<QWidget> s_optionsPanel;

QDockWidget* findDock(QWidget* widget)
{
    for (QWidget* parent = widget; parent; parent = parent->parentWidget())
    {
        if (QDockWidget* dock = qobject_cast<QDockWidget*>(parent))
        {
            return dock;
        }
    }
    return nullptr;
}
}   // namespace

void PDFAnnotationStyleWidget::setOptionsPanel(QWidget* panel)
{
    s_optionsPanel = panel;
}

QWidget* PDFAnnotationStyleWidget::createColorSwatches(bool isFill)
{
    QWidget* row = new QWidget(this);
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(PDFWidgetUtils::scaleDPI_x(this, 3));

    const int size = PDFWidgetUtils::scaleDPI_x(this, 18);
    const std::pair<QColor, QString> colors[] = { { QColor(Qt::black), tr("Black") }, { QColor(Qt::white), tr("White") },
                                                  { QColor(220, 0, 0), tr("Red") }, { QColor(255, 220, 0), tr("Yellow") },
                                                  { QColor(0, 170, 0), tr("Green") }, { QColor(0, 90, 220), tr("Blue") },
                                                  { QColor(255, 140, 0), tr("Orange") }, { QColor(128, 128, 128), tr("Gray") } };
    for (const auto& [color, name] : colors)
    {
        QPushButton* swatch = new QPushButton(row);
        swatch->setFixedSize(size, size);
        swatch->setAutoDefault(false);
        swatch->setToolTip(name);
        swatch->setStyleSheet(QString("QPushButton { background-color: %1; border: 1px solid #888888; border-radius: 3px; }"
                                      "QPushButton:hover { border: 2px solid #ff6a2c; }").arg(color.name()));
        connect(swatch, &QPushButton::clicked, this, [this, isFill, color]() { setColor(isFill, color); });
        rowLayout->addWidget(swatch);
    }
    rowLayout->addStretch(1);
    return row;
}

void PDFAnnotationStyleWidget::setColor(bool isFill, const QColor& color)
{
    if (isFill)
    {
        if (m_fillEnabledCheckBox && !m_fillEnabledCheckBox->isChecked())
        {
            const QSignalBlocker blocker(m_fillEnabledCheckBox);
            m_fillEnabledCheckBox->setChecked(true);
            if (m_fillColorButton)
            {
                m_fillColorButton->setEnabled(true);
            }
        }
        m_style.fillColor = color;
    }
    else
    {
        m_style.strokeColor = color;
    }

    updateColorButtons();
    Q_EMIT styleChanged(m_style);
}

void PDFAnnotationStyleWidget::showStyleWindow()
{
    // PDF Fire: in the options panel at the side of the window, it does not cover the page
    if (QWidget* panel = s_optionsPanel.data())
    {
        if (QBoxLayout* panelLayout = qobject_cast<QBoxLayout*>(panel->layout()))
        {
            m_isInPanel = true;
            setWindowFlags(Qt::Widget);
            setParent(panel);
            panelLayout->insertWidget(0, this);
            show();
            if (QDockWidget* dock = findDock(panel))
            {
                dock->show();
                dock->raise();
            }
            return;
        }
    }

    QSettings settings = createSettings();
    settings.beginGroup("AnnotationStyles");
    const QPoint position = settings.value("windowPosition").toPoint();
    settings.endGroup();

    // Jakub Melka: the remembered position can be outside of the available screens
    // (for example when the user disconnects a monitor), in that case we must fall
    // back to the default position - otherwise the window would be invisible.
    bool isPositionUsable = false;
    if (!position.isNull())
    {
        for (const QScreen* screen : QGuiApplication::screens())
        {
            if (screen->availableGeometry().contains(position))
            {
                isPositionUsable = true;
                break;
            }
        }
    }

    if (isPositionUsable)
    {
        move(position);
    }
    else if (QWidget* parent = parentWidget())
    {
        // Default position is the top right corner of the draw widget, so that
        // the window doesn't cover the beginning of the text on the page.
        const int offset = PDFWidgetUtils::scaleDPI_x(this, 16);
        move(parent->mapToGlobal(QPoint(qMax(0, parent->width() - sizeHint().width() - offset), offset)));
    }

    show();
}

void PDFAnnotationStyleWidget::closeStyleWindow()
{
    if (m_isInPanel)
    {
        hide();
        QWidget* panel = parentWidget();
        if (panel && panel->layout())
        {
            panel->layout()->removeWidget(this);
        }

        // The panel is hidden, when no other tool shows its options in it
        bool isPanelUsed = false;
        if (panel)
        {
            for (const PDFAnnotationStyleWidget* other : panel->findChildren<PDFAnnotationStyleWidget*>(Qt::FindDirectChildrenOnly))
            {
                isPanelUsed = isPanelUsed || (other != this && other->isVisibleTo(panel));
            }
        }
        if (!isPanelUsed)
        {
            if (QDockWidget* dock = findDock(panel))
            {
                dock->hide();
            }
        }

        disconnect();
        deleteLater();
        return;
    }

    QSettings settings = createSettings();
    settings.beginGroup("AnnotationStyles");
    settings.setValue("windowPosition", pos());
    settings.endGroup();

    hide();

    // Jakub Melka: this function is called from the deactivation of a tool, which
    // can happen while an event is being delivered (for example when the user
    // clicks on the action of another tool). Deleting the widget directly could
    // destroy an object, which is still being used by the event delivery, so
    // the deletion is deferred to the event loop.
    disconnect();
    deleteLater();
}

void PDFAnnotationStyleWidget::onStrokeColorButtonClicked()
{
    QColor color = selectColor(m_style.strokeColor, tr("Select Color"));
    if (color.isValid() && color != m_style.strokeColor)
    {
        m_style.strokeColor = color;
        updateColorButtons();
        Q_EMIT styleChanged(m_style);
    }
}

void PDFAnnotationStyleWidget::onFillColorButtonClicked()
{
    QColor color = selectColor(m_style.fillColor.isValid() ? m_style.fillColor : m_style.strokeColor, tr("Select Fill Color"));
    if (color.isValid() && color != m_style.fillColor)
    {
        m_style.fillColor = color;
        updateColorButtons();
        Q_EMIT styleChanged(m_style);
    }
}

void PDFAnnotationStyleWidget::onFillEnabledToggled(bool checked)
{
    if (m_fillColorButton)
    {
        m_fillColorButton->setEnabled(checked);
    }

    if (checked)
    {
        if (!m_style.fillColor.isValid())
        {
            m_style.fillColor = m_style.strokeColor;
        }
    }
    else
    {
        m_style.fillColor = QColor();
    }

    updateColorButtons();
    Q_EMIT styleChanged(m_style);
}

void PDFAnnotationStyleWidget::onPenWidthChanged(double value)
{
    if (!qFuzzyCompare(m_style.penWidth, value))
    {
        m_style.penWidth = value;
        Q_EMIT styleChanged(m_style);
    }
}

void PDFAnnotationStyleWidget::updateColorButton(QPushButton* button, QColor color)
{
    if (!button)
    {
        return;
    }

    if (!color.isValid())
    {
        button->setIcon(QIcon());
        button->setText(tr("None"));
        return;
    }

    const int size = PDFWidgetUtils::scaleDPI_x(button, 16);
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setPen(QPen(button->palette().color(QPalette::WindowText)));
    painter.setBrush(QBrush(color));
    painter.drawRect(0, 0, size - 1, size - 1);
    painter.end();

    button->setIcon(QIcon(pixmap));
    button->setText(color.name(QColor::HexRgb));
}

void PDFAnnotationStyleWidget::updateColorButtons()
{
    updateColorButton(m_strokeColorButton, m_style.strokeColor);
    updateColorButton(m_fillColorButton, m_style.fillColor);
}

PDFAnnotationStyleManager::PDFAnnotationStyleManager(QObject* parent,
                                                     PDFAnnotationStyleWidget::StyleItems items,
                                                     PDFAnnotationStyle defaultStyle,
                                                     QString styleId) :
    BaseClass(parent),
    m_items(items),
    m_defaultStyle(std::move(defaultStyle)),
    m_styleId(std::move(styleId)),
    m_styleWidget(nullptr)
{
    m_style = PDFAnnotationStyleSettings::getStyle(m_styleId, m_defaultStyle);
}

void PDFAnnotationStyleManager::setStyle(PDFAnnotationStyle style)
{
    if (m_style == style)
    {
        return;
    }

    m_style = std::move(style);
    PDFAnnotationStyleSettings::setStyle(m_styleId, m_style);
    recreateStyleWindow();

    Q_EMIT styleChanged(m_style);
}

void PDFAnnotationStyleManager::setStyleId(QString styleId, PDFAnnotationStyle defaultStyle)
{
    if (m_styleId == styleId)
    {
        return;
    }

    m_styleId = std::move(styleId);
    m_defaultStyle = std::move(defaultStyle);
    m_style = PDFAnnotationStyleSettings::getStyle(m_styleId, m_defaultStyle);
    recreateStyleWindow();

    Q_EMIT styleChanged(m_style);
}

void PDFAnnotationStyleManager::recreateStyleWindow()
{
    if (m_styleWidget)
    {
        // Style window shows the old style - recreate it, so the user sees
        // the style, which will be really used.
        QWidget* parentWidget = m_styleWidget->parentWidget();
        closeStyleWindow();
        showStyleWindow(parentWidget);
    }
}

void PDFAnnotationStyleManager::reloadStyle()
{
    PDFAnnotationStyle style = PDFAnnotationStyleSettings::getStyle(m_styleId, m_defaultStyle);

    if (m_style != style)
    {
        m_style = std::move(style);
        recreateStyleWindow();
        Q_EMIT styleChanged(m_style);
    }
}

void PDFAnnotationStyleManager::showStyleWindow(QWidget* parentWidget)
{
    if (m_styleWidget)
    {
        return;
    }

    // Jakub Melka: style can be changed by another tool, which shares the same
    // style id, so it must be reloaded before the window is created from it.
    reloadStyle();

    m_styleWidget = new PDFAnnotationStyleWidget(parentWidget, m_items, m_style);
    connect(m_styleWidget, &PDFAnnotationStyleWidget::styleChanged, this, &PDFAnnotationStyleManager::onStyleChanged);
    m_styleWidget->showStyleWindow();
}

void PDFAnnotationStyleManager::closeStyleWindow()
{
    if (m_styleWidget)
    {
        m_styleWidget->closeStyleWindow();
        m_styleWidget = nullptr;
    }
}

void PDFAnnotationStyleManager::onStyleChanged(const PDFAnnotationStyle& style)
{
    if (m_style != style)
    {
        m_style = style;
        PDFAnnotationStyleSettings::setStyle(m_styleId, m_style);
        Q_EMIT styleChanged(m_style);
    }
}

QColor PDFAnnotationStyleWidget::selectColor(QColor initialColor, const QString& title)
{
    QColorDialog dialog(initialColor.isValid() ? initialColor : QColor(Qt::red), this);
    dialog.setWindowTitle(title);
    dialog.setOption(QColorDialog::ShowAlphaChannel, false);
    dialog.setOption(QColorDialog::DontUseNativeDialog, true);

    if (dialog.exec() == QDialog::Accepted)
    {
        return dialog.selectedColor();
    }

    return QColor();
}

}   // namespace pdf
