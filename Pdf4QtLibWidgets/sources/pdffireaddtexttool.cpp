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

#include "pdffireaddtexttool.h"
#include "pdffirepermissions.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfwidgetutils.h"
#include "pdfutils.h"

#include <QApplication>
#include <QColorDialog>
#include <QCoreApplication>
#include <QFontComboBox>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>

#include <cmath>

namespace pdf
{

static constexpr PDFReal ADD_TEXT_PADDING = 2.0;    ///< Space around the text in the annotation (points)

static QSettings createAddTextSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
}

/// The font, in which the annotation draws its text (PDFFreeTextAnnotation::draw):
/// the size in points is the pixel size, the page unit is a point
static QFont createPageFont(const PDFFreeTextStyle& style)
{
    QFont font(style.fontFamily);
    font.setPixelSize(qMax(1, qRound(style.fontSize)));
    return font;
}

PDFFireAddTextEditor::PDFFireAddTextEditor(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName("addTextEditor");
    setFrameShape(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTabChangesFocus(true);
    document()->setDocumentMargin(0);
    viewport()->setAutoFillBackground(false);
    connect(this, &QPlainTextEdit::textChanged, this, &PDFFireAddTextEditor::updateSize);
}

void PDFFireAddTextEditor::start(const QString& text, const QFont& font, const QColor& color, QPoint topLeft, bool coverPage)
{
    m_isFinished = false;
    m_coverPage = coverPage;
    m_color = color;

    setFont(font);
    updateStyleSheet();
    setPlainText(text);
    move(topLeft);
    updateSize();

    show();
    raise();
    setFocus(Qt::MouseFocusReason);

    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
}

void PDFFireAddTextEditor::setTextStyle(const QFont& font, const QColor& color)
{
    m_color = color;
    setFont(font);
    updateStyleSheet();
    updateSize();
}

void PDFFireAddTextEditor::updateStyleSheet()
{
    // The text is typed on the paper: no background (a changed text covers its old
    // look, so the paper colour is used then), the colour of the text, and a thin
    // dashed frame, so it is clear, where the typing goes.
    const QString background = m_coverPage ? QStringLiteral("white") : QStringLiteral("transparent");
    setStyleSheet(QString("QPlainTextEdit#addTextEditor { background: %1; color: %2; border: 1px dashed #E8590C; padding: 0px; "
                          "selection-background-color: #9DC3F2; selection-color: black; }").arg(background, m_color.name(QColor::HexRgb)));
}

void PDFFireAddTextEditor::finish()
{
    if (!m_isFinished)
    {
        m_isFinished = true;
        Q_EMIT committed(toPlainText());
    }
}

void PDFFireAddTextEditor::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
        case Qt::Key_Escape:
            // As in Acrobat, Esc ends the typing and keeps the text
            event->accept();
            finish();
            return;

        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (event->modifiers().testFlag(Qt::ControlModifier))
            {
                event->accept();
                finish();
                return;
            }
            break;

        default:
            break;
    }

    BaseClass::keyPressEvent(event);
}

void PDFFireAddTextEditor::focusOutEvent(QFocusEvent* event)
{
    BaseClass::focusOutEvent(event);

    if (event->reason() == Qt::PopupFocusReason)
    {
        return;
    }

    // The typing finishes, when the user clicks elsewhere - but not, when the font or
    // the size is being chosen in the style bar (the focus comes back after that)
    QTimer::singleShot(0, this, [this]()
    {
        QWidget* focusWidget = QApplication::focusWidget();
        if (focusWidget == this)
        {
            return;
        }
        if (m_focusCompanion && focusWidget && (focusWidget == m_focusCompanion || m_focusCompanion->isAncestorOf(focusWidget)))
        {
            return;
        }
        if (m_focusCompanion && QApplication::activePopupWidget())
        {
            return;
        }
        finish();
    });
}

void PDFFireAddTextEditor::updateSize()
{
    const QFontMetricsF metrics(font());
    qreal width = 0.0;
    const QStringList lines = toPlainText().split(QChar('\n'));
    for (const QString& line : lines)
    {
        width = qMax(width, metrics.horizontalAdvance(line));
    }

    // Room for the caret and the next letter, so the text does not scroll
    width += metrics.averageCharWidth() * 2.0 + 4.0;
    const qreal height = metrics.lineSpacing() * qMax<qsizetype>(1, lines.size()) + 4.0;
    resize(qCeil(width), qCeil(height));
}

PDFFireAddTextTool::PDFFireAddTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager)
{
    loadStyle();
    setCursor(QCursor(Qt::IBeamCursor));
    updateActions();
}

PDFFireAddTextTool::~PDFFireAddTextTool()
{
    if (m_editor)
    {
        // The text is not written - the document is going away
        m_editor->disconnect(this);
        delete m_editor;
    }
    delete m_styleBar;
}

void PDFFireAddTextTool::setTextStyle(const PDFFreeTextStyle& style)
{
    m_style = style;
    saveStyle();
    updateEditorStyle();
}

void PDFFireAddTextTool::loadStyle()
{
    QSettings settings = createAddTextSettings();
    settings.beginGroup("AddText");
    m_style.fontFamily = settings.value("fontFamily", QStringLiteral("Helvetica")).toString();
    m_style.fontSize = qBound(4.0, settings.value("fontSize", 11.0).toDouble(), 144.0);
    m_style.textColor = QColor(settings.value("textColor", QStringLiteral("#000000")).toString());
    if (!m_style.textColor.isValid())
    {
        m_style.textColor = Qt::black;
    }
    m_style.textAlignment = TextAlignment(Qt::AlignLeft | Qt::AlignTop);
    settings.endGroup();
}

void PDFFireAddTextTool::saveStyle()
{
    QSettings settings = createAddTextSettings();
    settings.beginGroup("AddText");
    settings.setValue("fontFamily", m_style.fontFamily);
    settings.setValue("fontSize", m_style.fontSize);
    settings.setValue("textColor", m_style.textColor.name(QColor::HexRgb));
    settings.endGroup();
}

void PDFFireAddTextTool::updateActions()
{
    if (QAction* action = getAction())
    {
        // The text is a comment (an annotation) - the permission of the comments
        const bool isEnabled = PDFFirePermissions::canAnnotate(getDocument());
        action->setChecked(isActive());
        action->setEnabled(isEnabled);
        action->setStatusTip(getDocument() && !isEnabled ? PDFFirePermissions::getRestrictionReason(getDocument()) : QString());
    }
}

void PDFFireAddTextTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (active)
    {
        showStyleBar();
        Q_EMIT messageDisplayRequest(tr("Click anywhere on the page and type. Enter starts a new line; Esc or a click outside finishes. "
                                        "Click a text you added to change it."), 20000);
    }
    else
    {
        finishEditing();
        hideStyleBar();
    }
}

qreal PDFFireAddTextTool::getDeviceScale(PDFInteger pageIndex) const
{
    const PDFWidgetSnapshot snapshot = getProxy()->getSnapshot();
    if (const PDFWidgetSnapshot::SnapshotItem* pageSnapshot = snapshot.getPageSnapshot(pageIndex))
    {
        const QTransform& matrix = pageSnapshot->pageToDeviceMatrix;
        return std::hypot(matrix.m21(), matrix.m22());
    }
    return 1.0;
}

QFont PDFFireAddTextTool::createDeviceFont(const PDFFreeTextStyle& style) const
{
    QFont font(style.fontFamily);
    font.setPixelSize(qMax(4, qRound(qRound(style.fontSize) * getDeviceScale(m_editPageIndex))));
    return font;
}

bool PDFFireAddTextTool::findTypewriterText(PDFInteger pageIndex, QPointF pagePoint, HitAnnotation* result) const
{
    const PDFDocument* document = getDocument();
    const PDFPage* page = document ? document->getCatalog()->getPage(pageIndex) : nullptr;
    if (!page)
    {
        return false;
    }

    // The topmost text first (the last one in the array is drawn last)
    const std::vector<PDFObjectReference>& annotations = page->getAnnotations();
    for (auto it = annotations.rbegin(); it != annotations.rend(); ++it)
    {
        PDFAnnotationPtr annotation = PDFAnnotation::parse(&document->getStorage(), *it);
        const PDFFreeTextAnnotation* freeText = dynamic_cast<const PDFFreeTextAnnotation*>(annotation.data());
        if (!freeText || freeText->getIntent() != PDFFreeTextAnnotation::Intent::TypeWriter)
        {
            continue;
        }

        if (freeText->getRectangle().normalized().adjusted(-2, -2, 2, 2).contains(pagePoint))
        {
            const PDFAnnotationDefaultAppearance appearance = PDFAnnotationDefaultAppearance::parse(freeText->getDefaultAppearance());
            result->annotation = *it;
            result->rectangle = freeText->getRectangle().normalized();
            result->contents = freeText->getContents();
            result->style.fontFamily = PDFDocumentBuilder::decodeFreeTextFontName(appearance.getFontName());
            result->style.fontSize = appearance.getFontSize() > 0.0 ? appearance.getFontSize() : m_style.fontSize;
            result->style.textColor = appearance.getFontColor().isValid() ? appearance.getFontColor() : QColor(Qt::black);
            result->style.textAlignment = TextAlignment(Qt::AlignLeft | Qt::AlignTop);
            return true;
        }
    }

    return false;
}

void PDFFireAddTextTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        BaseClass::mousePressEvent(widget, event);
        return;
    }

    event->accept();

    // A click outside of the text being typed finishes it (as in Acrobat, the next
    // click starts a new text)
    if (m_editor)
    {
        finishEditing();
        return;
    }

    QPointF pagePoint;
    const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
    if (pageIndex == -1)
    {
        return;
    }

    startEditing(widget, pageIndex, pagePoint, event->pos());
}

void PDFFireAddTextTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();
}

void PDFFireAddTextTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();
}

void PDFFireAddTextTool::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && !m_editor)
    {
        // Esc with no text being typed leaves the tool
        event->accept();
        setActive(false);
        return;
    }

    BaseClass::keyPressEvent(widget, event);
}

void PDFFireAddTextTool::startEditing(QWidget* widget, PDFInteger pageIndex, QPointF pagePoint, QPoint devicePoint)
{
    const PDFWidgetSnapshot snapshot = getProxy()->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* pageSnapshot = snapshot.getPageSnapshot(pageIndex);
    if (!pageSnapshot)
    {
        return;
    }

    // The text is typed upright on the screen - a page shown rotated is not supported
    const QTransform& pageToDevice = pageSnapshot->pageToDeviceMatrix;
    if (qAbs(pageToDevice.m12()) > 0.01 * qAbs(pageToDevice.m11()) || pageToDevice.m11() <= 0.0 || pageToDevice.m22() >= 0.0)
    {
        Q_EMIT messageDisplayRequest(tr("Text can be added to a page shown upright only - rotate the view back first."), 8000);
        return;
    }

    m_editPageIndex = pageIndex;
    m_editAnnotation = PDFObjectReference();

    PDFFreeTextStyle style = m_style;
    QString text;
    QPoint editorTopLeft;
    bool coverPage = false;

    HitAnnotation hit;
    if (findTypewriterText(pageIndex, pagePoint, &hit))
    {
        // Typing into a text added before
        m_editAnnotation = hit.annotation;
        style = hit.style;
        text = hit.contents;
        coverPage = true;
        m_editPageTopLeft = QPointF(hit.rectangle.left() + ADD_TEXT_PADDING, hit.rectangle.bottom() - ADD_TEXT_PADDING);

        // The style bar shows the style of this text, and a change applies to it
        m_style = style;
        if (m_styleBar)
        {
            QSignalBlocker blocker1(m_fontComboBox);
            QSignalBlocker blocker2(m_sizeSpinBox);
            m_fontComboBox->setCurrentFont(QFont(style.fontFamily));
            m_sizeSpinBox->setValue(qRound(style.fontSize));
            m_colorButton->setStyleSheet(QString("background-color: %1;").arg(style.textColor.name(QColor::HexRgb)));
        }
    }
    else
    {
        // A new text: the click is in the middle of the first line, the caret is there
        const QFont pageFont = createPageFont(style);
        const qreal lineHeight = QFontMetricsF(pageFont).lineSpacing();
        m_editPageTopLeft = QPointF(pagePoint.x(), pagePoint.y() + lineHeight * 0.5);
    }

    editorTopLeft = pageToDevice.map(m_editPageTopLeft).toPoint();

    m_editor = new PDFFireAddTextEditor(widget);
    m_editor->setFocusCompanion(m_styleBar);
    connect(m_editor, &PDFFireAddTextEditor::committed, this, &PDFFireAddTextTool::commit);

    // Zooming or scrolling moves the page - the typing finishes then
    connect(getProxy(), &PDFDrawWidgetProxy::drawSpaceChanged, m_editor, &PDFFireAddTextEditor::finish);

    // The editor has its frame and no margins - the text starts one pixel inside
    m_editor->start(text, createDeviceFont(style), style.textColor, editorTopLeft - QPoint(1, 1), coverPage);
    Q_UNUSED(devicePoint);
}

void PDFFireAddTextTool::finishEditing()
{
    if (m_editor)
    {
        m_editor->finish();
    }
}

void PDFFireAddTextTool::commit(const QString& enteredText)
{
    PDFFireAddTextEditor* editor = m_editor;
    m_editor = nullptr;
    if (editor)
    {
        editor->hide();
        editor->deleteLater();
    }

    const PDFDocument* document = getDocument();
    const PDFPage* page = document ? document->getCatalog()->getPage(m_editPageIndex) : nullptr;
    if (!page || !PDFFirePermissions::canAnnotate(document))
    {
        return;
    }

    // Trailing empty lines and spaces are not kept
    QString text = enteredText;
    while (!text.isEmpty() && text.back().isSpace())
    {
        text.chop(1);
    }

    const bool isNew = !m_editAnnotation.isValid();
    if (isNew && text.isEmpty())
    {
        return;
    }

    // The rectangle of the annotation in the page: the text starts at the top left
    // corner (y goes up on the page), the padding is around it
    const QFont pageFont = createPageFont(m_style);
    const QFontMetricsF metrics(pageFont);
    const QStringList lines = text.split(QChar('\n'));
    qreal width = 0.0;
    for (const QString& line : lines)
    {
        width = qMax(width, metrics.horizontalAdvance(line));
    }

    // A little more width than measured: the annotation wraps the words, if the
    // text does not fit, and the drawing uses the design metrics of the font
    width = std::ceil(width * 1.03 + metrics.averageCharWidth() * 0.5) + 2.0 * ADD_TEXT_PADDING;
    const qreal height = std::ceil(metrics.lineSpacing() * qMax<qsizetype>(1, lines.size()) + metrics.descent() * 0.5) + 2.0 * ADD_TEXT_PADDING;
    const QRectF rectangle(m_editPageTopLeft.x() - ADD_TEXT_PADDING, m_editPageTopLeft.y() + ADD_TEXT_PADDING - height, width, height);

    PDFDocumentModifier modifier(document);
    PDFDocumentBuilder* builder = modifier.getBuilder();
    const PDFObjectReference pageReference = page->getPageReference();

    if (!isNew && text.isEmpty())
    {
        // All the text was deleted - the text is removed
        builder->removeAnnotation(pageReference, m_editAnnotation);
    }
    else
    {
        PDFObjectReference annotation = m_editAnnotation;
        if (isNew)
        {
            annotation = builder->createAnnotationFreeText(pageReference, rectangle, PDFAuthorSettings::getAuthorName(), QString(), text, m_style, false);
        }
        else
        {
            builder->setAnnotationContents(annotation, text);
        }

        // A typewriter text: no border, no background (ISO 32000-2, 12.5.6.6)
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("IT");
        factory << WrapName("FreeTextTypeWriter");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Rect");
        factory << rectangle;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("RD");
        factory.beginArray();
        factory << ADD_TEXT_PADDING << ADD_TEXT_PADDING << ADD_TEXT_PADDING << ADD_TEXT_PADDING;
        factory.endArray();
        factory.endDictionaryItem();
        factory.beginDictionaryItem("DA");
        factory << WrapString(PDFDocumentBuilder::createFreeTextDefaultAppearance(m_style));
        factory.endDictionaryItem();
        factory.beginDictionaryItem("BS");
        factory.beginDictionary();
        factory.beginDictionaryItem("W");
        factory << 0;
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Border");
        factory.beginArray();
        factory << 0 << 0 << 0;
        factory.endArray();
        factory.endDictionaryItem();
        factory.beginDictionaryItem("M");
        factory << WrapCurrentDateTime();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder->mergeTo(annotation, factory.takeObject());
        builder->updateAnnotationAppearanceStreams(annotation);
    }

    modifier.markAnnotationsChanged();
    if (modifier.finalize())
    {
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }

    m_editAnnotation = PDFObjectReference();
}

void PDFFireAddTextTool::updateEditorStyle()
{
    if (m_editor)
    {
        m_editor->setTextStyle(createDeviceFont(m_style), m_style.textColor);
        m_editor->setFocus(Qt::OtherFocusReason);
    }
}

void PDFFireAddTextTool::showStyleBar()
{
    if (m_styleBar)
    {
        return;
    }

    QWidget* drawWidget = getProxy()->getWidget()->getDrawWidget()->getWidget();

    // A small bar over the top right corner of the pages: font, size, colour
    QFrame* bar = new QFrame(drawWidget);
    bar->setObjectName("addTextStyleBar");
    bar->setFrameShape(QFrame::StyledPanel);
    bar->setAutoFillBackground(true);
    QHBoxLayout* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(6);

    layout->addWidget(new QLabel(tr("Add Text:"), bar));

    m_fontComboBox = new QFontComboBox(bar);
    m_fontComboBox->setObjectName("addTextFontComboBox");
    m_fontComboBox->setCurrentFont(QFont(m_style.fontFamily));
    m_fontComboBox->setMaximumWidth(PDFWidgetUtils::scaleDPI_x(bar, 180));
    m_fontComboBox->setToolTip(tr("Font of the text"));
    layout->addWidget(m_fontComboBox);

    m_sizeSpinBox = new QSpinBox(bar);
    m_sizeSpinBox->setObjectName("addTextSizeSpinBox");
    m_sizeSpinBox->setRange(4, 144);
    m_sizeSpinBox->setValue(qRound(m_style.fontSize));
    m_sizeSpinBox->setSuffix(tr(" pt"));
    m_sizeSpinBox->setToolTip(tr("Size of the text"));
    layout->addWidget(m_sizeSpinBox);

    m_colorButton = new QPushButton(bar);
    m_colorButton->setObjectName("addTextColorButton");
    m_colorButton->setFixedSize(PDFWidgetUtils::scaleDPI_x(bar, 24), PDFWidgetUtils::scaleDPI_y(bar, 24));
    m_colorButton->setToolTip(tr("Color of the text"));
    m_colorButton->setStyleSheet(QString("background-color: %1;").arg(m_style.textColor.name(QColor::HexRgb)));
    layout->addWidget(m_colorButton);

    connect(m_fontComboBox, &QFontComboBox::currentFontChanged, this, [this](const QFont& font)
    {
        // "Nimbus Sans [UKWN]" - the foundry is not a part of the name in the PDF
        m_style.fontFamily = font.family().remove(QRegularExpression(QStringLiteral("\\s*\\[[^\\]]*\\]$")));
        saveStyle();
        updateEditorStyle();
    });
    connect(m_sizeSpinBox, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value)
    {
        m_style.fontSize = value;
        saveStyle();
        updateEditorStyle();
    });
    connect(m_colorButton, &QPushButton::clicked, this, [this, bar]()
    {
        const QColor color = QColorDialog::getColor(m_style.textColor, bar, tr("Text color"));
        if (color.isValid())
        {
            m_style.textColor = color;
            m_colorButton->setStyleSheet(QString("background-color: %1;").arg(color.name(QColor::HexRgb)));
            saveStyle();
            updateEditorStyle();
        }
    });

    bar->adjustSize();
    const int offset = PDFWidgetUtils::scaleDPI_x(bar, 12);
    bar->move(qMax(0, drawWidget->width() - bar->width() - offset), offset);
    bar->show();
    bar->raise();
    m_styleBar = bar;

    if (m_editor)
    {
        m_editor->setFocusCompanion(m_styleBar);
    }
}

void PDFFireAddTextTool::hideStyleBar()
{
    if (m_styleBar)
    {
        m_styleBar->hide();
        m_styleBar->deleteLater();
        m_styleBar = nullptr;
        m_fontComboBox = nullptr;
        m_sizeSpinBox = nullptr;
        m_colorButton = nullptr;
    }
}

}   // namespace pdf
