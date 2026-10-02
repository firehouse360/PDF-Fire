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

#include "signatureplugin.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfpagecontenteditorwidget.h"
#include "pdfpagecontenteditorstylesettings.h"
#include "pdfdocumentbuilder.h"
#include "pdfcertificatemanagerdialog.h"
#include "signdialog.h"
#include "departmentauthoritydialog.h"
#include "pdfdocumentsigner.h"
#include "pdffireformfields.h"
#include "pdfannotationmanipulator.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgettool.h"
#include "pdfform.h"
#include "pdffiresigningrecord.h"
#include "pdffirecertificateauthority.h"
#include "pdffirepermissions.h"
#include <QButtonGroup>
#include <QSettings>
#include <cmath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QEventLoop>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPainter>
#include <QRadioButton>
#include <QStatusBar>
#include <QVBoxLayout>

#include <QTimer>
#include <QBuffer>
#include <QAction>
#include <QApplication>
#include <QToolButton>
#include <QMainWindow>
#include <QMessageBox>
#include <QFileDialog>

namespace pdfplugin
{

namespace
{

/// PDF Fire: places a signature on a page - a half transparent preview of the signature,
/// of a preset size, follows the mouse; a click places it (Ctrl + wheel changes the size,
/// Esc cancels). The placed rectangle is returned through the callback.
class SignaturePlacementTool : public pdf::PDFWidgetTool
{
public:
    using Callback = std::function<void(pdf::PDFInteger pageIndex, QRectF rect)>;

    SignaturePlacementTool(pdf::PDFDrawWidgetProxy* proxy, QObject* parent) :
        pdf::PDFWidgetTool(proxy, parent)
    {
        setCursor(QCursor(Qt::CrossCursor));
    }

    /// Prepares the placing of the next signature (before the tool is activated)
    void setup(QImage preview, QSizeF size, Callback placed, std::function<void()> cancelled)
    {
        m_preview = std::move(preview);
        m_size = size;
        m_placed = std::move(placed);
        m_cancelled = std::move(cancelled);
        m_pageIndex = -1;
    }

    virtual void drawPage(QPainter* painter, pdf::PDFInteger pageIndex, const pdf::PDFPrecompiledPage* compiledPage, pdf::PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix, const pdf::PDFColorConvertor& convertor, QList<pdf::PDFRenderError>& errors) const override
    {
        Q_UNUSED(compiledPage);
        Q_UNUSED(layoutGetter);
        Q_UNUSED(convertor);
        Q_UNUSED(errors);

        if (pageIndex != m_pageIndex)
        {
            return;
        }

        // The preview is drawn upright on the screen (also on a rotated page)
        const QPointF center = pagePointToDevicePointMatrix.map(m_pagePoint);
        const qreal scale = std::hypot(pagePointToDevicePointMatrix.m11(), pagePointToDevicePointMatrix.m12());
        const QSizeF deviceSize = m_size * scale;
        const QRectF deviceRect(center - QPointF(deviceSize.width() * 0.5, deviceSize.height() * 0.5), deviceSize);

        painter->save();
        painter->setOpacity(0.55);
        painter->drawImage(deviceRect, m_preview);
        painter->setOpacity(1.0);
        painter->setPen(QPen(QColor(232, 89, 12), 1.0, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(deviceRect);
        painter->restore();
    }

    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override
    {
        Q_UNUSED(widget);
        QPointF pagePoint;
        m_pageIndex = getProxy()->getPageUnderPoint(event->position().toPoint(), &pagePoint);
        m_pagePoint = pagePoint;
        m_pageToDevice = QTransform();
        Q_EMIT getProxy()->repaintNeeded();
        event->accept();
    }

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override
    {
        Q_UNUSED(widget);
        event->accept();
        if (event->button() != Qt::LeftButton)
        {
            return;
        }

        QPointF pagePoint;
        const pdf::PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->position().toPoint(), &pagePoint);
        if (pageIndex < 0)
        {
            return;
        }

        // The rectangle in the page space: on a page rotated by 90 or 270 degrees the
        // width on the screen is the height of the rectangle in the page
        const pdf::PDFPage* page = getDocument() ? getDocument()->getCatalog()->getPage(pageIndex) : nullptr;
        const bool isTurned = page && (page->getPageRotation() == pdf::PageRotation::Rotate90 || page->getPageRotation() == pdf::PageRotation::Rotate270);
        const QSizeF size = isTurned ? m_size.transposed() : m_size;
        const QRectF rect(pagePoint - QPointF(size.width() * 0.5, size.height() * 0.5), size);

        Callback placed = m_placed;
        m_placed = nullptr;
        setActive(false);
        if (placed)
        {
            placed(pageIndex, rect);
        }
    }

    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) override
    {
        Q_UNUSED(widget);
        if (event->modifiers().testFlag(Qt::ControlModifier))
        {
            // Ctrl + wheel: the size of the signature
            resize(event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1);
            event->accept();
        }
    }

    /// The keys + and - change the size of the signature (they zoom the page otherwise)
    static int getSizeKeyDirection(const QKeyEvent* event)
    {
        switch (event->key())
        {
            case Qt::Key_Plus:
            case Qt::Key_Equal:
                return 1;
            case Qt::Key_Minus:
            case Qt::Key_Underscore:
                return -1;
            default:
                return 0;
        }
    }

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override
    {
        Q_UNUSED(widget);
        if (getSizeKeyDirection(event) != 0)
        {
            event->accept();
        }
    }

    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override
    {
        Q_UNUSED(widget);
        const int direction = getSizeKeyDirection(event);
        if (direction != 0)
        {
            resize(direction > 0 ? 1.1 : 1.0 / 1.1);
            event->accept();
        }
    }

    QSizeF getSize() const { return m_size; }

    void resize(qreal factor)
    {
        // The size is limited - from initials to a signature across the page
        const QSizeF newSize = m_size * factor;
        if (newSize.width() >= 40.0 && newSize.width() <= 600.0)
        {
            m_size = newSize;
        }
        Q_EMIT getProxy()->repaintNeeded();
    }

protected:
    virtual void setActiveImpl(bool active) override
    {
        pdf::PDFWidgetTool::setActiveImpl(active);
        if (!active && m_placed)
        {
            // Esc, or another tool - the placing is cancelled
            m_placed = nullptr;
            if (m_cancelled)
            {
                m_cancelled();
            }
        }
    }

private:
    QImage m_preview;
    QSizeF m_size;
    Callback m_placed;
    std::function<void()> m_cancelled;
    pdf::PDFInteger m_pageIndex = -1;
    QPointF m_pagePoint;
    QTransform m_pageToDevice;
};

}   // namespace

SignaturePlugin::SignaturePlugin() :
    pdf::PDFPlugin(nullptr),
    m_actions({ }),
    m_tools({ }),
    m_editorWidget(nullptr),
    m_scene(nullptr),
    m_sceneSelectionChangeEnabled(true)
{

}

void SignaturePlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    QAction* activateAction = new QAction(QIcon(":/pdfplugins/signatureplugin/activate.svg"), tr("&Activate signature creator"), this);
    QAction* createTextAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-text.svg"), tr("Create &Text Label"), this);
    QAction* createFreehandCurveAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-freehand-curve.svg"), tr("Create &Freehand Curve"), this);
    QAction* createAcceptMarkAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-yes-mark.svg"), tr("Create &Accept Mark"), this);
    QAction* createRejectMarkAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-no-mark.svg"), tr("Create Reject &Mark"), this);
    QAction* createRectangleAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-rectangle.svg"), tr("Create &Rectangle"), this);
    QAction* createRoundedRectangleAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-rounded-rectangle.svg"), tr("Create R&ounded Rectangle"), this);
    QAction* createHorizontalLineAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-horizontal-line.svg"), tr("Create &Horizontal Line"), this);
    QAction* createVerticalLineAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-vertical-line.svg"), tr("Create &Vertical Line"), this);
    QAction* createLineAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-line.svg"), tr("Create &Line"), this);
    QAction* createDotAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-dot.svg"), tr("Create &Dot"), this);
    QAction* createSvgImageAction = new QAction(QIcon(":/pdfplugins/signatureplugin/create-svg-image.svg"), tr("Create SVG &Image"), this);
    QAction* clearAction = new QAction(QIcon(":/pdfplugins/signatureplugin/clear.svg"), tr("Clear All &Graphics"), this);
    QAction* signElectronicallyAction = new QAction(QIcon(":/pdfplugins/signatureplugin/sign-electronically.svg"), tr("Sign &Electronically"), this);
    QAction* signDigitallyAction = new QAction(QIcon(":/pdfplugins/signatureplugin/sign-digitally.svg"), tr("Sign Digitally &With Certificate"), this);
    QAction* certificatesAction = new QAction(QIcon(":/pdfplugins/signatureplugin/certificates.svg"), tr("Certificates &Manager"), this);

    activateAction->setObjectName("signaturetool_activateAction");
    createTextAction->setObjectName("signaturetool_createTextAction");
    createFreehandCurveAction->setObjectName("signaturetool_createFreehandCurveAction");
    createAcceptMarkAction->setObjectName("signaturetool_createAcceptMarkAction");
    createRejectMarkAction->setObjectName("signaturetool_createRejectMarkAction");
    createRectangleAction->setObjectName("signaturetool_createRectangleAction");
    createRoundedRectangleAction->setObjectName("signaturetool_createRoundedRectangleAction");
    createHorizontalLineAction->setObjectName("signaturetool_createHorizontalLineAction");
    createVerticalLineAction->setObjectName("signaturetool_createVerticalLineAction");
    createLineAction->setObjectName("signaturetool_createLineAction");
    createDotAction->setObjectName("signaturetool_createDotAction");
    createSvgImageAction->setObjectName("signaturetool_createSvgImageAction");
    clearAction->setObjectName("signaturetool_clearAction");
    signElectronicallyAction->setObjectName("signaturetool_signElectronicallyAction");
    signDigitallyAction->setObjectName("signaturetool_signDigitallyAction");
    certificatesAction->setObjectName("signaturetool_certificatesAction");

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
    m_actions[Clear] = clearAction;
    m_actions[SignElectronically] = signElectronicallyAction;
    m_actions[SignDigitally] = signDigitallyAction;
    m_actions[Certificates] = certificatesAction;

    // PDF Fire: the certificate authority of the department
    QAction* departmentAuthorityAction = new QAction(QIcon(":/pdfplugins/signatureplugin/department.svg"), tr("&Department Certificates"), this);
    departmentAuthorityAction->setObjectName("signaturetool_departmentAuthorityAction");
    m_actions[DepartmentAuthority] = departmentAuthorityAction;

    QAction* certifyAction = new QAction(QIcon(":/pdfplugins/signatureplugin/certify.svg"), tr("&Certify Document"), this);
    certifyAction->setObjectName("signaturetool_certifyAction");
    m_actions[Certify] = certifyAction;
    connect(certifyAction, &QAction::triggered, this, &SignaturePlugin::onCertifyDocument);
    connect(departmentAuthorityAction, &QAction::triggered, this, &SignaturePlugin::onOpenDepartmentAuthority);

    QFile acceptMarkFile(":/pdfplugins/signatureplugin/accept-mark.svg");
    QByteArray acceptMarkContent;
    if (acceptMarkFile.open(QFile::ReadOnly))
    {
        acceptMarkContent = acceptMarkFile.readAll();
        acceptMarkFile.close();
    }

    QFile rejectMarkFile(":/pdfplugins/signatureplugin/reject-mark.svg");
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
        connect(tool, &pdf::PDFWidgetTool::toolActivityChanged, this, &SignaturePlugin::onToolActivityChanged);
    }

    m_widget->addInputInterface(&m_scene);
    m_widget->getDrawWidgetProxy()->registerDrawInterface(&m_scene);
    m_scene.setWidget(m_widget);
    connect(&m_scene, &pdf::PDFPageContentScene::sceneChanged, this, &SignaturePlugin::onSceneChanged);
    connect(&m_scene, &pdf::PDFPageContentScene::selectionChanged, this, &SignaturePlugin::onSceneSelectionChanged);
    connect(&m_scene, &pdf::PDFPageContentScene::editElementRequest, this, &SignaturePlugin::onSceneEditElement);
    connect(clearAction, &QAction::triggered, &m_scene, &pdf::PDFPageContentScene::clear);
    connect(activateAction, &QAction::triggered, this, &SignaturePlugin::setActive);
    connect(signElectronicallyAction, &QAction::triggered, this, &SignaturePlugin::onSignElectronically);
    connect(signDigitallyAction, &QAction::triggered, this, &SignaturePlugin::onSignDigitally);
    connect(certificatesAction, &QAction::triggered, this, &SignaturePlugin::onOpenCertificatesManager);
    connect(m_widget, &pdf::PDFWidget::sceneActivityChanged, this, &SignaturePlugin::onSceneActivityChanged);
    connectFormManager();

    updateActions();
}

void SignaturePlugin::connectFormManager()
{
    // PDF Fire: a click on a signature line, which is not signed yet, signs into it.
    // The signing is started after the click is finished (queued) - a dialog opened
    // during the click would leave the mouse grabbed by the form.
    pdf::PDFWidgetFormManager* formManager = m_widget ? m_widget->getFormManager() : nullptr;
    if (formManager && formManager != m_connectedFormManager)
    {
        m_connectedFormManager = formManager;
        connect(formManager, &pdf::PDFWidgetFormManager::unsignedSignatureFieldClicked, this, &SignaturePlugin::onUnsignedSignatureFieldClicked, Qt::QueuedConnection);
    }
}

void SignaturePlugin::onUnsignedSignatureFieldClicked(pdf::PDFObjectReference widget)
{
    if (!m_document)
    {
        return;
    }

    SignTarget target;
    target.kind = SignTarget::Kind::Field;
    target.widget = widget;
    const pdf::PDFObjectReference page = pdf::PDFAnnotationManipulator::findAnnotationPage(&m_document->getStorage(), widget);
    target.pageIndex = page.isValid() ? pdf::PDFInteger(m_document->getCatalog()->getPageIndexFromPageReference(page)) : -1;
    if (target.pageIndex < 0)
    {
        return;
    }

    signDigitally(target);
}

void SignaturePlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);
    connectFormManager();

    if (document.hasReset())
    {
        setActive(false);
        updateActions();
    }
}

std::vector<QAction*> SignaturePlugin::getActions() const
{
    std::vector<QAction*> result;

    result.push_back(m_actions[Activate]);
    result.push_back(m_actions[SignElectronically]);
    result.push_back(m_actions[SignDigitally]);
    result.push_back(m_actions[Certificates]);
    result.push_back(m_actions[DepartmentAuthority]);
    result.push_back(m_actions[Certify]);

    return result;
}

QString SignaturePlugin::getPluginMenuName() const
{
    return tr("Si&gnature");
}

void SignaturePlugin::onSceneChanged(bool graphicsOnly)
{
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

void SignaturePlugin::onSceneSelectionChanged()
{
    if (m_editorWidget && m_sceneSelectionChangeEnabled)
    {
        m_editorWidget->setSelection(m_scene.getSelectedElementIds());
    }
}

void SignaturePlugin::onWidgetSelectionChanged()
{
    Q_ASSERT(m_editorWidget);

    pdf::PDFTemporaryValueChange guard(&m_sceneSelectionChangeEnabled, false);
    m_scene.setSelectedElementIds(m_editorWidget->getSelection());
}

pdf::PDFWidgetTool* SignaturePlugin::getActiveTool()
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

void SignaturePlugin::onToolActivityChanged()
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

void SignaturePlugin::onSceneEditElement(const std::set<pdf::PDFInteger>& elements)
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

    if (pdf::PDFPageContentEditorStyleSettings::showEditElementStyleDialog(m_dataExchangeInterface->getMainWindow(), element))
    {
        updateGraphics();
    }
}

void SignaturePlugin::onSignElectronically()
{
    Q_ASSERT(m_document);
    Q_ASSERT(!m_scene.isEmpty());

    pdf::PDFColorConvertor convertor;

    if (QMessageBox::question(m_dataExchangeInterface->getMainWindow(), tr("Confirm Signature"), tr("Document will be signed electronically. Do you want to continue?"), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
    {
        pdf::PDFDocumentModifier modifier(m_document);

        std::set<pdf::PDFInteger> pageIndices = m_scene.getPageIndices();
        for (pdf::PDFInteger pageIndex : pageIndices)
        {
            const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
            pdf::PDFPageContentStreamBuilder pageContentStreamBuilder(modifier.getBuilder(),
                                                                      pdf::PDFContentStreamBuilder::CoordinateSystem::PDF,
                                                                      pdf::PDFPageContentStreamBuilder::Mode::PlaceAfter);
            QPainter* painter = pageContentStreamBuilder.begin(page->getPageReference());
            QList<pdf::PDFRenderError> errors;
            pdf::PDFTextLayoutGetter nullGetter(nullptr, pageIndex);
            m_scene.drawElements(painter, pageIndex, nullGetter, QTransform(), nullptr, convertor, errors);
            pageContentStreamBuilder.end(painter);
            modifier.markPageContentsChanged();
        }
        m_scene.clear();

        if (modifier.finalize())
        {
            Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }
    }
}

void SignaturePlugin::onSignDigitally()
{
    if (!m_document)
    {
        return;
    }

    // A drawn signature is used, as before
    if (!m_scene.isEmpty())
    {
        signDigitally(SignTarget());
        return;
    }

    // PDF Fire: otherwise the user chooses, where the signature goes - into a signature
    // line of the document, into a box drawn on a page, or nowhere (invisible)
    const pdf::PDFCatalog* catalog = m_document->getCatalog();
    std::vector<SignTarget> fieldTargets;
    QStringList fieldTexts;
    const pdf::PDFForm form = pdf::PDFForm::parse(m_document, catalog->getFormObject());
    form.apply([&](const pdf::PDFFormField* field)
    {
        const pdf::PDFFormFieldSignature* signatureField = dynamic_cast<const pdf::PDFFormFieldSignature*>(field);
        if (!signatureField || !signatureField->getSignature().getContents().isEmpty())
        {
            return;
        }

        for (const pdf::PDFFormWidget& widget : signatureField->getWidgets())
        {
            const size_t pageIndex = catalog->getPageIndexFromPageReference(widget.getPage());
            if (pageIndex < catalog->getPageCount())
            {
                SignTarget target;
                target.kind = SignTarget::Kind::Field;
                target.widget = widget.getWidget();
                target.pageIndex = pdf::PDFInteger(pageIndex);
                fieldTargets.push_back(target);
                fieldTexts << tr("In the signature line \"%1\" (page %2)").arg(field->getName(pdf::PDFFormField::NameType::FullyQualified)).arg(pageIndex + 1);
            }
        }
    });

    QDialog dialog(m_dataExchangeInterface->getMainWindow());
    dialog.setWindowTitle(tr("Where to Sign"));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Where should the signature appear?"), &dialog));
    QButtonGroup* group = new QButtonGroup(&dialog);
    int id = 0;
    for (const QString& text : fieldTexts)
    {
        QRadioButton* button = new QRadioButton(text, &dialog);
        group->addButton(button, id++);
        layout->addWidget(button);
    }
    QRadioButton* boxButton = new QRadioButton(tr("Place it on the page - click where the signature goes"), &dialog);
    group->addButton(boxButton, id++);
    layout->addWidget(boxButton);
    QRadioButton* invisibleButton = new QRadioButton(tr("Invisible - nothing is shown on the page (the signature is listed in the Signatures panel)"), &dialog);
    group->addButton(invisibleButton, id++);
    layout->addWidget(invisibleButton);
    group->button(0)->setChecked(true);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const int choice = group->checkedId();
    if (choice >= 0 && choice < int(fieldTargets.size()))
    {
        signDigitally(fieldTargets[choice]);
    }
    else if (group->checkedButton() == boxButton)
    {
        // The signature is placed after its look is chosen (in the sign dialog)
        SignTarget target;
        target.kind = SignTarget::Kind::Rectangle;
        signDigitally(target);
    }
    else
    {
        SignTarget target;
        target.kind = SignTarget::Kind::Invisible;
        signDigitally(target);
    }
}

pdf::PDFObjectReference SignaturePlugin::createNameSignature(pdf::PDFDocumentBuilder& builder,
                                                             pdf::PDFObjectReference signatureDictionary,
                                                             const SignTarget& target,
                                                             const QString& fieldName,
                                                             const QString& name,
                                                             const QFont& font,
                                                             bool showDetails,
                                                             const QDateTime& dateTime,
                                                             const QString& reason)
{
    const pdf::PDFObjectStorage* storage = builder.getStorage();
    const pdf::PDFPage* page = m_document->getCatalog()->getPage(target.pageIndex);
    const QRectF rect = (target.kind == SignTarget::Kind::Field) ? pdf::PDFFireFormFields::getWidgetRect(storage, target.widget) : target.rect;

    // On a rotated page the signature is turned with the page, so it is upright, when shown
    int rotation = 0;
    switch (page->getPageRotation())
    {
        case pdf::PageRotation::Rotate90: rotation = 90; break;
        case pdf::PageRotation::Rotate180: rotation = 180; break;
        case pdf::PageRotation::Rotate270: rotation = 270; break;
        default: break;
    }
    const QSizeF size = (rotation == 90 || rotation == 270) ? rect.size().transposed() : rect.size();

    // The appearance: the name (in the handwriting font) and the details
    pdf::PDFContentStreamBuilder contentBuilder(size, pdf::PDFContentStreamBuilder::CoordinateSystem::Qt);
    QPainter* painter = contentBuilder.begin();
    SignDialog::drawNameAppearance(painter, QRectF(QPointF(0, 0), size), name, font, showDetails, dateTime, reason);
    pdf::PDFContentStreamBuilder::ContentStream contentStream = contentBuilder.end(painter);

    std::vector<pdf::PDFObject> copiedObjects = builder.copyFrom({ contentStream.resources, contentStream.contents }, contentStream.document.getStorage(), true);
    Q_ASSERT(copiedObjects.size() == 2);
    const pdf::PDFObjectReference resourcesReference = copiedObjects[0].getReference();
    const pdf::PDFObjectReference formReference = copiedObjects[1].getReference();

    pdf::PDFObjectFactory formFactory;
    formFactory.beginDictionary();
    formFactory.beginDictionaryItem("Type");
    formFactory << pdf::WrapName("XObject");
    formFactory.endDictionaryItem();
    formFactory.beginDictionaryItem("Subtype");
    formFactory << pdf::WrapName("Form");
    formFactory.endDictionaryItem();
    formFactory.beginDictionaryItem("BBox");
    formFactory << QRectF(QPointF(0, 0), size);
    formFactory.endDictionaryItem();
    formFactory.beginDictionaryItem("Resources");
    formFactory << resourcesReference;
    formFactory.endDictionaryItem();
    if (rotation)
    {
        std::vector<pdf::PDFReal> matrix;
        switch (rotation)
        {
            case 90: matrix = { 0, 1, -1, 0, size.height(), 0 }; break;
            case 180: matrix = { -1, 0, 0, -1, size.width(), size.height() }; break;
            default: matrix = { 0, -1, 1, 0, 0, size.width() }; break;
        }
        formFactory.beginDictionaryItem("Matrix");
        formFactory << matrix;
        formFactory.endDictionaryItem();
    }
    formFactory.endDictionary();
    builder.mergeTo(formReference, formFactory.takeObject());

    if (target.kind == SignTarget::Kind::Rectangle)
    {
        return builder.createSignatureField(fieldName, signatureDictionary, page->getPageReference(), formReference, rect);
    }

    // An existing signature line: its widget gets the appearance, its field the signature
    const pdf::PDFDictionary* widgetDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(target.widget));
    const pdf::PDFObjectReference field = (widgetDictionary && !widgetDictionary->hasKey("T") && widgetDictionary->get("Parent").isReference()) ? widgetDictionary->get("Parent").getReference() : target.widget;

    pdf::PDFDictionaryBuilder widgetBuilder = widgetDictionary ? pdf::PDFDictionaryBuilder(*widgetDictionary) : pdf::PDFDictionaryBuilder();
    pdf::PDFDictionaryBuilder appearance;
    appearance.setEntry(pdf::PDFInplaceOrMemoryString("N"), pdf::PDFObject::createReference(formReference));
    widgetBuilder.setEntry(pdf::PDFInplaceOrMemoryString("AP"), pdf::PDFObject::createDictionary(std::move(appearance)));
    widgetBuilder.setEntry(pdf::PDFInplaceOrMemoryString("F"), pdf::PDFObject::createInteger(4));
    builder.setObject(target.widget, pdf::PDFObject::createDictionary(std::move(widgetBuilder)));

    const pdf::PDFDictionary* fieldDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(field));
    pdf::PDFDictionaryBuilder fieldBuilder = fieldDictionary ? pdf::PDFDictionaryBuilder(*fieldDictionary) : pdf::PDFDictionaryBuilder();
    fieldBuilder.setEntry(pdf::PDFInplaceOrMemoryString("V"), pdf::PDFObject::createReference(signatureDictionary));
    builder.setObject(field, pdf::PDFObject::createDictionary(std::move(fieldBuilder)));

    // The form says, that it has signatures, and that the document is changed by appending only
    const pdf::PDFDictionary* catalogDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(builder.getCatalogReference()));
    const pdf::PDFObject acroFormObject = catalogDictionary ? catalogDictionary->get("AcroForm") : pdf::PDFObject();
    if (const pdf::PDFDictionary* acroForm = storage->getDictionaryFromObject(acroFormObject))
    {
        pdf::PDFDictionaryBuilder formBuilder(*acroForm);
        const pdf::PDFObject& flags = storage->getObject(acroForm->get("SigFlags"));
        formBuilder.setEntry(pdf::PDFInplaceOrMemoryString("SigFlags"), pdf::PDFObject::createInteger((flags.isInt() ? flags.getInteger() : 0) | 3));
        if (acroFormObject.isReference())
        {
            builder.setObject(acroFormObject.getReference(), pdf::PDFObject::createDictionary(std::move(formBuilder)));
        }
        else
        {
            builder.setCatalogAcroForm(builder.addObject(pdf::PDFObject::createDictionary(std::move(formBuilder))));
        }
    }

    return field;
}

void SignaturePlugin::signDigitally(const SignTarget& initialTarget)
{
    if (!pdf::PDFFirePermissions::canSign(m_document))
    {
        QMessageBox::information(m_widget, tr("Sign"), pdf::PDFFirePermissions::getRestrictionReason(m_document));
        return;
    }

    const SignTarget& target = initialTarget;
    // Jakub Melka: do we have certificates? If not,
    // open certificate dialog, so the user can create
    // a new one. A document timestamp is signed by the timestamp authority
    // and not by the user, so we continue even when no certificate exists -
    // the dialog reports the missing certificate for the other signature types.
    if (pdf::PDFCertificateManager::getCertificates(pdf::PDFCertificateUsageFilter::DigitalSignature).empty())
    {
        onOpenCertificatesManager();
    }

    const bool isNameTarget = target.kind == SignTarget::Kind::Field || target.kind == SignTarget::Kind::Rectangle;
    SignDialog dialog(m_dataExchangeInterface->getMainWindow(), target.kind != SignTarget::Kind::Scene || m_scene.isEmpty());
    dialog.setNameAppearanceUsed(isNameTarget);
    dialog.setCertification(!pdf::PDFDocumentSigner::hasExistingSignatures(m_document), m_certificationRequest);
    m_certificationRequest = 0;
    if (dialog.exec() == SignDialog::Accepted)
    {
        const SignDialog::SignatureType signatureType = dialog.getSignatureType();

        // PDF Fire: a signature placed on the page - a preview of the signature follows
        // the mouse, a click places it (the signing continues after the click)
        SignTarget target = initialTarget;
        if (target.kind == SignTarget::Kind::Rectangle && target.rect.isEmpty() && signatureType != SignDialog::TimestampOnly)
        {
            // The size used last time (the preview keeps the proportions of the default size)
            const QSizeF defaultSize(dialog.isAppearanceDetailsShown() ? 200.0 : 180.0, dialog.isAppearanceDetailsShown() ? 60.0 : 50.0);
            const qreal sizeFactor = qBound(0.2, QSettings().value(QStringLiteral("SignaturePlugin/PlacementScale"), 1.0).toDouble(), 3.0);
            const QSizeF size = defaultSize * sizeFactor;
            QImage preview(QSize(int(size.width() * 3), int(size.height() * 3)), QImage::Format_ARGB32_Premultiplied);
            preview.fill(Qt::transparent);
            {
                QPainter painter(&preview);
                SignDialog::drawNameAppearance(&painter, QRectF(QPointF(0, 0), QSizeF(preview.size())), dialog.getAppearanceName(), dialog.getAppearanceFont(),
                                               dialog.isAppearanceDetailsShown(), QDateTime::currentDateTime(), dialog.getReasonText());
            }

            QEventLoop loop;
            bool isPlaced = false;
            if (!m_placementTool)
            {
                m_placementTool = new SignaturePlacementTool(m_widget->getDrawWidgetProxy(), this);
                m_widget->getToolManager()->addTool(m_placementTool);
            }
            SignaturePlacementTool* tool = static_cast<SignaturePlacementTool*>(m_placementTool);
            tool->setup(preview, size,
                        [&](pdf::PDFInteger pageIndex, QRectF rect)
                        {
                            target.pageIndex = pageIndex;
                            target.rect = rect;
                            isPlaced = true;
                            loop.quit();
                        },
                        [&loop]() { loop.quit(); });
            m_widget->getToolManager()->setActiveTool(tool);

            // The keys + and - go to the page (the focus can be on a button of the ribbon)
            m_widget->getDrawWidget()->getWidget()->setFocus(Qt::OtherFocusReason);
            if (QMainWindow* mainWindow = m_dataExchangeInterface->getMainWindow())
            {
                mainWindow->statusBar()->showMessage(tr("Click where the signature goes. The keys + and - (or Ctrl + mouse wheel) make it larger or smaller, Esc cancels."), 60000);
            }
            loop.exec();
            if (QMainWindow* mainWindow = m_dataExchangeInterface->getMainWindow())
            {
                mainWindow->statusBar()->clearMessage();
            }
            if (tool->isActive())
            {
                m_widget->getToolManager()->setActiveTool(nullptr);
            }
            // The callbacks refer to this function - they are cleared
            tool->setup(QImage(), size, nullptr, nullptr);

            if (isPlaced)
            {
                // The longer side is the width of the signature (also on a rotated page)
                QSettings().setValue(QStringLiteral("SignaturePlugin/PlacementScale"), qMax(target.rect.width(), target.rect.height()) / defaultSize.width());
            }
            else
            {
                return;
            }
        }

        // A document timestamp only attests, that the document existed at the time
        // of the timestamp - it has no signer and no visible appearance.
        const bool isDocumentTimestamp = signatureType == SignDialog::TimestampOnly;

        bool visibleSignature = !isDocumentTimestamp && dialog.getSignMethod() == SignDialog::SignDigitally && target.kind != SignTarget::Kind::Invisible;
        const bool isNameSignature = visibleSignature && isNameTarget;
        const std::set<pdf::PDFInteger> pageIndices = m_scene.getPageIndices();
        if (visibleSignature && !isNameSignature && pageIndices.empty())
        {
            if (QMessageBox::warning(m_widget, tr("Confirm Signature"),
                                    tr("No signature graphics were created, the signature will have no visible appearance. Continue?"),
                                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            {
                return;
            }
            visibleSignature = false;
        }
        else if (visibleSignature && !isNameSignature && pageIndices.size() > 1)
        {
            if (QMessageBox::warning(m_widget, tr("Confirm Signature"),
                                    tr("Signature graphics span multiple pages. Only the first page containing graphics will be used. Continue?"),
                                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            {
                return;
            }
        }

        const pdf::PDFCatalog* catalog = m_document->getCatalog();
        if (catalog->getPageCount() == 0)
        {
            QMessageBox::critical(m_widget, tr("Error"), tr("The document has no page for a signature widget."));
            return;
        }

        const pdf::PDFCertificateEntry* certificate = dialog.getCertificate();
        Q_ASSERT(certificate || isDocumentTimestamp);

        const QString signatureNamePrefix = isDocumentTimestamp ? QString("Timestamp_") : QString("Signature_");
        const QString signatureName = signatureNamePrefix + QString::number(QDateTime::currentMSecsSinceEpoch());
        const QString appearanceName = dialog.getAppearanceName();
        const QFont appearanceFont = dialog.getAppearanceFont();
        const bool isAppearanceDetailsShown = dialog.isAppearanceDetailsShown();
        const QString reasonText = isDocumentTimestamp ? QString() : dialog.getReasonText();
        const int certification = isDocumentTimestamp ? 0 : dialog.getCertification();
        const QString contactInfoText = isDocumentTimestamp ? QString() : dialog.getContactInfoText();

        pdf::PDFDocumentSigner::Parameters parameters;
        parameters.document = m_document;
        parameters.progress = m_widget->getDrawWidgetProxy()->getProgress();

        if (isDocumentTimestamp)
        {
            parameters.subfilter = "ETSI.RFC3161";
            parameters.signatureDictionaryType = "DocTimeStamp";
        }

        // The signed document is written as an incremental update of the file
        // the document was loaded from, so the signatures already present in it
        // stay valid.
        QFile originalFile(m_dataExchangeInterface->getOriginalFileName());
        if (originalFile.open(QFile::ReadOnly))
        {
            parameters.originalDocumentData = originalFile.readAll();
            originalFile.close();
        }

        // PDF Fire: the signing record (audit trail) - who, when, where, which
        // document, and the consent to sign electronically. It is written into
        // the signature dictionary, so the signature covers it.
        parameters.signingTime = QDateTime::currentDateTime();
        pdf::PDFFireSigningRecord signingRecord;
        if (!isDocumentTimestamp)
        {
            signingRecord.signerName = pdf::PDFCertificateManager::getCertificateOwnerName(*certificate, dialog.getPassword());
            signingRecord.signerEmail = certificate->info.getName(pdf::PDFCertificateInfo::Email);
            signingRecord.typedName = isNameSignature ? appearanceName : QString();
            signingRecord.reason = reasonText;
            signingRecord.contactInfo = contactInfoText;
            signingRecord.signatureType = (signatureType == SignDialog::SignatureWithTimestamp) ? QStringLiteral("Signature with timestamp (RFC 3161)")
                                                                                                : QStringLiteral("Signature");
            signingRecord.timestampAuthority = (signatureType == SignDialog::SignatureWithTimestamp) ? dialog.getTimestampUrl() : QString();
            signingRecord.signingTime = parameters.signingTime;
            signingRecord.documentName = QFileInfo(m_dataExchangeInterface->getOriginalFileName()).fileName();
            if (!parameters.originalDocumentData.isEmpty())
            {
                signingRecord.documentFingerprint = pdf::PDFFireSigningRecord::getFingerprint(parameters.originalDocumentData);
            }
            if (isNameSignature)
            {
                signingRecord.pageNumber = int(target.pageIndex) + 1;
            }
            else if (visibleSignature && !pageIndices.empty())
            {
                signingRecord.pageNumber = int(*pageIndices.begin()) + 1;
            }
            signingRecord.consentAccepted = dialog.isConsentAccepted();
            if (certification > 0)
            {
                static const char* const certificationTexts[] = { "", "certified - no changes allowed", "certified - form filling and signing allowed",
                                                                  "certified - form filling, signing and comments allowed" };
                signingRecord.certification = QString::fromLatin1(certificationTexts[certification]);
            }
            signingRecord.certificateIssuer = pdf::PDFFireCertificateAuthority::readIssuerName(*certificate, dialog.getPassword());
            if (signingRecord.certificateIssuer == signingRecord.signerName)
            {
                // A self-made certificate is issued by its owner
                signingRecord.certificateIssuer.clear();
            }
            if (dialog.isSigningPolicyApplied())
            {
                signingRecord.recordingSetBy = signingRecord.certificateIssuer;
                signingRecord.isRecordingLocked = dialog.isSigningPolicyLocked();
            }
            signingRecord.collectComputerInformation(dialog.isOperatingSystemRecorded(), dialog.isComputerRecorded(), dialog.isLocalAddressRecorded());
            if (dialog.isPublicAddressRecorded())
            {
                QApplication::setOverrideCursor(Qt::WaitCursor);
                signingRecord.lookupPublicAddress();
                QApplication::restoreOverrideCursor();
            }
        }

        pdf::PDFSignatureFactory::TimestampSettings timestampSettings;
        timestampSettings.url = dialog.getTimestampUrl();

        // Jakub Melka: the failures of the timestamping are described in detail,
        // because most of them are caused by the timestamp authority and not by
        // the signing itself.
        QString timestampErrorMessage;

        switch (signatureType)
        {
            case SignDialog::SignatureOnly:
                parameters.signFunction = [certificate, &dialog](const QByteArray& dataToBeSigned, QByteArray& signature)
                {
                    return pdf::PDFSignatureFactory::sign(*certificate, dialog.getPassword(), dataToBeSigned, signature);
                };
                break;

            case SignDialog::SignatureWithTimestamp:
                parameters.signFunction = [certificate, &dialog, timestampSettings, &timestampErrorMessage](const QByteArray& dataToBeSigned, QByteArray& signature)
                {
                    return pdf::PDFSignatureFactory::signWithTimestamp(*certificate, dialog.getPassword(), dataToBeSigned, timestampSettings, signature, timestampErrorMessage);
                };
                break;

            case SignDialog::TimestampOnly:
                parameters.signFunction = [timestampSettings, &timestampErrorMessage](const QByteArray& dataToBeSigned, QByteArray& signature)
                {
                    return pdf::PDFSignatureFactory::createTimestampToken(dataToBeSigned, timestampSettings, signature, timestampErrorMessage);
                };
                break;

            default:
                Q_ASSERT(false);
                return;
        }

        // Jakub Melka: the signature field is created by the signer, because the
        // document must be built again, when the space reserved for the signature
        // turns out to be too small.
        parameters.createSignatureFieldFunction = [&](pdf::PDFDocumentBuilder& builder, pdf::PDFObjectReference signatureDictionary)
        {
            pdf::PDFObjectReference signatureField;

            if (isNameSignature)
            {
                signatureField = createNameSignature(builder, signatureDictionary, target, signatureName, appearanceName, appearanceFont,
                                                     isAppearanceDetailsShown, parameters.signingTime, reasonText);
            }
            else if (!visibleSignature)
            {
                signatureField = builder.createSignatureField(signatureName, signatureDictionary, catalog->getPage(0)->getPageReference());
            }
            else
            {
                const pdf::PDFInteger pageIndex = *pageIndices.begin();
                const pdf::PDFPage* page = catalog->getPage(pageIndex);
                pdf::PDFColorConvertor convertor;

                pdf::PDFContentStreamBuilder contentBuilder(page->getMediaBox().size(), pdf::PDFContentStreamBuilder::CoordinateSystem::PDF);
                QPainter* painter = contentBuilder.begin();
                // Scene elements use unrotated PDF coordinates, including the MediaBox origin.
                // QPdfWriter's temporary page starts at zero; keep the Form BBox in that space.
                const QPointF mediaOrigin = page->getMediaBox().topLeft();
                painter->translate(-mediaOrigin);
                QList<pdf::PDFRenderError> errors;
                pdf::PDFTextLayoutGetter nullGetter(nullptr, pageIndex);
                m_scene.drawElements(painter, pageIndex, nullGetter, QTransform(), nullptr, convertor, errors);
                pdf::PDFContentStreamBuilder::ContentStream contentStream = contentBuilder.end(painter);

                QRectF boundingRect = m_scene.getBoundingBox(pageIndex, true);
                std::vector<pdf::PDFObject> copiedObjects = builder.copyFrom({ contentStream.resources, contentStream.contents }, contentStream.document.getStorage(), true);
                Q_ASSERT(copiedObjects.size() == 2);

                pdf::PDFObjectReference resourcesReference = copiedObjects[0].getReference();
                pdf::PDFObjectReference formReference = copiedObjects[1].getReference();

                // Create form object
                pdf::PDFObjectFactory formFactory;

                formFactory.beginDictionary();

                formFactory.beginDictionaryItem("Type");
                formFactory << pdf::WrapName("XObject");
                formFactory.endDictionaryItem();

                formFactory.beginDictionaryItem("Subtype");
                formFactory << pdf::WrapName("Form");
                formFactory.endDictionaryItem();

                formFactory.beginDictionaryItem("BBox");
                formFactory << boundingRect.translated(-mediaOrigin);
                formFactory.endDictionaryItem();

                formFactory.beginDictionaryItem("Resources");
                formFactory << resourcesReference;
                formFactory.endDictionaryItem();

                formFactory.endDictionary();

                builder.mergeTo(formReference, formFactory.takeObject());

                signatureField = builder.createSignatureField(signatureName, signatureDictionary, page->getPageReference(), formReference, boundingRect);
            }

            if (!reasonText.isEmpty())
            {
                builder.setSignatureReason(signatureDictionary, reasonText);
            }

            if (!contactInfoText.isEmpty())
            {
                builder.setSignatureContactInfo(signatureDictionary, contactInfoText);
            }

            if (!isDocumentTimestamp)
            {
                signingRecord.writeTo(builder, signatureDictionary);
            }

            // PDF Fire: certification - the signature reference (DocMDP) with the allowed
            // changes, and the catalog points to the certifying signature (PDF 2.0, 12.8.2.2)
            if (certification > 0)
            {
                pdf::PDFFirePermissions::writeCertification(builder, signatureDictionary, pdf::PDFFirePermissions::Certification(certification));
            }

            return signatureField;
        };

        // Creating the timestamp needs the timestamp authority to be contacted over
        // the network, so the signing can take a while.
        QByteArray signedDocument;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        pdf::PDFDocumentSigner::Result signingResult = pdf::PDFDocumentSigner::sign(parameters, signedDocument);
        QApplication::restoreOverrideCursor();

        // PDF Fire: the timestamp authority can't be reached (no internet) - the
        // signer can sign without the timestamp instead of not at all
        if (signingResult != pdf::PDFDocumentSigner::Result::OK && signatureType == SignDialog::SignatureWithTimestamp && !timestampErrorMessage.isEmpty())
        {
            if (QMessageBox::question(m_widget, tr("Timestamp Not Available"),
                                      tr("%1\n\nSign without a timestamp? The time of the signing is then the time of this computer, "
                                         "and the signature is not protected against a later revocation of the certificate.").arg(timestampErrorMessage),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            {
                return;
            }

            timestampErrorMessage.clear();
            signingRecord.signatureType = QStringLiteral("Signature (the timestamp authority could not be reached)");
            signingRecord.timestampAuthority.clear();
            parameters.signFunction = [certificate, &dialog](const QByteArray& dataToBeSigned, QByteArray& signature)
            {
                return pdf::PDFSignatureFactory::sign(*certificate, dialog.getPassword(), dataToBeSigned, signature);
            };
            QApplication::setOverrideCursor(Qt::WaitCursor);
            signingResult = pdf::PDFDocumentSigner::sign(parameters, signedDocument);
            QApplication::restoreOverrideCursor();
        }

        if (signingResult != pdf::PDFDocumentSigner::Result::OK)
        {
            const QString errorMessage = !timestampErrorMessage.isEmpty() ? timestampErrorMessage
                                                                         : pdf::PDFDocumentSigner::getResultMessage(signingResult);
            QMessageBox::critical(m_widget, tr("Error"), errorMessage);
            return;
        }

        QString fileName = QFileDialog::getSaveFileName(m_dataExchangeInterface->getMainWindow(), tr("Save Signed Document"), getSignedFileName(), tr("Portable Document (*.pdf);;All files (*.*)"));
        if (!fileName.isEmpty())
        {
            QFile signedFile(fileName);
            if (signedFile.open(QFile::WriteOnly | QFile::Truncate))
            {
                signedFile.write(signedDocument);
                signedFile.close();
                if (!isDocumentTimestamp)
                {
                    signingRecord.appendToLog(fileName, signedDocument);
                }
                // The signed document is opened, so the signature is seen at once
                if (QMessageBox::question(m_widget, tr("Signed"),
                                          tr("The document was signed and saved as:\n%1\n\nOpen the signed document now?").arg(fileName),
                                          QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
                {
                    const QString signedFileName = fileName;
                    QTimer::singleShot(0, this, [this, signedFileName]() { m_dataExchangeInterface->openDocument(signedFileName); });
                }
            }
            else
            {
                QMessageBox::critical(m_widget, tr("Error"), tr("Failed to save the signed document."));
            }
        }
    }
}

QString SignaturePlugin::getSignedFileName() const
{
    QFileInfo fileInfo(m_dataExchangeInterface->getOriginalFileName());

    return fileInfo.path() + "/" + fileInfo.baseName() + "_SIGNED.pdf";
}

void SignaturePlugin::onCertifyDocument()
{
    // PDF Fire: certification is a signature, which also states the allowed changes
    if (m_document && pdf::PDFDocumentSigner::hasExistingSignatures(m_document))
    {
        QMessageBox::information(m_widget, tr("Certify"), tr("The document is already signed. Only the first signature of a document can certify it."));
        return;
    }
    m_certificationRequest = 1;
    onSignDigitally();
    m_certificationRequest = 0;
}

void SignaturePlugin::onOpenDepartmentAuthority()
{
    DepartmentAuthorityDialog dialog(m_dataExchangeInterface->getMainWindow());
    dialog.exec();
}

void SignaturePlugin::onOpenCertificatesManager()
{
    pdf::PDFCertificateManagerDialog dialog(m_dataExchangeInterface->getMainWindow());
    dialog.exec();
}

void SignaturePlugin::onSceneActivityChanged()
{
    updateActions();
}

void SignaturePlugin::onPenChanged(const QPen& pen)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setPen(pen);
    }
}

void SignaturePlugin::onBrushChanged(const QBrush& brush)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setBrush(brush);
    }
}

void SignaturePlugin::onFontChanged(const QFont& font)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setFont(font);
    }
}

void SignaturePlugin::onAlignmentChanged(Qt::Alignment alignment)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setAlignment(alignment);
    }
}

void SignaturePlugin::onTextAngleChanged(pdf::PDFReal angle)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setTextAngle(angle);
    }
}

void SignaturePlugin::setActive(bool active)
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
            m_scene.clear();
        }
        else
        {
            updateDockWidget();
        }

        m_actions[Activate]->setChecked(active);
        updateActions();

        // PDF Fire: drawing a signature starts with the pen - the user expects to draw
        // right away, not to look for a drawing tool in the panel first
        if (active && !m_widget->getToolManager()->getActiveTool())
        {
            QTimer::singleShot(0, m_actions[FreehandCurve], [this]()
            {
                if (m_scene.isActive() && !m_actions[FreehandCurve]->isChecked())
                {
                    m_actions[FreehandCurve]->trigger();
                }
            });
        }

        // If editor is not active, remove the widget
        if (m_editorWidget && !active)
        {
            delete m_editorWidget;
            m_editorWidget = nullptr;
        }
    }
}

void SignaturePlugin::updateActions()
{
    m_actions[Activate]->setEnabled(m_document);

    if (!m_scene.isActive() || !m_document)
    {
        // Inactive scene - disable all except activate action and certificates
        for (QAction* action : m_actions)
        {
            if (action == m_actions[Activate])
            {
                action->setEnabled(m_widget && !m_widget->isAnySceneActive(&m_scene));
            }

            if (action == m_actions[Activate] ||
                action == m_actions[Certificates] ||
                action == m_actions[DepartmentAuthority])
            {
                continue;
            }

            action->setEnabled(false);
        }

        m_actions[SignDigitally]->setEnabled(m_document && m_widget && !m_widget->isAnySceneActive(&m_scene));
        m_actions[Certify]->setEnabled(m_document && m_widget && !m_widget->isAnySceneActive(&m_scene));
        applyPermissions();
        return;
    }

    const bool isSceneNonempty = !m_scene.isEmpty();

    // Tool actions
    for (auto actionId : { Text, FreehandCurve, AcceptMark, RejectMark,
                           Rectangle, RoundedRectangle, HorizontalLine,
                           VerticalLine, Line, Dot, SvgImage })
    {
        m_actions[actionId]->setEnabled(true);
    }

    // Clear action
    QAction* clearAction = m_actions[Clear];
    clearAction->setEnabled(isSceneNonempty);

    // Sign actions
    QAction* signElectronicallyAction = m_actions[SignElectronically];
    signElectronicallyAction->setEnabled(isSceneNonempty);
    QAction* signDigitallyAction = m_actions[SignDigitally];
    signDigitallyAction->setEnabled(m_document);
    m_actions[Certify]->setEnabled(m_document);
    applyPermissions();
}

void SignaturePlugin::applyPermissions()
{
    // PDF Fire: a protected or certified document - a drawn signature changes the
    // page content, a certificate signature fills in a signature field
    if (m_document && !pdf::PDFFirePermissions::canModifyContent(m_document))
    {
        const QString reason = pdf::PDFFirePermissions::getRestrictionReason(m_document);
        for (Action actionId : { Activate, SignElectronically })
        {
            m_actions[actionId]->setEnabled(false);
            m_actions[actionId]->setStatusTip(reason);
        }
    }
    if (m_document && !pdf::PDFFirePermissions::canSign(m_document))
    {
        m_actions[SignDigitally]->setEnabled(false);
        m_actions[SignDigitally]->setStatusTip(pdf::PDFFirePermissions::getRestrictionReason(m_document));
        m_actions[Certify]->setEnabled(false);
    }
}

void SignaturePlugin::updateGraphics()
{
    if (m_widget)
    {
        m_widget->getDrawWidget()->getWidget()->update();
    }
}

void SignaturePlugin::updateDockWidget()
{
    if (m_editorWidget)
    {
        return;
    }

    m_editorWidget = new pdf::PDFPageContentEditorWidget(m_dataExchangeInterface->getMainWindow());
    m_editorWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_dataExchangeInterface->getMainWindow()->addDockWidget(Qt::RightDockWidgetArea, m_editorWidget, Qt::Vertical);
    m_editorWidget->setFloating(false);
    m_editorWidget->setWindowTitle(tr("Signature Toolbox"));
    m_editorWidget->setScene(&m_scene);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::operationTriggered, &m_scene, &pdf::PDFPageContentScene::performOperation);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::itemSelectionChangedByUser, this, &SignaturePlugin::onWidgetSelectionChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::editElementRequest, this, &SignaturePlugin::onSceneEditSingleElement);

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

    for (QAction* action : m_actions)
    {
        m_editorWidget->addAction(action);
    }

    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::penChanged, this, &SignaturePlugin::onPenChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::brushChanged, this, &SignaturePlugin::onBrushChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::fontChanged, this, &SignaturePlugin::onFontChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::alignmentChanged, this, &SignaturePlugin::onAlignmentChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::textAngleChanged, this, &SignaturePlugin::onTextAngleChanged);
}

void SignaturePlugin::onSceneEditSingleElement(pdf::PDFInteger elementId)
{
    onSceneEditElement({ elementId });
}

}
