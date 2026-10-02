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

#include "editorplugin.h"
#include "pdffirepermissions.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfpagecontenteditorwidget.h"
#include "pdfpagecontenteditorstylesettings.h"
#include "pdfdocumentbuilder.h"
#include "pdfcertificatemanagerdialog.h"
#include "pdfdocumentwriter.h"
#include "pdfpagecontenteditorprocessor.h"
#include "pdfpagecontenteditorcontentstreambuilder.h"
#include "pdfstreamfilters.h"
#include "pdfoptimizer.h"
#include "pdffireeditedtext.h"
#include "pdffireinlinetexteditor.h"
#include <QApplication>
#include <QStatusBar>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QRegularExpression>

#include <QAction>
#include <QToolButton>
#include <QMainWindow>
#include <QMessageBox>
#include <QFileDialog>
#include <QSettings>
#include <QCoreApplication>

#include <cmath>
#include <limits>

namespace pdfplugin
{

EditorPlugin::EditorPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actions({ }),
    m_tools({ }),
    m_editorWidget(nullptr),
    m_scene(nullptr),
    m_sceneSelectionChangeEnabled(true),
    m_isSaving(false),
    m_isUndoRedoInProgress(false)
{
    m_scene.setIsPageContentDrawSuppressed(true);
}

void EditorPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    QAction* activateAction = new QAction(QIcon(":/pdfplugins/editorplugin/activate.svg"), tr("&Edit page content"), this);
    QAction* createTextAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-text.svg"), tr("Create &Text Label"), this);
    QAction* createFreehandCurveAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-freehand-curve.svg"), tr("Create &Freehand Curve"), this);
    QAction* createAcceptMarkAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-yes-mark.svg"), tr("Create &Accept Mark"), this);
    QAction* createRejectMarkAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-no-mark.svg"), tr("Create &Reject Mark"), this);
    QAction* createRectangleAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-rectangle.svg"), tr("Create R&ectangle"), this);
    QAction* createRoundedRectangleAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-rounded-rectangle.svg"), tr("&Create Rounded Rectangle"), this);
    QAction* createHorizontalLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-horizontal-line.svg"), tr("Create &Horizontal Line"), this);
    QAction* createVerticalLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-vertical-line.svg"), tr("Create &Vertical Line"), this);
    QAction* createLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-line.svg"), tr("Create L&ine"), this);
    QAction* createDotAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-dot.svg"), tr("Create &Dot"), this);
    QAction* createSvgImageAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-svg-image.svg"), tr("Create &SVG Image"), this);
    QAction* createMultipleElementsAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-multiple.svg"), tr("Create &Multiple Elements"), this);
    QAction* undoAction = new QAction(QIcon(":/resources/undo.svg"), tr("&Undo"), this);
    QAction* redoAction = new QAction(QIcon(":/resources/redo.svg"), tr("&Redo"), this);
    QAction* clearAction = new QAction(QIcon(":/pdfplugins/editorplugin/clear.svg"), tr("Clear A&ll Graphics"), this);

    activateAction->setObjectName("editortool_activateAction");
    createTextAction->setObjectName("editortool_createTextAction");
    createFreehandCurveAction->setObjectName("editortool_createFreehandCurveAction");
    createAcceptMarkAction->setObjectName("editortool_createAcceptMarkAction");
    createRejectMarkAction->setObjectName("editortool_createRejectMarkAction");
    createRectangleAction->setObjectName("editortool_createRectangleAction");
    createRoundedRectangleAction->setObjectName("editortool_createRoundedRectangleAction");
    createHorizontalLineAction->setObjectName("editortool_createHorizontalLineAction");
    createVerticalLineAction->setObjectName("editortool_createVerticalLineAction");
    createLineAction->setObjectName("editortool_createLineAction");
    createDotAction->setObjectName("editortool_createDotAction");
    createSvgImageAction->setObjectName("editortool_createSvgImageAction");
    createMultipleElementsAction->setObjectName("editortool_createMultipleElementsAction");
    undoAction->setObjectName("editortool_undoAction");
    redoAction->setObjectName("editortool_redoAction");
    clearAction->setObjectName("editortool_clearAction");

    activateAction->setCheckable(true);
    createTextAction->setCheckable(true);
    createFreehandCurveAction->setCheckable(true);
    createAcceptMarkAction->setCheckable(true);
    createRejectMarkAction->setCheckable(true);
    createRectangleAction->setCheckable(true);
    createRoundedRectangleAction->setCheckable(true);
    createHorizontalLineAction->setCheckable(true);
    createVerticalLineAction->setCheckable(true);
    createLineAction->setCheckable(true);
    createDotAction->setCheckable(true);
    createSvgImageAction->setCheckable(true);
    createMultipleElementsAction->setCheckable(true);
    createMultipleElementsAction->setToolTip(tr("Create Multiple Elements\n\n"
                                                "The creation tool stays active after an element has been created "
                                                "and the size of the last created element is reused, so a single click "
                                                "creates the next element of the same size. "
                                                "Hold Shift to define a different size."));

    m_actions[Activate] = activateAction;
    m_actions[Text] = createTextAction;
    m_actions[FreehandCurve] = createFreehandCurveAction;
    m_actions[AcceptMark] = createAcceptMarkAction;
    m_actions[RejectMark] = createRejectMarkAction;
    m_actions[Rectangle] = createRectangleAction;
    m_actions[RoundedRectangle] = createRoundedRectangleAction;
    m_actions[HorizontalLine] = createHorizontalLineAction;
    m_actions[VerticalLine] = createVerticalLineAction;
    m_actions[Line] = createLineAction;
    m_actions[Dot] = createDotAction;
    m_actions[SvgImage] = createSvgImageAction;
    m_actions[CreateMultipleElements] = createMultipleElementsAction;
    m_actions[Undo] = undoAction;
    m_actions[Redo] = redoAction;
    m_actions[Clear] = clearAction;

    undoAction->setShortcut(QKeySequence::Undo);
    redoAction->setShortcut(QKeySequence::Redo);

    // Jakub Melka: the tool actions are enabled only when the page content editing
    // is active, so single letter shortcuts do not collide with the rest of the
    // application. While a text label is being edited, the printable characters are
    // consumed by the text editor, so the shortcuts do not interfere with typing.
    activateAction->setShortcut(QKeySequence("Ctrl+Shift+E"));
    createTextAction->setShortcut(QKeySequence("T"));
    createFreehandCurveAction->setShortcut(QKeySequence("F"));
    createAcceptMarkAction->setShortcut(QKeySequence("A"));
    createRejectMarkAction->setShortcut(QKeySequence("X"));
    createRectangleAction->setShortcut(QKeySequence("R"));
    createRoundedRectangleAction->setShortcut(QKeySequence("Shift+R"));
    createHorizontalLineAction->setShortcut(QKeySequence("H"));
    createVerticalLineAction->setShortcut(QKeySequence("V"));
    createLineAction->setShortcut(QKeySequence("L"));
    createDotAction->setShortcut(QKeySequence("D"));
    createSvgImageAction->setShortcut(QKeySequence("I"));
    createMultipleElementsAction->setShortcut(QKeySequence("M"));

    QFile acceptMarkFile(":/pdfplugins/editorplugin/accept-mark.svg");
    QByteArray acceptMarkContent;
    if (acceptMarkFile.open(QFile::ReadOnly))
    {
        acceptMarkContent = acceptMarkFile.readAll();
        acceptMarkFile.close();
    }

    QFile rejectMarkFile(":/pdfplugins/editorplugin/reject-mark.svg");
    QByteArray rejectMarkContent;
    if (rejectMarkFile.open(QFile::ReadOnly))
    {
        rejectMarkContent = rejectMarkFile.readAll();
        rejectMarkFile.close();
    }

    m_tools[TextTool] = new pdf::PDFCreatePCElementTextTool(widget->getDrawWidgetProxy(), &m_scene, createTextAction, this);
    m_tools[FreehandCurveTool] = new pdf::PDFCreatePCElementFreehandCurveTool(widget->getDrawWidgetProxy(), &m_scene, createFreehandCurveAction, this);
    m_tools[AcceptMarkTool] = new pdf::PDFCreatePCElementImageTool(widget->getDrawWidgetProxy(), &m_scene, createAcceptMarkAction, acceptMarkContent, false, this);
    m_tools[RejectMarkTool] = new pdf::PDFCreatePCElementImageTool(widget->getDrawWidgetProxy(), &m_scene, createRejectMarkAction, rejectMarkContent, false, this);
    m_tools[RectangleTool] = new pdf::PDFCreatePCElementRectangleTool(widget->getDrawWidgetProxy(), &m_scene, createRectangleAction, false, this);
    m_tools[RoundedRectangleTool] = new pdf::PDFCreatePCElementRectangleTool(widget->getDrawWidgetProxy(), &m_scene, createRoundedRectangleAction, true, this);
    m_tools[HorizontalLineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createHorizontalLineAction, true, false, this);
    m_tools[VerticalLineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createVerticalLineAction, false, true, this);
    m_tools[LineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createLineAction, false, false, this);
    m_tools[DotTool] = new pdf::PDFCreatePCElementDotTool(widget->getDrawWidgetProxy(), &m_scene, createDotAction, this);
    m_tools[ImageTool] = new pdf::PDFCreatePCElementImageTool(widget->getDrawWidgetProxy(), &m_scene, createSvgImageAction, QByteArray(), true, this);

    pdf::PDFToolManager* toolManager = widget->getToolManager();
    for (pdf::PDFWidgetTool* tool : m_tools)
    {
        toolManager->addTool(tool);
        connect(tool, &pdf::PDFWidgetTool::toolActivityChanged, this, &EditorPlugin::onToolActivityChanged);
    }

    m_widget->addInputInterface(&m_scene);
    m_widget->getDrawWidgetProxy()->registerDrawInterface(&m_scene);
    m_scene.setWidget(m_widget);
    connect(&m_scene, &pdf::PDFPageContentScene::sceneChanged, this, &EditorPlugin::onSceneChanged);
    connect(&m_scene, &pdf::PDFPageContentScene::selectionChanged, this, &EditorPlugin::onSceneSelectionChanged);
    connect(&m_scene, &pdf::PDFPageContentScene::editElementRequest, this, &EditorPlugin::onSceneEditElement);
    // PDF Fire: this removes everything from the edited pages, including their original
    // content - applying the changes then writes empty pages. So it is confirmed first.
    connect(clearAction, &QAction::triggered, this, [this]()
    {
        const QString message = tr("All graphics will be removed from the edited pages, including the original content of the pages. "
                                   "If the changes are then applied, the pages become empty.\n\nRemove all graphics?");
        if (QMessageBox::warning(m_widget, tr("Clear All Graphics"), message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        {
            m_scene.clear();
        }
    });
    connect(undoAction, &QAction::triggered, this, &EditorPlugin::onUndoTriggered);
    connect(redoAction, &QAction::triggered, this, &EditorPlugin::onRedoTriggered);
    connect(activateAction, &QAction::triggered, this, &EditorPlugin::onSetActive);
    connect(createMultipleElementsAction, &QAction::triggered, this, &EditorPlugin::onCreateMultipleElementsTriggered);
    connect(createMultipleElementsAction, &QAction::triggered, this, &EditorPlugin::writeSettings);
    connect(m_widget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::drawSpaceChanged, this, &EditorPlugin::onDrawSpaceChanged);
    connect(m_widget, &pdf::PDFWidget::sceneActivityChanged, this, &EditorPlugin::onSceneActivityChanged);

    readSettings();
    updateActions();
}

void EditorPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        clearUndoRedo();
        setActive(false);
        updateActions();
    }
}

std::vector<QAction*> EditorPlugin::getActions() const
{
    // Jakub Melka: all the actions are offered, so the page content editing and its
    // tools can be found in the menu of the application and not only in the toolbox,
    // which is displayed when the editing is already active. A null action creates
    // a separator both in the menu and in the toolbar of the plugin.
    std::vector<QAction*> result;

    result.push_back(m_actions[Activate]);
    result.push_back(nullptr);

    for (auto actionId : { Text, FreehandCurve, AcceptMark, RejectMark,
                           Rectangle, RoundedRectangle, HorizontalLine,
                           VerticalLine, Line, Dot, SvgImage })
    {
        result.push_back(m_actions[actionId]);
    }

    result.push_back(nullptr);
    result.push_back(m_actions[CreateMultipleElements]);
    result.push_back(nullptr);
    result.push_back(m_actions[Undo]);
    result.push_back(m_actions[Redo]);
    result.push_back(m_actions[Clear]);

    return result;
}

QString EditorPlugin::getPluginMenuName() const
{
    return tr("Ed&itor");
}

pdf::PDFPlugin::PluginMenuLocation EditorPlugin::getPluginMenuLocation() const
{
    return PluginMenuLocation::Edit;
}

std::vector<QAction*> EditorPlugin::getToolbarActions() const
{
    // Jakub Melka: only the activation of the page content editing is placed on the
    // toolbar - the tools would not fit on it. They are offered in the menu of the
    // plugin and in the toolbox, which is displayed while the editing is active.
    std::vector<QAction*> result;

    result.push_back(m_actions[Activate]);

    return result;
}

bool EditorPlugin::updatePageContent(pdf::PDFInteger pageIndex,
                                     const std::vector<const pdf::PDFPageContentElement*>& elements,
                                     pdf::PDFDocumentBuilder* builder)
{
    pdf::PDFColorConvertor convertor;
    const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    const pdf::PDFEditedPageContent& editedPageContent = m_editedPageContent.at(pageIndex);

    QRectF mediaBox = page->getMediaBox();
    QRectF mediaBoxMM = page->getMediaBoxMM();

    pdf::PDFPageContentEditorContentStreamBuilder contentStreamBuilder(m_document);
    contentStreamBuilder.setFontDictionary(editedPageContent.getFontDictionary());
    contentStreamBuilder.setXObjectDictionary(editedPageContent.getXObjectDictionary());
    contentStreamBuilder.setGraphicStateDictionary(editedPageContent.getGraphicStateDictionary());
    contentStreamBuilder.setShadingDictionary(editedPageContent.getShadingDictionary());

    for (const pdf::PDFPageContentElement* element : elements)
    {
        const pdf::PDFPageContentElementEdited* editedElement = element->asElementEdited();
        const pdf::PDFPageContentElementRectangle* elementRectangle = element->asElementRectangle();
        const pdf::PDFPageContentElementLine* elementLine = element->asElementLine();
        const pdf::PDFPageContentElementDot* elementDot = element->asElementDot();
        const pdf::PDFPageContentElementFreehandCurve* elementFreehandCurve = element->asElementFreehandCurve();
        const pdf::PDFPageContentImageElement* elementImage = element->asElementImage();
        const pdf::PDFPageContentElementTextBox* elementTextBox = element->asElementTextBox();

        if (editedElement)
        {
            contentStreamBuilder.writeEditedElement(editedElement->getElement());
        }

        if (elementRectangle)
        {
            QRectF rect = elementRectangle->getRectangle();

            QPainterPath path;
            if (elementRectangle->isRounded())
            {
                qreal radius = qMin(rect.width(), rect.height()) * 0.25;
                path.addRoundedRect(rect, radius, radius, Qt::AbsoluteSize);
            }
            else
            {
                path.addRect(rect);
            }

            const bool stroke = elementRectangle->getPen().style() != Qt::NoPen;
            const bool fill = elementRectangle->getBrush().style() != Qt::NoBrush;
            contentStreamBuilder.writeStyledPath(path, elementRectangle->getPen(), elementRectangle->getBrush(), stroke, fill);
        }

        if (elementLine)
        {
            QLineF line = elementLine->getLine();
            QPainterPath path;
            path.moveTo(line.p1());
            path.lineTo(line.p2());

            contentStreamBuilder.writeStyledPath(path, elementLine->getPen(), elementLine->getBrush(), true, false);
        }

        if (elementDot)
        {
            QPen pen = elementDot->getPen();
            const qreal radius = pen.widthF() * 0.5;

            QPainterPath path;
            path.addEllipse(elementDot->getPoint(), radius, radius);

            contentStreamBuilder.writeStyledPath(path, Qt::NoPen, QBrush(pen.color()), false, true);
        }

        if (elementFreehandCurve)
        {
            QPainterPath path = elementFreehandCurve->getCurve();
            contentStreamBuilder.writeStyledPath(path, elementFreehandCurve->getPen(), elementFreehandCurve->getBrush(), true, false);
        }

        if (elementImage)
        {
            QImage image = elementImage->getImage();
            if (!image.isNull())
            {
                contentStreamBuilder.writeImage(image, elementImage->getRectangle());
            }
            else
            {
                // It is probably an SVG image
                pdf::PDFContentEditorPaintDevice paintDevice(&contentStreamBuilder, mediaBox, mediaBoxMM);
                QPainter painter(&paintDevice);

                QList<pdf::PDFRenderError> errors;
                pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
                elementImage->drawPage(&painter, &m_scene, pageIndex, nullptr, textLayoutGetter, QTransform(), convertor, errors);
            }
        }

        if (elementTextBox)
        {
            pdf::PDFContentEditorPaintDevice paintDevice(&contentStreamBuilder, mediaBox, mediaBoxMM);
            QPainter painter(&paintDevice);

            QList<pdf::PDFRenderError> errors;
            pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
            elementTextBox->drawPage(&painter, &m_scene, pageIndex, nullptr, textLayoutGetter, QTransform(), convertor, errors);
        }
    }

    QStringList errors = contentStreamBuilder.getErrors();
    contentStreamBuilder.clearErrors();

    if (!errors.empty())
    {
        const int errorCount = errors.size();
        if (errors.size() > 3)
        {
            errors.resize(3);
        }

        QString message = tr("Errors (%2) occured while creating content stream on page %3.<br>%1").arg(errors.join("<br>")).arg(errorCount).arg(pageIndex + 1);
        if (QMessageBox::question(m_dataExchangeInterface->getMainWindow(), tr("Error"), message, QMessageBox::Abort, QMessageBox::Ignore) == QMessageBox::Abort)
        {
            return false;
        }
    }

    pdf::PDFDictionaryBuilder fontDictionary = contentStreamBuilder.getFontDictionary();
    pdf::PDFDictionaryBuilder xobjectDictionary = contentStreamBuilder.getXObjectDictionary();
    pdf::PDFDictionaryBuilder graphicStateDictionary = contentStreamBuilder.getGraphicStateDictionary();
    pdf::PDFDictionaryBuilder shadingDictionary = contentStreamBuilder.getShadingDictionary();

    builder->replaceObjectsByReferences(fontDictionary);
    builder->replaceObjectsByReferences(xobjectDictionary);
    builder->replaceObjectsByReferences(graphicStateDictionary);
    builder->replaceObjectsByReferences(shadingDictionary);

    pdf::PDFArrayBuilder array;
    array.appendItem(pdf::PDFObject::createName("FlateDecode"));

    // Compress the content stream
    QByteArray compressedData = pdf::PDFFlateDecodeFilter::compress(contentStreamBuilder.getOutputContent());
    pdf::PDFDictionaryBuilder contentDictionary;
    contentDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(compressedData.size()));
    contentDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createArray(qMove(array)));
    pdf::PDFObject contentObject = pdf::PDFObject::createStream(pdf::PDFStream(qMove(contentDictionary), qMove(compressedData)));

    pdf::PDFObject pageObject = builder->getObjectByReference(page->getPageReference());

    // The new resource dictionary is created from the current one (which can also
    // be inherited from the page tree), so resources, which are not regenerated by
    // the content stream builder - for example color spaces, patterns, shadings or
    // properties - are preserved. Only the regenerated categories are replaced.
    // The current resources cannot be merged by the merge operation below, because
    // they are usually an indirect object, which is replaced as a whole.
    pdf::PDFDictionaryBuilder resourcesDictionary;
    if (const pdf::PDFDictionary* currentResourcesDictionary = m_document->getDictionaryFromObject(page->getResources()))
    {
        resourcesDictionary = pdf::PDFDictionaryBuilder(*currentResourcesDictionary);
    }

    auto setResources = [&resourcesDictionary](const char* key, const pdf::PDFDictionaryBuilder& dictionary)
    {
        if (!dictionary.isEmpty())
        {
            resourcesDictionary.setEntry(pdf::PDFInplaceOrMemoryString(key),
                                         pdf::PDFObject::createDictionary(dictionary));
        }
    };

    setResources("Font", fontDictionary);
    setResources("XObject", xobjectDictionary);
    setResources("ExtGState", graphicStateDictionary);
    setResources("Shading", shadingDictionary);

    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Resources");
    factory << resourcesDictionary;
    factory.endDictionaryItem();

    factory.beginDictionaryItem("Contents");
    factory << builder->addObject(std::move(contentObject));
    factory.endDictionaryItem();

    factory.endDictionary();

    pageObject = pdf::PDFObjectManipulator::merge(pageObject, factory.takeObject(), pdf::PDFObjectManipulator::RemoveNullObjects);
    builder->setObject(page->getPageReference(), std::move(pageObject));

    return true;
}

bool EditorPlugin::hasUnwrittenChanges() const
{
    // While the page content editing is active, the edited content is held
    // by the scene only - it is written into the document when the editing
    // is finished.
    return m_scene.isActive();
}

bool EditorPlugin::writeUnwrittenChanges()
{
    pdf::PDFTemporaryValueChange guard(&m_isSaving, true);
    return writePageContentToDocument();
}

bool EditorPlugin::save()
{
    pdf::PDFTemporaryValueChange guard(&m_isSaving, true);

    auto answer = QMessageBox::question(m_dataExchangeInterface->getMainWindow(), tr("Confirm Changes"), tr("The changes to the page content will be written to the document. Do you want to continue?"), QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Cancel);

    if (answer == QMessageBox::Cancel)
    {
        return false;
    }

    if (answer == QMessageBox::Yes)
    {
        return writePageContentToDocument();
    }

    return true;
}

bool EditorPlugin::writePageContentToDocument()
{
    pdf::PDFDocumentModifier modifier(m_document);
    pdf::PDFDocumentBuilder* builder = modifier.getBuilder();

    std::set<pdf::PDFInteger> pageIndices;
    for (const auto& item : m_editedPageContent)
    {
        pageIndices.insert(item.first);
    }

    std::map<pdf::PDFInteger, std::vector<const pdf::PDFPageContentElement*>> elementsByPage = m_scene.getElementsByPage();
    for (pdf::PDFInteger pageIndex : pageIndices)
    {
        if (m_editedPageContent.count(pageIndex) == 0)
        {
            continue;
        }

        std::vector<const pdf::PDFPageContentElement*> elements;
        auto it = elementsByPage.find(pageIndex);
        if (it != elementsByPage.cend())
        {
            elements = std::move(it->second);
        }

        if (!updatePageContent(pageIndex, elements, builder))
        {
            return false;
        }

        modifier.markReset();
    }

    pdf::PDFTemporaryValueChange restoreGuard(&m_isUndoRedoInProgress, true);
    clearUndoRedo();
    m_scene.clear();
    m_editedPageContent.clear();

    if (modifier.finalize())
    {
        pdf::PDFDocument document = *modifier.getDocument();
        pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::DereferenceSimpleObjects |
                                    pdf::PDFOptimizer::RemoveNullObjects |
                                    pdf::PDFOptimizer::RemoveUnusedObjects |
                                    pdf::PDFOptimizer::MergeIdenticalObjects |
                                    pdf::PDFOptimizer::ShrinkObjectStorage, nullptr);
        optimizer.setDocument(&document);
        optimizer.optimize();
        document = optimizer.takeOptimizedDocument();

        const pdf::PDFModifiedDocument::ModificationFlags flags = modifier.getFlags() | pdf::PDFModifiedDocument::PreserveUndoRedo;
        Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))), nullptr, flags));
    }

    return true;
}

void EditorPlugin::onSceneActivityChanged()
{
    updateActions();
}

void EditorPlugin::onSceneChanged(bool graphicsOnly)
{
    if (!graphicsOnly && !m_isUndoRedoInProgress && !m_isSaving)
    {
        createUndoStep();
    }

    if (!graphicsOnly)
    {
        updateActions();
    }

    if (m_editorWidget)
    {
        m_editorWidget->updateItemsInListWidget();
    }

    updateGraphics();
}

void EditorPlugin::onSceneSelectionChanged()
{
    if (m_editorWidget && m_sceneSelectionChangeEnabled)
    {
        m_editorWidget->setSelection(m_scene.getSelectedElementIds());
    }
}

void EditorPlugin::onWidgetSelectionChanged()
{
    Q_ASSERT(m_editorWidget);

    pdf::PDFTemporaryValueChange guard(&m_sceneSelectionChangeEnabled, false);
    m_scene.setSelectedElementIds(m_editorWidget->getSelection());
}

pdf::PDFWidgetTool* EditorPlugin::getActiveTool()
{
    for (pdf::PDFWidgetTool* currentTool : m_tools)
    {
        if (currentTool->isActive())
        {
            return currentTool;
        }
    }

    return nullptr;
}

void EditorPlugin::onToolActivityChanged()
{
    if (m_editorWidget)
    {
        pdf::PDFWidgetTool* activeTool = getActiveTool();

        const pdf::PDFPageContentElement* element = nullptr;
        pdf::PDFCreatePCElementTool* tool = qobject_cast<pdf::PDFCreatePCElementTool*>(activeTool);
        if (tool)
        {
            element = tool->getElement();
        }

        m_editorWidget->loadStyleFromElement(element);
    }
}

void EditorPlugin::onSceneEditElement(const std::set<pdf::PDFInteger>& elements)
{
    if (elements.empty())
    {
        return;
    }

    pdf::PDFPageContentElement* element = nullptr;
    for (pdf::PDFInteger id : elements)
    {
        element = m_scene.getElementById(id);
        if (element)
        {
            break;
        }
    }

    if (!element)
    {
        return;
    }

    // PDF Fire: a text is edited in place, with a caret in the text; Shift opens the
    // dialog (font size, transformation, the markup)
    if (!QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier) && startInlineTextEdit(element))
    {
        return;
    }

    std::unique_ptr<pdf::PDFPageContentElement> clonedElement(element->clone());
    if (pdf::PDFPageContentEditorStyleSettings::showEditElementStyleDialog(m_dataExchangeInterface->getMainWindow(), clonedElement.get()))
    {
        if (clonedElement->asElementEdited())
        {
            pdf::PDFPageContentElementEdited* editedElement = dynamic_cast<pdf::PDFPageContentElementEdited*>(clonedElement.get());
            if (editedElement->getElement()->asText())
            {
                if (!updateTextElement(editedElement))
                {
                    return;
                }
            }
        }

        m_scene.replaceElement(clonedElement.release());
        updateGraphics();
    }
}

QFont EditorPlugin::getInlineEditorFont(const pdf::PDFFontPointer& pdfFont)
{
    // The editor shows the text in a font of the system with the name of the font of
    // the text. The program of the font embedded in the document is not used: it is
    // usually a subset with a private character map, and Qt shows garbage with it.
    // (The editor only shows the text - it is written back in the original font.)
    QFont font(QStringLiteral("Sans Serif"));
    if (pdfFont)
    {
        const pdf::FontDescriptor* descriptor = pdfFont->getFontDescriptor();
        QString fontName = QString::fromLatin1(descriptor->fontName);
        if (fontName.size() > 7 && fontName[6] == QChar('+'))
        {
            fontName = fontName.mid(7);
        }

        const bool isBold = fontName.contains(QLatin1String("Bold"), Qt::CaseInsensitive) || descriptor->fontWeight >= 600;
        const bool isItalic = fontName.contains(QLatin1String("Italic"), Qt::CaseInsensitive) || fontName.contains(QLatin1String("Oblique"), Qt::CaseInsensitive) || descriptor->italicAngle != 0.0;
        QString familyName = fontName.section(QChar('-'), 0, 0).section(QChar(','), 0, 0);
        familyName.replace(QRegularExpression(QStringLiteral("(MT|PS|Std|PSMT)$")), QString());

        QStringList candidates = { familyName, QString::fromLatin1(descriptor->fontFamily) };
        // Common fonts of Windows documents and their metric-compatible free twins
        static const std::pair<const char*, const char*> twins[] = {
            { "Calibri", "Carlito" }, { "Cambria", "Caladea" }, { "Arial", "Liberation Sans" },
            { "TimesNewRoman", "Liberation Serif" }, { "Times New Roman", "Liberation Serif" },
            { "CourierNew", "Liberation Mono" }, { "Courier New", "Liberation Mono" }, { "Helvetica", "Liberation Sans" }
        };
        for (const auto& [windowsName, twin] : twins)
        {
            if (familyName.compare(QLatin1String(windowsName), Qt::CaseInsensitive) == 0)
            {
                candidates << QLatin1String(twin);
            }
        }

        bool isFontFound = false;
        for (const QString& candidate : candidates)
        {
            if (candidate.isEmpty())
            {
                continue;
            }
            QFont systemFont(candidate);
            if (QFontInfo(systemFont).family().compare(candidate, Qt::CaseInsensitive) == 0)
            {
                font = systemFont;
                isFontFound = true;
                break;
            }
        }

        if (!isFontFound)
        {
            const bool isSerif = descriptor->flags & 2;
            const bool isFixedPitch = descriptor->flags & 1;
            font = QFont(isFixedPitch ? QStringLiteral("Monospace") : (isSerif ? QStringLiteral("Serif") : QStringLiteral("Sans Serif")));
        }

        font.setBold(isBold);
        font.setItalic(isItalic);
    }

    return font;
}

bool EditorPlugin::startInlineTextEdit(pdf::PDFPageContentElement* element)
{
    // PDF Fire: a line of a paragraph is edited with the whole paragraph (reflow)
    if (startInlineParagraphEdit(element))
    {
        return true;
    }

    const pdf::PDFPageContentElementEdited* editedElement = element->asElementEdited();
    const pdf::PDFEditedPageContentElementText* textElement = editedElement ? editedElement->getElement()->asText() : nullptr;
    if (!textElement)
    {
        return false;
    }

    pdf::PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
    const pdf::PDFWidgetSnapshot snapshot = proxy->getSnapshot();
    const pdf::PDFWidgetSnapshot::SnapshotItem* pageSnapshot = snapshot.getPageSnapshot(element->getPageIndex());
    if (!pageSnapshot)
    {
        return false;
    }

    // Only a text written horizontally on the screen is edited in place
    const QTransform& pageToDevice = pageSnapshot->pageToDeviceMatrix;
    const QTransform textTransform = textElement->getState().getTextMatrix() * textElement->getTransform();
    const QTransform textToDevice = textTransform * pageToDevice;
    if (qAbs(textToDevice.m12()) > 0.01 * qAbs(textToDevice.m11()) || textToDevice.m11() <= 0.0 || textToDevice.m22() >= 0.0)
    {
        return false;
    }

    const pdf::PDFEditedTextMarkup markup(textElement->getItemsAsText());
    const QString plainText = markup.getPlainText();

    // The font of the text: the font program embedded in the document, if it can be
    // used, otherwise a similar font of the system
    pdf::PDFFontPointer pdfFont = textElement->getState().getTextFont();
    for (const pdf::PDFEditedPageContentElementText::FontResource& fontResource : textElement->getFontResources())
    {
        if (pdfFont)
        {
            break;
        }
        pdfFont = fontResource.font;
    }

    QFont font = getInlineEditorFont(pdfFont);

    const qreal fontSize = markup.getFirstFontSize() > 0.0 ? markup.getFirstFontSize() : textElement->getState().getTextFontSize();
    const qreal textScale = std::hypot(textTransform.m21(), textTransform.m22());
    const qreal deviceScale = std::hypot(pageToDevice.m21(), pageToDevice.m22());
    font.setPixelSize(qMax(4, qRound(fontSize * (textScale > 0.0 ? textScale : 1.0) * deviceScale)));

    if (m_inlineEditor)
    {
        m_inlineEditor->finish();
    }

    QWidget* drawWidget = m_widget->getDrawWidget()->getWidget();
    pdf::PDFFireInlineTextEditor* editor = new pdf::PDFFireInlineTextEditor(drawWidget);
    m_inlineEditor = editor;

    const pdf::PDFInteger elementId = element->getElementId();
    connect(editor, &pdf::PDFFireInlineTextEditor::committed, this, [this, editor, elementId, plainText](const QString& text)
    {
        editor->hide();
        editor->deleteLater();
        if (text != plainText)
        {
            applyInlineTextEdit(elementId, text);
        }
    });
    connect(editor, &pdf::PDFFireInlineTextEditor::cancelled, this, [editor]()
    {
        editor->hide();
        editor->deleteLater();
    });

    // Zooming or scrolling moves the text - the editing finishes then
    connect(proxy, &pdf::PDFDrawWidgetProxy::drawSpaceChanged, editor, &pdf::PDFFireInlineTextEditor::finish);

    const QRect deviceRect = pageToDevice.mapRect(element->getBoundingBox()).toAlignedRect();

    // The size from the page and the zoom is close, but the font of the system differs
    // a little - the size is corrected, so the height of the letters (the ink, as the
    // bounding box of the text on the page) is the same as on the page
    const qreal inkHeight = QFontMetricsF(font).tightBoundingRect(plainText).height();
    if (inkHeight > 0.0 && deviceRect.height() > 0)
    {
        const qreal factor = qBound(0.5, deviceRect.height() / inkHeight, 2.0);
        font.setPixelSize(qMax(4, qRound(font.pixelSize() * factor)));
    }

    // The text of a PDF is often set tighter or looser than the natural spacing of the
    // font (Word writes the kerning as small moves). The letter spacing of the editor is
    // adjusted, so the line is as wide as on the page and the text does not jump.
    const qreal naturalWidth = QFontMetricsF(font).horizontalAdvance(plainText);
    if (naturalWidth > 0.0 && plainText.size() > 1)
    {
        const qreal difference = deviceRect.width() - naturalWidth;
        if (qAbs(difference) < naturalWidth * 0.3)
        {
            font.setLetterSpacing(QFont::AbsoluteSpacing, difference / plainText.size());
        }
    }
    editor->start(plainText, font, deviceRect, drawWidget->mapFromGlobal(QCursor::pos()));

    if (QMainWindow* mainWindow = m_dataExchangeInterface->getMainWindow())
    {
        mainWindow->statusBar()->showMessage(tr("Type to change the text. Enter or a click outside keeps the change, Esc cancels. "
                                                "Shift + double-click opens the text properties (font size, position)."), 15000);
    }

    return true;
}

bool EditorPlugin::getInlineLine(const pdf::PDFPageContentElement* element, InlineLine* line) const
{
    const pdf::PDFPageContentElementEdited* editedElement = element ? element->asElementEdited() : nullptr;
    const pdf::PDFEditedPageContentElementText* textElement = editedElement ? editedElement->getElement()->asText() : nullptr;
    if (!textElement)
    {
        return false;
    }

    // Only horizontal text, upright in the page, of one line
    const QTransform textTransform = textElement->getState().getTextMatrix() * textElement->getTransform();
    if (qAbs(textTransform.m12()) > 0.01 * qAbs(textTransform.m11()) || qAbs(textTransform.m21()) > 0.01 * qAbs(textTransform.m22()) ||
        textTransform.m11() <= 0.0 || textTransform.m22() <= 0.0)
    {
        return false;
    }

    const pdf::PDFEditedTextMarkup markup(textElement->getItemsAsText());
    const QString text = markup.getPlainText();
    if (text.contains(QChar('\n')) || text.trimmed().isEmpty())
    {
        return false;
    }

    pdf::PDFFontPointer font = textElement->getState().getTextFont();
    for (const pdf::PDFEditedPageContentElementText::FontResource& fontResource : textElement->getFontResources())
    {
        if (font)
        {
            break;
        }
        font = fontResource.font;
    }

    line->id = element->getElementId();
    line->pageIndex = element->getPageIndex();
    line->rect = element->getBoundingBox().normalized();
    line->text = text;
    line->fontName = font ? font->getFontDescriptor()->fontName : QByteArray();
    line->font = font;
    return !line->rect.isEmpty();
}

bool EditorPlugin::findParagraph(pdf::PDFPageContentElement* element, Paragraph* paragraph) const
{
    InlineLine clicked;
    if (!getInlineLine(element, &clicked))
    {
        return false;
    }

    // The lines of the page, which can be a part of the paragraph
    std::vector<InlineLine> lines;
    const auto elementsByPage = m_scene.getElementsByPage();
    auto it = elementsByPage.find(clicked.pageIndex);
    if (it == elementsByPage.cend())
    {
        return false;
    }
    for (const pdf::PDFPageContentElement* pageElement : it->second)
    {
        InlineLine line;
        if (getInlineLine(pageElement, &line) && line.fontName == clicked.fontName &&
            qAbs(line.rect.height() - clicked.rect.height()) <= clicked.rect.height() * 0.3)
        {
            lines.push_back(line);
        }
    }

    const qreal height = clicked.rect.height();
    auto findNeighbour = [&](const InlineLine& from, bool below, qreal pitch) -> const InlineLine*
    {
        const InlineLine* best = nullptr;
        qreal bestDistance = std::numeric_limits<qreal>::infinity();
        for (const InlineLine& line : lines)
        {
            // The y axis of a page goes up - a line below has a smaller y
            const qreal distance = below ? from.rect.center().y() - line.rect.center().y() : line.rect.center().y() - from.rect.center().y();
            const bool isOverlapping = line.rect.left() < from.rect.right() && line.rect.right() > from.rect.left();
            if (line.id == from.id || !isOverlapping || distance < 0.8 * height || distance > 2.2 * height)
            {
                continue;
            }
            if (pitch > 0.0 && qAbs(distance - pitch) > 0.15 * pitch)
            {
                continue;
            }
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = &line;
            }
        }
        return best;
    };

    // The column: the lines above and below with the same left margin (the first line
    // of a paragraph can be indented), at the same distance
    std::vector<InlineLine> column = { clicked };
    qreal pitch = 0.0;
    qreal bodyLeft = clicked.rect.left();
    bool isBodyLeftKnown = false;

    while (const InlineLine* next = findNeighbour(column.back(), true, pitch))
    {
        const qreal left = next->rect.left();
        if (isBodyLeftKnown ? qAbs(left - bodyLeft) > 3.0 : (left > bodyLeft + 3.0 || bodyLeft - left > 40.0))
        {
            break;
        }
        if (!isBodyLeftKnown)
        {
            bodyLeft = left;
            isBodyLeftKnown = true;
        }
        if (pitch == 0.0)
        {
            pitch = column.back().rect.center().y() - next->rect.center().y();
        }
        column.push_back(*next);
    }

    while (const InlineLine* previous = findNeighbour(column.front(), false, pitch))
    {
        const qreal left = previous->rect.left();
        if (pitch == 0.0)
        {
            pitch = previous->rect.center().y() - column.front().rect.center().y();
        }
        if (qAbs(left - bodyLeft) <= 3.0 || !isBodyLeftKnown && qAbs(left - column.front().rect.left()) <= 3.0)
        {
            column.insert(column.begin(), *previous);
            continue;
        }
        if (left > bodyLeft + 3.0 && left <= bodyLeft + 40.0)
        {
            // An indented first line of a paragraph
            column.insert(column.begin(), *previous);
        }
        break;
    }

    if (column.size() < 2 || pitch <= 0.0)
    {
        // A line alone - edited as a line
        return false;
    }

    if (!isBodyLeftKnown)
    {
        bodyLeft = std::min_element(column.cbegin(), column.cend(), [](const auto& a, const auto& b) { return a.rect.left() < b.rect.left(); })->rect.left();
    }

    qreal maxRight = 0.0;
    for (const InlineLine& line : column)
    {
        maxRight = qMax(maxRight, line.rect.right());
    }

    // The paragraphs of the column: a paragraph ends with a short line, or before an
    // indented line
    const qreal width = maxRight - bodyLeft;
    const qreal shortLineLimit = maxRight - qMax(15.0, width * 0.12);
    size_t start = 0;
    for (size_t i = 0; i < column.size(); ++i)
    {
        const bool isLast = (i + 1 == column.size());
        const bool isEnd = isLast || column[i].rect.right() < shortLineLimit || column[i + 1].rect.left() > bodyLeft + 3.0;
        if (!isEnd)
        {
            continue;
        }

        const bool containsClicked = std::any_of(column.cbegin() + start, column.cbegin() + i + 1, [&clicked](const InlineLine& line) { return line.id == clicked.id; });
        if (containsClicked)
        {
            paragraph->lines.assign(column.cbegin() + start, column.cbegin() + i + 1);
            break;
        }
        start = i + 1;
    }

    paragraph->bodyLeft = bodyLeft;
    paragraph->maxRight = maxRight;
    paragraph->pitch = pitch;
    return !paragraph->lines.empty();
}

bool EditorPlugin::startInlineParagraphEdit(pdf::PDFPageContentElement* element)
{
    Paragraph paragraph;
    if (!findParagraph(element, &paragraph))
    {
        return false;
    }

    pdf::PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
    const pdf::PDFWidgetSnapshot snapshot = proxy->getSnapshot();
    const pdf::PDFWidgetSnapshot::SnapshotItem* pageSnapshot = snapshot.getPageSnapshot(element->getPageIndex());
    if (!pageSnapshot)
    {
        return false;
    }

    const QTransform& pageToDevice = pageSnapshot->pageToDeviceMatrix;
    if (qAbs(pageToDevice.m12()) > 0.001 || qAbs(pageToDevice.m21()) > 0.001)
    {
        // A rotated page - the lines are edited one by one
        return false;
    }

    // The text: the lines joined by spaces (a word broken by a hyphen stays together)
    QString text;
    for (size_t i = 0; i < paragraph.lines.size(); ++i)
    {
        const QString& lineText = paragraph.lines[i].text;
        if (i > 0)
        {
            const bool isHyphenated = text.endsWith(QChar('-')) && !lineText.isEmpty() && lineText.front().isLower();
            if (!isHyphenated)
            {
                text += QChar(' ');
            }
        }
        text += lineText.trimmed();
    }

    // The font and its size from the first line (as for one line), the letter spacing
    // from the full lines (the last line of a paragraph is usually short)
    const InlineLine& firstLine = paragraph.lines.front();
    QFont font = getInlineEditorFont(firstLine.font);
    const qreal deviceScale = std::hypot(pageToDevice.m11(), pageToDevice.m12());
    const QRectF firstDeviceRect = pageToDevice.mapRect(firstLine.rect);
    font.setPixelSize(qMax(4, qRound(firstDeviceRect.height() * 0.9)));
    const qreal inkHeight = QFontMetricsF(font).tightBoundingRect(firstLine.text).height();
    if (inkHeight > 0.0)
    {
        font.setPixelSize(qMax(4, qRound(font.pixelSize() * qBound(0.5, firstDeviceRect.height() / inkHeight, 2.0))));
    }

    qreal naturalWidth = 0.0;
    qreal pageWidth = 0.0;
    int characterCount = 0;
    for (size_t i = 0; i + 1 < paragraph.lines.size() || (paragraph.lines.size() == 1 && i == 0); ++i)
    {
        naturalWidth += QFontMetricsF(font).horizontalAdvance(paragraph.lines[i].text);
        pageWidth += paragraph.lines[i].rect.width() * deviceScale;
        characterCount += int(paragraph.lines[i].text.size());
    }
    if (naturalWidth > 0.0 && characterCount > 1 && qAbs(pageWidth - naturalWidth) < naturalWidth * 0.3)
    {
        font.setLetterSpacing(QFont::AbsoluteSpacing, (pageWidth - naturalWidth) / characterCount);
    }

    // The editor covers the paragraph; its lines are as wide as the column
    const QRectF paragraphRect(QPointF(paragraph.bodyLeft, paragraph.lines.back().rect.bottom()), QPointF(paragraph.maxRight, firstLine.rect.top()));
    QRect deviceRect = pageToDevice.mapRect(paragraphRect.normalized()).toAlignedRect();
    deviceRect.setTop(qRound(firstDeviceRect.top() - (paragraph.pitch * deviceScale - firstDeviceRect.height()) * 0.5));
    const qreal lineHeight = paragraph.pitch * deviceScale;
    const qreal firstLineIndent = (firstLine.rect.left() - paragraph.bodyLeft) * deviceScale;

    if (m_inlineEditor)
    {
        m_inlineEditor->finish();
    }
    if (m_inlineParagraphEditor)
    {
        m_inlineParagraphEditor->finish();
    }

    QWidget* drawWidget = m_widget->getDrawWidget()->getWidget();
    pdf::PDFFireInlineParagraphEditor* editor = new pdf::PDFFireInlineParagraphEditor(drawWidget);
    m_inlineParagraphEditor = editor;

    QStringList originalLines;
    for (const InlineLine& line : paragraph.lines)
    {
        originalLines << line.text.trimmed();
    }

    connect(editor, &pdf::PDFFireInlineParagraphEditor::committed, this, [this, editor, paragraph, originalLines](const QStringList& lines)
    {
        editor->hide();
        editor->deleteLater();
        if (lines != originalLines)
        {
            applyInlineParagraphEdit(paragraph, lines);
        }
    });
    connect(editor, &pdf::PDFFireInlineParagraphEditor::cancelled, this, [editor]()
    {
        editor->hide();
        editor->deleteLater();
    });
    connect(proxy, &pdf::PDFDrawWidgetProxy::drawSpaceChanged, editor, &pdf::PDFFireInlineParagraphEditor::finish);

    editor->start(text, font, deviceRect, lineHeight, firstLineIndent, drawWidget->mapFromGlobal(QCursor::pos()));

    if (QMainWindow* mainWindow = m_dataExchangeInterface->getMainWindow())
    {
        mainWindow->statusBar()->showMessage(tr("Editing the paragraph: the lines are rewrapped as you type. Enter starts a new line; a click outside "
                                                "or Ctrl+Enter keeps the change, Esc cancels. Shift + double-click edits one line in the dialog."), 20000);
    }
    return true;
}

void EditorPlugin::applyInlineParagraphEdit(const Paragraph& paragraph, const QStringList& newLines)
{
    // All the lines are changed as one step of undo
    std::vector<pdf::PDFPageContentElement*> replacedElements;
    std::vector<pdf::PDFPageContentElement*> addedElements;
    std::vector<pdf::PDFInteger> removedElements;
    bool isFailed = false;

    auto createChangedElement = [&](pdf::PDFInteger sourceId, const QString& text, QPointF offset) -> pdf::PDFPageContentElement*
    {
        const pdf::PDFPageContentElement* source = m_scene.getElementById(sourceId);
        if (!source)
        {
            return nullptr;
        }

        std::unique_ptr<pdf::PDFPageContentElement> element(source->clone());
        pdf::PDFPageContentElementEdited* editedElement = dynamic_cast<pdf::PDFPageContentElementEdited*>(element.get());
        pdf::PDFEditedPageContentElementText* textElement = editedElement ? editedElement->getElement()->asText() : nullptr;
        if (!textElement)
        {
            return nullptr;
        }

        if (!offset.isNull())
        {
            // A new line is a copy of a line of the paragraph, moved down
            textElement->setTransform(textElement->getTransform() * QTransform::fromTranslate(offset.x(), offset.y()));
        }

        textElement->setItemsAsText(pdf::PDFEditedTextMarkup(textElement->getItemsAsText()).getMarkupWithPlainText(text));
        if (!updateTextElement(editedElement))
        {
            return nullptr;
        }
        return element.release();
    };

    const size_t oldCount = paragraph.lines.size();
    const size_t newCount = size_t(newLines.size());
    const InlineLine& lastLine = paragraph.lines.back();
    for (size_t i = 0; i < qMax(oldCount, newCount); ++i)
    {
        if (i < oldCount && i < newCount)
        {
            if (newLines[i] == paragraph.lines[i].text.trimmed())
            {
                continue;
            }
            if (newLines[i].isEmpty())
            {
                removedElements.push_back(paragraph.lines[i].id);
                continue;
            }
            if (pdf::PDFPageContentElement* element = createChangedElement(paragraph.lines[i].id, newLines[i], QPointF()))
            {
                replacedElements.push_back(element);
            }
            else
            {
                isFailed = true;
            }
        }
        else if (i < oldCount)
        {
            removedElements.push_back(paragraph.lines[i].id);
        }
        else if (!newLines[i].isEmpty())
        {
            // A new line below the last line; it starts at the left margin of the paragraph
            const qreal dx = paragraph.bodyLeft - lastLine.rect.left();
            const qreal dy = -paragraph.pitch * qreal(i - (oldCount - 1));
            if (pdf::PDFPageContentElement* element = createChangedElement(lastLine.id, newLines[i], QPointF(dx, dy)))
            {
                addedElements.push_back(element);
            }
            else
            {
                isFailed = true;
            }
        }
    }

    {
        pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
        if (!replacedElements.empty())
        {
            m_scene.replaceElements(replacedElements);
        }
        if (!addedElements.empty())
        {
            m_scene.addElements(addedElements);
        }
        if (!removedElements.empty())
        {
            m_scene.removeElementsById(removedElements);
        }
    }
    createUndoStep();
    updateActions();
    updateGraphics();

    if (isFailed)
    {
        QMessageBox::warning(m_widget, tr("Edit Text"), tr("Some lines of the paragraph could not be changed."));
    }
}

void EditorPlugin::applyInlineTextEdit(pdf::PDFInteger elementId, const QString& text)
{
    pdf::PDFPageContentElement* element = m_scene.getElementById(elementId);
    if (!element)
    {
        return;
    }

    std::unique_ptr<pdf::PDFPageContentElement> clonedElement(element->clone());
    pdf::PDFPageContentElementEdited* editedElement = dynamic_cast<pdf::PDFPageContentElementEdited*>(clonedElement.get());
    pdf::PDFEditedPageContentElementText* textElement = editedElement ? editedElement->getElement()->asText() : nullptr;
    if (!textElement)
    {
        return;
    }

    // The same conversion as in the edit dialog: the untouched characters keep their
    // positions and kerning, the typed ones are written in the font of the text
    const QString newMarkup = pdf::PDFEditedTextMarkup(textElement->getItemsAsText()).getMarkupWithPlainText(text);
    if (qEnvironmentVariableIsSet("PDFFIRE_DEBUG_TEXT"))
    {
        // Development aid: the markup before and after the edit
        qInfo().noquote() << "PDFFIRE_DEBUG_TEXT before:" << textElement->getItemsAsText();
        qInfo().noquote() << "PDFFIRE_DEBUG_TEXT after: " << newMarkup;
    }
    textElement->setItemsAsText(newMarkup);
    if (!updateTextElement(editedElement))
    {
        QMessageBox::warning(m_widget, tr("Edit Text"), tr("The text could not be changed."));
        return;
    }

    m_scene.replaceElement(clonedElement.release());
    updateGraphics();
}

void EditorPlugin::onSceneEditSingleElement(pdf::PDFInteger elementId)
{
    onSceneEditElement({ elementId });
}

void EditorPlugin::onPenChanged(const QPen& pen)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setPen(pen);
    }
}

void EditorPlugin::onBrushChanged(const QBrush& brush)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setBrush(brush);
    }
}

void EditorPlugin::onFontChanged(const QFont& font)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setFont(font);
    }
}

void EditorPlugin::onAlignmentChanged(Qt::Alignment alignment)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setAlignment(alignment);
    }
}

void EditorPlugin::onTextAngleChanged(pdf::PDFReal angle)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setTextAngle(angle);
    }
}

void EditorPlugin::setActive(bool active)
{
    if (m_scene.isActive() != active)
    {
        // Abort active tool, if we are deactivating the plugin
        if (!active)
        {
            if (pdf::PDFWidgetTool* tool = m_widget->getToolManager()->getActiveTool())
            {
                auto it = std::find(m_tools.cbegin(), m_tools.cend(), tool);
                if (it != m_tools.cend())
                {
                    m_widget->getToolManager()->setActiveTool(nullptr);
                }
            }
        }

        m_scene.setActive(active);
        if (!active)
        {
            clearUndoRedo();
            m_scene.clear();
            m_editedPageContent.clear();
        }
        else
        {
            clearUndoRedo();
            updateDockWidget();
            pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
            updateEditedPages();
            initializeUndoRedo();
        }

        m_actions[Activate]->setChecked(active);
        updateActions();

        // If editor is not active, remove the widget
        if (m_editorWidget && !active)
        {
            delete m_editorWidget;
            m_editorWidget = nullptr;
        }
    }
}

void EditorPlugin::onSetActive(bool active)
{
    if (m_scene.isActive() && !active && !save())
    {
        updateActions();
        m_actions[Activate]->setChecked(true);
        return;
    }

    setActive(active);
}

void EditorPlugin::onCreateMultipleElementsTriggered(bool enabled)
{
    for (pdf::PDFWidgetTool* tool : m_tools)
    {
        if (pdf::PDFCreatePCElementTool* createTool = qobject_cast<pdf::PDFCreatePCElementTool*>(tool))
        {
            createTool->setMultipleElementCreationEnabled(enabled);
        }
    }
}

void EditorPlugin::readSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());

    settings.beginGroup("EditorPlugin");
    const bool createMultipleElements = settings.value("CreateMultipleElements", false).toBool();
    settings.endGroup();

    m_actions[CreateMultipleElements]->setChecked(createMultipleElements);
    onCreateMultipleElementsTriggered(createMultipleElements);
}

void EditorPlugin::writeSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());

    settings.beginGroup("EditorPlugin");
    settings.setValue("CreateMultipleElements", m_actions[CreateMultipleElements]->isChecked());
    settings.endGroup();
}

void EditorPlugin::updateActions()
{
    m_actions[Activate]->setEnabled(m_document);

    if (!m_scene.isActive() || !m_document)
    {
        // Inactive scene - disable all except activate action and the settings
        // of the creation tools, which can be changed at any time
        for (QAction* action : m_actions)
        {
            if (action == m_actions[Activate])
            {
                // PDF Fire: a protected or certified document can't be edited
                const bool canModify = pdf::PDFFirePermissions::canModifyContent(m_document);
                action->setEnabled(m_widget && !m_widget->isAnySceneActive(&m_scene) && canModify);
                action->setStatusTip(m_document && !canModify ? pdf::PDFFirePermissions::getRestrictionReason(m_document) : QString());
                continue;
            }

            if (action == m_actions[CreateMultipleElements])
            {
                continue;
            }

            action->setEnabled(false);
        }

        return;
    }

    const bool isSceneNonempty = !m_scene.isEmpty();
    const bool canUndo = !m_isSaving && !m_isUndoRedoInProgress && m_undoStates.size() > 1;
    const bool canRedo = !m_isSaving && !m_isUndoRedoInProgress && !m_redoStates.empty();

    // Tool actions
    for (auto actionId : { Text, FreehandCurve, AcceptMark, RejectMark,
                           Rectangle, RoundedRectangle, HorizontalLine,
                           VerticalLine, Line, Dot, SvgImage })
    {
        m_actions[actionId]->setEnabled(true);
    }

    m_actions[Undo]->setEnabled(canUndo);
    m_actions[Redo]->setEnabled(canRedo);

    // Clear action
    QAction* clearAction = m_actions[Clear];
    clearAction->setEnabled(isSceneNonempty);
}

void EditorPlugin::clearUndoRedo()
{
    m_undoStates.clear();
    m_redoStates.clear();
}

void EditorPlugin::initializeUndoRedo()
{
    clearUndoRedo();
    m_undoStates.emplace_back(m_scene.captureState());
}

void EditorPlugin::createUndoStep()
{
    m_undoStates.emplace_back(m_scene.captureState());
    m_redoStates.clear();

    while (m_undoStates.size() > MAX_UNDO_STEPS)
    {
        m_undoStates.erase(m_undoStates.begin());
    }
}

pdf::PDFPageContentScene::SceneState EditorPlugin::copySceneState(const pdf::PDFPageContentScene::SceneState& state) const
{
    pdf::PDFPageContentScene::SceneState copy;
    copy.selectedElementIds = state.selectedElementIds;
    copy.elements.reserve(state.elements.size());

    for (const auto& element : state.elements)
    {
        copy.elements.emplace_back(element->clone());
    }

    return copy;
}

void EditorPlugin::onUndoTriggered()
{
    if (m_undoStates.size() < 2)
    {
        return;
    }

    {
        pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
        m_redoStates.emplace_back(std::move(m_undoStates.back()));
        m_undoStates.pop_back();
        while (m_redoStates.size() > MAX_REDO_STEPS)
        {
            m_redoStates.erase(m_redoStates.begin());
        }

        m_scene.restoreState(copySceneState(m_undoStates.back()));
    }

    updateActions();
}

void EditorPlugin::onRedoTriggered()
{
    if (m_redoStates.empty())
    {
        return;
    }

    {
        pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
        pdf::PDFPageContentScene::SceneState state = std::move(m_redoStates.back());
        m_redoStates.pop_back();
        m_scene.restoreState(copySceneState(state));
        m_undoStates.emplace_back(std::move(state));
        while (m_undoStates.size() > MAX_UNDO_STEPS)
        {
            m_undoStates.erase(m_undoStates.begin());
        }
    }

    updateActions();
}

void EditorPlugin::updateGraphics()
{
    if (m_widget)
    {
        m_widget->getDrawWidget()->getWidget()->update();
    }
}

void EditorPlugin::updateDockWidget()
{
    if (m_editorWidget)
    {
        return;
    }

    m_editorWidget = new pdf::PDFPageContentEditorWidget(m_dataExchangeInterface->getMainWindow());
    m_editorWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_dataExchangeInterface->getMainWindow()->addDockWidget(Qt::RightDockWidgetArea, m_editorWidget, Qt::Vertical);
    m_editorWidget->setFloating(false);
    m_editorWidget->setWindowTitle(tr("Properties"));
    m_editorWidget->setScene(&m_scene);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::operationTriggered, &m_scene, &pdf::PDFPageContentScene::performOperation);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::itemSelectionChangedByUser, this, &EditorPlugin::onWidgetSelectionChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::editElementRequest, this, &EditorPlugin::onSceneEditSingleElement);

    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignTop))->setIcon(QIcon(":/resources/pce-align-top.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignCenterVertically))->setIcon(QIcon(":/resources/pce-align-v-center.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignBottom))->setIcon(QIcon(":/resources/pce-align-bottom.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignLeft))->setIcon(QIcon(":/resources/pce-align-left.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignCenterHorizontally))->setIcon(QIcon(":/resources/pce-align-h-center.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignRight))->setIcon(QIcon(":/resources/pce-align-right.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::SetSameHeight))->setIcon(QIcon(":/resources/pce-same-height.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::SetSameWidth))->setIcon(QIcon(":/resources/pce-same-width.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::SetSameSize))->setIcon(QIcon(":/resources/pce-same-size.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::CenterHorizontally))->setIcon(QIcon(":/resources/pce-center-h.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::CenterVertically))->setIcon(QIcon(":/resources/pce-center-v.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::CenterHorAndVert))->setIcon(QIcon(":/resources/pce-center-vh.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutVertically))->setIcon(QIcon(":/resources/pce-layout-v.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutHorizontally))->setIcon(QIcon(":/resources/pce-layout-h.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutForm))->setIcon(QIcon(":/resources/pce-layout-form.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutGrid))->setIcon(QIcon(":/resources/pce-layout-grid.svg"));

    // PDF Fire: the tools are in the ribbon (tab Edit), so they are not repeated here
    // as buttons - this panel keeps the properties of the tools and of the selection.

    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::penChanged, this, &EditorPlugin::onPenChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::brushChanged, this, &EditorPlugin::onBrushChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::fontChanged, this, &EditorPlugin::onFontChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::alignmentChanged, this, &EditorPlugin::onAlignmentChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::textAngleChanged, this, &EditorPlugin::onTextAngleChanged);
}

void EditorPlugin::updateEditedPages()
{
    if (!m_scene.isActive() || m_isSaving)
    {
        // Editor is not active or we are saving the document
        return;
    }

    std::vector<pdf::PDFInteger> currentPages = m_widget->getDrawWidget()->getCurrentPages();
    for (pdf::PDFInteger pageIndex : currentPages)
    {
        if (m_editedPageContent.count(pageIndex))
        {
            continue;
        }

        const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
        auto cms = m_widget->getDrawWidgetProxy()->getCMSManager()->getCurrentCMS();
        pdf::PDFPageContentEditorProcessor processor(page,
                                                     m_document,
                                                     m_widget->getDrawWidgetProxy()->getFontCache(),
                                                     cms.data(),
                                                     m_widget->getDrawWidgetProxy()->getOptionalContentActivity(),
                                                     QTransform(),
                                                     pdf::PDFMeshQualitySettings());

        QList<pdf::PDFRenderError> errors = processor.processContents();
        Q_UNUSED(errors);

        m_editedPageContent[pageIndex] = processor.takeEditedPageContent();

        size_t elementCount = m_editedPageContent[pageIndex].getElementCount();
        for (size_t i = 0; i < elementCount; ++i)
        {
            pdf::PDFEditedPageContentElement* element = m_editedPageContent[pageIndex].getElement(i);
            pdf::PDFPageContentElementEdited* editedElement = new pdf::PDFPageContentElementEdited(element);
            editedElement->setPageIndex(pageIndex);
            m_scene.addElement(editedElement);
        }
    }
}

bool EditorPlugin::updateTextElement(pdf::PDFPageContentElementEdited* element)
{
    pdf::PDFPageContentElementEdited* elementEdited = dynamic_cast<pdf::PDFPageContentElementEdited*>(element);
    if (!elementEdited)
    {
        return false;
    }

    pdf::PDFEditedPageContentElementText* targetTextElement = elementEdited->getElement()->asText();

    if (!targetTextElement)
    {
        return false;
    }

    pdf::PDFDocumentModifier modifier(m_document);
    pdf::PDFDocumentBuilder* builder = modifier.getBuilder();

    if (!updatePageContent(element->getPageIndex(), { element }, builder))
    {
        return false;
    }

    if (modifier.finalize())
    {
        pdf::PDFDocument* document = modifier.getDocument().get();

        const pdf::PDFPage* page = document->getCatalog()->getPage(element->getPageIndex());
        auto cms = m_widget->getDrawWidgetProxy()->getCMSManager()->getCurrentCMS();
        pdf::PDFFontCache fontCache(64, 64);
        pdf::PDFOptionalContentActivity activity(document, pdf::OCUsage::View, nullptr);
        fontCache.setDocument(pdf::PDFModifiedDocument(document, &activity));
        pdf::PDFPageContentEditorProcessor processor(page, document, &fontCache, cms.data(), &activity, QTransform(), pdf::PDFMeshQualitySettings());

        QList<pdf::PDFRenderError> errors = processor.processContents();
        Q_UNUSED(errors);

        pdf::PDFEditedPageContent content = processor.takeEditedPageContent();
        if (content.getElementCount() == 1)
        {
            pdf::PDFEditedPageContentElement* sourceElement = content.getElement(0);
            pdf::PDFEditedPageContentElementText* sourceElementText = sourceElement->asText();
            if (!sourceElementText)
            {
                return false;
            }
            targetTextElement->setState(sourceElementText->getState());
            targetTextElement->setTextPath(sourceElementText->getTextPath());
            targetTextElement->setItems(sourceElementText->getItems());
            targetTextElement->setTransform(sourceElementText->getTransform());
            targetTextElement->setClipPath(sourceElementText->getClipPath());

            // The font keys of the processed page can differ from the keys of the target
            // element (fonts are written under the keys of the page resources), so the
            // font resources and the text items as text must correspond to each other.
            // The processed document is temporary - font objects created only in it
            // (for example a fallback font) are not valid in the edited document.
            targetTextElement->setFontResources(pdf::PDFEditedPageContentElementText::getFontResourcesValidInDocument(sourceElementText->getFontResources(), document, m_document));
            targetTextElement->setItemsAsText(sourceElementText->getItemsAsText());
        }
        else
        {
            return false;
        }
    }

    return true;
}

void EditorPlugin::onDrawSpaceChanged()
{
    const size_t elementCount = m_scene.getElementIds().size();
    pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
    updateEditedPages();

    if (m_scene.getElementIds().size() != elementCount)
    {
        initializeUndoRedo();
        updateActions();
    }
}

}
