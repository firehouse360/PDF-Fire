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

#include "pagemarksdialogs.h"

#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"
#include "pdfrenderer.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace pdfplugin
{

namespace
{

constexpr int PREVIEW_WIDTH = 300;
constexpr int PREVIEW_HEIGHT = 400;

/// The page of the document with the mark, as it is shown (rotated), fitted into the size
QImage renderPage(const pdf::PDFDocument* document, pdf::PDFInteger pageIndex, QSize maxSize)
{
    const pdf::PDFPage* page = document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        return QImage();
    }

    const QSizeF pageSize = page->getRotatedCropBox().size();
    if (pageSize.isEmpty())
    {
        return QImage();
    }

    const QSize imageSize = pageSize.scaled(QSizeF(maxSize), Qt::KeepAspectRatio).toSize().expandedTo(QSize(1, 1));

    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(16, 16);
    pdf::PDFOptionalContentActivity activity(document, pdf::OCUsage::View, nullptr);
    fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), &activity));
    pdf::PDFRenderer renderer(document, &fontCache, &cms, &activity,
                              pdf::PDFRenderer::Features(pdf::PDFRenderer::Antialiasing | pdf::PDFRenderer::TextAntialiasing | pdf::PDFRenderer::SmoothImages),
                              pdf::PDFMeshQualitySettings());

    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(imageSize)), size_t(pageIndex));
    painter.end();
    return image;
}

QDoubleSpinBox* createPointSpin(double value, double maximum, QWidget* parent)
{
    QDoubleSpinBox* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(-maximum, maximum);
    spinBox->setDecimals(1);
    spinBox->setSuffix(QObject::tr(" pt", "points"));
    spinBox->setValue(value);
    return spinBox;
}

QLabel* createHint(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setEnabled(false);   // the disabled color of the theme - a quiet hint in light and dark theme
    return label;
}

}   // namespace

PageMarksColorButton::PageMarksColorButton(QColor color, QWidget* parent) :
    QPushButton(parent),
    m_color()
{
    setColor(color);
    connect(this, &QPushButton::clicked, this, [this]()
    {
        const QColor color = QColorDialog::getColor(m_color, this, tr("Color"));
        if (color.isValid())
        {
            setColor(color);
        }
    });
}

void PageMarksColorButton::setColor(QColor color)
{
    if (m_color == color)
    {
        return;
    }

    m_color = color;
    QPixmap swatch(28, 14);
    swatch.fill(color);
    QPainter painter(&swatch);
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
    painter.end();
    setIcon(QIcon(swatch));
    setIconSize(swatch.size());
    setText(color.name().toUpper());
    Q_EMIT colorChanged();
}

PageMarksDialogBase::PageMarksDialogBase(const pdf::PDFDocument* document, const QString& title, QWidget* parent) :
    QDialog(parent),
    m_document(document),
    m_settingsLayout(nullptr),
    m_previewLabel(nullptr),
    m_errorLabel(nullptr),
    m_okButton(nullptr),
    m_previewTimer(new QTimer(this))
{
    setWindowTitle(title);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QHBoxLayout* contentLayout = new QHBoxLayout();
    m_settingsLayout = new QVBoxLayout();
    contentLayout->addLayout(m_settingsLayout, 1);

    QGroupBox* previewBox = new QGroupBox(tr("Preview"), this);
    QVBoxLayout* previewLayout = new QVBoxLayout(previewBox);
    m_previewLabel = new QLabel(previewBox);
    m_previewLabel->setObjectName(QStringLiteral("pageMarksPreview"));
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumSize(PREVIEW_WIDTH, PREVIEW_HEIGHT);
    previewLayout->addWidget(m_previewLabel, 1);
    contentLayout->addWidget(previewBox);
    mainLayout->addLayout(contentLayout, 1);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("pageMarksError"));
    m_errorLabel->setWordWrap(true);
    mainLayout->addWidget(m_errorLabel);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PageMarksDialogBase::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &PageMarksDialogBase::reject);
    mainLayout->addWidget(buttonBox);

    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(200);
    connect(m_previewTimer, &QTimer::timeout, this, &PageMarksDialogBase::updatePreview);
}

void PageMarksDialogBase::schedulePreview()
{
    m_previewTimer->start();
}

void PageMarksDialogBase::watch(QWidget* widget)
{
    if (QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget))
    {
        connect(lineEdit, &QLineEdit::textChanged, this, &PageMarksDialogBase::schedulePreview);
    }
    else if (QAbstractSpinBox* spinBox = qobject_cast<QAbstractSpinBox*>(widget))
    {
        if (QSpinBox* intSpin = qobject_cast<QSpinBox*>(spinBox))
        {
            connect(intSpin, &QSpinBox::valueChanged, this, &PageMarksDialogBase::schedulePreview);
        }
        else if (QDoubleSpinBox* doubleSpin = qobject_cast<QDoubleSpinBox*>(spinBox))
        {
            connect(doubleSpin, &QDoubleSpinBox::valueChanged, this, &PageMarksDialogBase::schedulePreview);
        }
    }
    else if (QComboBox* comboBox = qobject_cast<QComboBox*>(widget))
    {
        connect(comboBox, &QComboBox::currentIndexChanged, this, &PageMarksDialogBase::schedulePreview);
    }
    else if (QAbstractButton* button = qobject_cast<QAbstractButton*>(widget))
    {
        connect(button, &QAbstractButton::toggled, this, &PageMarksDialogBase::schedulePreview);
    }

    if (PageMarksColorButton* colorButton = qobject_cast<PageMarksColorButton*>(widget))
    {
        connect(colorButton, &PageMarksColorButton::colorChanged, this, &PageMarksDialogBase::schedulePreview);
    }
}

QComboBox* PageMarksDialogBase::createFontCombo(PageMarks::Font font)
{
    QComboBox* comboBox = new QComboBox(this);
    const std::pair<PageMarks::Font, QString> fonts[] = {
        { PageMarks::Font::Helvetica, tr("Helvetica") },
        { PageMarks::Font::HelveticaBold, tr("Helvetica Bold") },
        { PageMarks::Font::HelveticaOblique, tr("Helvetica Oblique") },
        { PageMarks::Font::HelveticaBoldOblique, tr("Helvetica Bold Oblique") },
        { PageMarks::Font::TimesRoman, tr("Times") },
        { PageMarks::Font::TimesBold, tr("Times Bold") },
        { PageMarks::Font::TimesItalic, tr("Times Italic") },
        { PageMarks::Font::TimesBoldItalic, tr("Times Bold Italic") },
        { PageMarks::Font::Courier, tr("Courier") },
        { PageMarks::Font::CourierBold, tr("Courier Bold") },
        { PageMarks::Font::CourierOblique, tr("Courier Oblique") },
        { PageMarks::Font::CourierBoldOblique, tr("Courier Bold Oblique") },
    };
    for (const auto& [value, name] : fonts)
    {
        comboBox->addItem(name, int(value));
    }
    comboBox->setCurrentIndex(comboBox->findData(int(font)));
    comboBox->setToolTip(tr("The standard fonts of PDF - every PDF reader has them, so they are not embedded. "
                            "Characters, which these fonts do not have, are written as \"?\"."));
    return comboBox;
}

PageMarks::Font PageMarksDialogBase::getFont(const QComboBox* comboBox)
{
    return PageMarks::Font(comboBox->currentData().toInt());
}

void PageMarksDialogBase::updatePreview()
{
    QString errorMessage;
    const std::vector<pdf::PDFInteger> pages = PageMarks::parsePageRange(getPageRange(), m_document->getCatalog()->getPageCount(), &errorMessage);

    if (!pages.empty())
    {
        // The mark is written into a copy of the document, to the first chosen page only
        pdf::PDFDocumentBuilder builder(m_document);
        if (write(&builder, &errorMessage, pages.front()))
        {
            const pdf::PDFDocument document = builder.build();
            // Rendered in the pixels of the screen (sharp on a high resolution screen)
            const qreal ratio = devicePixelRatioF();
            const QSize size = m_previewLabel->size().boundedTo(QSize(PREVIEW_WIDTH * 2, PREVIEW_HEIGHT * 2));
            QImage image = renderPage(&document, pages.front(), (QSizeF(size) * ratio).toSize());
            image.setDevicePixelRatio(ratio);
            m_previewLabel->setPixmap(QPixmap::fromImage(image));
            m_previewLabel->setToolTip(tr("Page %1").arg(pages.front() + 1));
        }
    }
    else if (errorMessage.isEmpty())
    {
        errorMessage = tr("No page is chosen.");
    }

    m_errorLabel->setText(errorMessage);
    m_okButton->setEnabled(errorMessage.isEmpty());
}

void PageMarksDialogBase::accept()
{
    // The values are checked once more (the preview can still wait for its timer)
    m_previewTimer->stop();
    updatePreview();
    if (m_okButton->isEnabled())
    {
        QDialog::accept();
    }
}

WatermarkDialog::WatermarkDialog(const pdf::PDFDocument* document, QWidget* parent) :
    PageMarksDialogBase(document, tr("Add Watermark"), parent)
{
    QGroupBox* sourceBox = new QGroupBox(tr("Source"), this);
    QGridLayout* sourceLayout = new QGridLayout(sourceBox);

    m_textRadio = new QRadioButton(tr("Text"), sourceBox);
    m_textRadio->setObjectName(QStringLiteral("watermarkTextRadio"));
    m_textRadio->setChecked(true);
    m_textEdit = new QLineEdit(sourceBox);
    m_textEdit->setObjectName(QStringLiteral("watermarkTextEdit"));
    m_textEdit->setPlaceholderText(tr("For example CONFIDENTIAL or DRAFT"));
    sourceLayout->addWidget(m_textRadio, 0, 0);
    sourceLayout->addWidget(m_textEdit, 0, 1, 1, 3);

    m_fontCombo = createFontCombo(PageMarks::Font::HelveticaBold);
    m_sizeSpin = new QDoubleSpinBox(sourceBox);
    m_sizeSpin->setRange(1, 1000);
    m_sizeSpin->setDecimals(1);
    m_sizeSpin->setValue(72);
    m_sizeSpin->setSuffix(tr(" pt"));
    m_fitCheck = new QCheckBox(tr("Fit to page"), sourceBox);
    m_fitCheck->setObjectName(QStringLiteral("watermarkFitCheck"));
    m_fitCheck->setToolTip(tr("Choose the font size so the text fills the page (a diagonal text runs corner to corner, with a margin)"));
    m_fitCheck->setChecked(true);   // PDF Fire: fit to page is the default (as the user asked)
    m_colorButton = new PageMarksColorButton(QColor(128, 128, 128), sourceBox);
    QHBoxLayout* textStyleLayout = new QHBoxLayout();
    textStyleLayout->addWidget(m_fontCombo, 1);
    textStyleLayout->addWidget(m_sizeSpin);
    textStyleLayout->addWidget(m_fitCheck);
    textStyleLayout->addWidget(m_colorButton);
    sourceLayout->addLayout(textStyleLayout, 1, 1, 1, 3);

    m_imageRadio = new QRadioButton(tr("Image"), sourceBox);
    m_imageRadio->setObjectName(QStringLiteral("watermarkImageRadio"));
    m_imageEdit = new QLineEdit(sourceBox);
    m_imageEdit->setObjectName(QStringLiteral("watermarkImageEdit"));
    m_imageEdit->setPlaceholderText(tr("A PNG or JPEG file (a logo, a stamp)"));
    m_browseButton = new QPushButton(tr("Browse..."), sourceBox);
    sourceLayout->addWidget(m_imageRadio, 2, 0);
    sourceLayout->addWidget(m_imageEdit, 2, 1, 1, 2);
    sourceLayout->addWidget(m_browseButton, 2, 3);

    m_scaleSpin = new QSpinBox(sourceBox);
    m_scaleSpin->setRange(1, 2000);
    m_scaleSpin->setValue(100);
    m_scaleSpin->setSuffix(tr(" %"));
    m_relativeCheck = new QCheckBox(tr("of the page size"), sourceBox);
    m_relativeCheck->setObjectName(QStringLiteral("watermarkRelativeCheck"));
    m_relativeCheck->setToolTip(tr("Scale of the largest size, which fits the page (otherwise of the size of the image)"));
    m_relativeCheck->setChecked(true);  // PDF Fire: an image fits the page by default too
    QHBoxLayout* imageStyleLayout = new QHBoxLayout();
    imageStyleLayout->addWidget(new QLabel(tr("Scale:"), sourceBox));
    imageStyleLayout->addWidget(m_scaleSpin);
    imageStyleLayout->addWidget(m_relativeCheck);
    imageStyleLayout->addStretch(1);
    sourceLayout->addLayout(imageStyleLayout, 3, 1, 1, 3);
    getSettingsLayout()->addWidget(sourceBox);

    QGroupBox* appearanceBox = new QGroupBox(tr("Appearance"), this);
    QFormLayout* appearanceLayout = new QFormLayout(appearanceBox);
    m_opacitySpin = new QSpinBox(appearanceBox);
    m_opacitySpin->setObjectName(QStringLiteral("watermarkOpacitySpin"));
    m_opacitySpin->setRange(1, 100);
    m_opacitySpin->setValue(40);        // PDF Fire: 40 % is the default opacity
    m_opacitySpin->setSuffix(tr(" %"));
    appearanceLayout->addRow(tr("Opacity:"), m_opacitySpin);

    m_rotationCombo = new QComboBox(appearanceBox);
    m_rotationCombo->setObjectName(QStringLiteral("watermarkRotationCombo"));
    m_rotationCombo->addItem(tr("Diagonal (corner to corner)"));
    m_rotationCombo->addItem(tr("45°"), 45);
    m_rotationCombo->addItem(tr("0° (horizontal)"), 0);
    m_rotationCombo->addItem(tr("-45°"), -45);
    m_rotationCombo->addItem(tr("90°"), 90);
    m_rotationCombo->addItem(tr("Custom"));
    m_rotationSpin = new QSpinBox(appearanceBox);
    m_rotationSpin->setRange(-360, 360);
    m_rotationSpin->setSuffix(tr("°"));
    m_rotationSpin->setToolTip(tr("Degrees, counterclockwise"));
    QHBoxLayout* rotationLayout = new QHBoxLayout();
    rotationLayout->addWidget(m_rotationCombo, 1);
    rotationLayout->addWidget(m_rotationSpin);
    appearanceLayout->addRow(tr("Rotation:"), rotationLayout);

    m_layerCombo = new QComboBox(appearanceBox);
    m_layerCombo->setObjectName(QStringLiteral("watermarkLayerCombo"));
    m_layerCombo->addItem(tr("In front of the page content"));
    m_layerCombo->addItem(tr("Behind the page content"));
    m_layerCombo->setToolTip(tr("Behind the content, the watermark is hidden by pictures and filled areas (a scanned page hides it completely)"));
    appearanceLayout->addRow(tr("Location:"), m_layerCombo);
    getSettingsLayout()->addWidget(appearanceBox);

    QGroupBox* positionBox = new QGroupBox(tr("Position"), this);
    QHBoxLayout* positionLayout = new QHBoxLayout(positionBox);
    QGridLayout* gridLayout = new QGridLayout();
    gridLayout->setSpacing(2);
    m_positionGroup = new QButtonGroup(this);
    const Qt::Alignment rows[] = { Qt::AlignTop, Qt::AlignVCenter, Qt::AlignBottom };
    const Qt::Alignment columns[] = { Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight };
    const char* names[3][3] = { { "TopLeft", "Top", "TopRight" }, { "Left", "Center", "Right" }, { "BottomLeft", "Bottom", "BottomRight" } };
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            QRadioButton* button = new QRadioButton(positionBox);
            button->setObjectName(QStringLiteral("watermarkPosition%1").arg(QString::fromLatin1(names[row][column])));
            button->setToolTip(tr("Place the watermark here on the page"));
            m_positionGroup->addButton(button, int(rows[row] | columns[column]));
            gridLayout->addWidget(button, row, column);
            button->setChecked(row == 1 && column == 1);
            watch(button);
        }
    }
    positionLayout->addLayout(gridLayout);
    QFormLayout* offsetLayout = new QFormLayout();
    m_offsetXSpin = createPointSpin(0, 5000, positionBox);
    m_offsetXSpin->setToolTip(tr("Moves the watermark to the right (a negative value to the left)"));
    m_offsetYSpin = createPointSpin(0, 5000, positionBox);
    m_offsetYSpin->setToolTip(tr("Moves the watermark up (a negative value down)"));
    offsetLayout->addRow(tr("Move right:"), m_offsetXSpin);
    offsetLayout->addRow(tr("Move up:"), m_offsetYSpin);
    positionLayout->addLayout(offsetLayout, 1);
    getSettingsLayout()->addWidget(positionBox);

    QGroupBox* pagesBox = new QGroupBox(tr("Pages"), this);
    QVBoxLayout* pagesLayout = new QVBoxLayout(pagesBox);
    m_pagesEdit = new QLineEdit(QStringLiteral("all"), pagesBox);
    m_pagesEdit->setObjectName(QStringLiteral("watermarkPagesEdit"));
    pagesLayout->addWidget(m_pagesEdit);
    pagesLayout->addWidget(createHint(tr("For example: all, 1-3, 5, 8- (to the end), odd, even"), pagesBox));
    getSettingsLayout()->addWidget(pagesBox);
    getSettingsLayout()->addStretch(1);

    for (QWidget* widget : std::initializer_list<QWidget*>{ m_textRadio, m_imageRadio, m_textEdit, m_fontCombo, m_sizeSpin, m_fitCheck, m_colorButton,
                                                           m_imageEdit, m_scaleSpin, m_relativeCheck, m_opacitySpin, m_rotationCombo, m_rotationSpin,
                                                           m_layerCombo, m_offsetXSpin, m_offsetYSpin, m_pagesEdit })
    {
        watch(widget);
    }

    connect(m_textRadio, &QRadioButton::toggled, this, &WatermarkDialog::updateEnabled);
    connect(m_fitCheck, &QCheckBox::toggled, this, &WatermarkDialog::updateEnabled);
    connect(m_rotationCombo, &QComboBox::currentIndexChanged, this, &WatermarkDialog::updateEnabled);
    connect(m_browseButton, &QPushButton::clicked, this, &WatermarkDialog::browseImage);

    // A text watermark is diagonal, a logo is upright - until the user chooses the rotation
    connect(m_rotationCombo, &QComboBox::activated, this, [this]() { m_rotationChosen = true; });
    connect(m_imageRadio, &QRadioButton::toggled, this, [this](bool image)
    {
        if (!m_rotationChosen)
        {
            m_rotationCombo->setCurrentIndex(image ? m_rotationCombo->findData(0) : 0);
        }
    });

    // A file name given to the image (by Browse, or pasted) means the image is wanted
    connect(m_imageEdit, &QLineEdit::textChanged, this, [this](const QString& text)
    {
        if (!text.isEmpty())
        {
            m_imageRadio->setChecked(true);
        }
    });

    updateEnabled();
    schedulePreview();
}

void WatermarkDialog::updateEnabled()
{
    const bool text = m_textRadio->isChecked();
    m_textEdit->setEnabled(text);
    m_fontCombo->setEnabled(text);
    m_sizeSpin->setEnabled(text && !m_fitCheck->isChecked());
    m_fitCheck->setEnabled(text);
    m_colorButton->setEnabled(text);
    m_imageEdit->setEnabled(!text);
    m_browseButton->setEnabled(!text);
    m_scaleSpin->setEnabled(!text);
    m_relativeCheck->setEnabled(!text);
    m_rotationSpin->setEnabled(m_rotationCombo->currentIndex() == m_rotationCombo->count() - 1);
}

void WatermarkDialog::browseImage()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Image of the Watermark"), m_imageEdit->text(),
                                                          tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff);;All files (*)"));
    if (!fileName.isEmpty())
    {
        m_imageEdit->setText(fileName);
        m_imageRadio->setChecked(true);
    }
}

PageMarks::WatermarkSettings WatermarkDialog::getSettings() const
{
    PageMarks::WatermarkSettings settings;
    settings.useImage = m_imageRadio->isChecked();
    settings.text = m_textEdit->text();
    settings.style.font = getFont(m_fontCombo);
    settings.style.fontSize = m_sizeSpin->value();
    settings.style.color = m_colorButton->getColor();
    settings.fitToPage = m_fitCheck->isChecked();

    if (settings.useImage)
    {
        // The file is read once (not each time the preview is rendered)
        const QString fileName = m_imageEdit->text().trimmed();
        if (fileName != m_loadedImageFile)
        {
            m_loadedImageFile = fileName;
            m_loadedImageData.clear();
            QFile file(fileName);
            if (!fileName.isEmpty() && file.open(QFile::ReadOnly))
            {
                m_loadedImageData = file.readAll();
            }
        }
        settings.imageData = m_loadedImageData;
    }
    settings.imageScale = m_scaleSpin->value() / 100.0;
    settings.imageScaleRelativeToPage = m_relativeCheck->isChecked();

    settings.opacity = m_opacitySpin->value() / 100.0;
    const int rotationIndex = m_rotationCombo->currentIndex();
    settings.diagonal = rotationIndex == 0;
    settings.rotation = rotationIndex == m_rotationCombo->count() - 1 ? m_rotationSpin->value() : m_rotationCombo->currentData().toDouble();
    settings.behind = m_layerCombo->currentIndex() == 1;
    settings.alignment = Qt::Alignment(m_positionGroup->checkedId());
    settings.offsetX = m_offsetXSpin->value();
    settings.offsetY = m_offsetYSpin->value();
    settings.pageRange = m_pagesEdit->text();
    return settings;
}

bool WatermarkDialog::write(pdf::PDFDocumentBuilder* builder, QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage) const
{
    return PageMarks::addWatermark(builder, m_document, getSettings(), errorMessage, onlyPage);
}

QString WatermarkDialog::getPageRange() const
{
    return m_pagesEdit->text();
}

HeaderFooterDialog::HeaderFooterDialog(const pdf::PDFDocument* document, const QString& fileName, QWidget* parent) :
    PageMarksDialogBase(document, tr("Add Header & Footer"), parent),
    m_fileName(fileName),
    m_lastBoxEdit(nullptr)
{
    QGroupBox* fontBox = new QGroupBox(tr("Font"), this);
    QHBoxLayout* fontLayout = new QHBoxLayout(fontBox);
    m_fontCombo = createFontCombo(PageMarks::Font::Helvetica);
    m_sizeSpin = new QDoubleSpinBox(fontBox);
    m_sizeSpin->setRange(1, 200);
    m_sizeSpin->setDecimals(1);
    m_sizeSpin->setValue(10);
    m_sizeSpin->setSuffix(tr(" pt"));
    m_colorButton = new PageMarksColorButton(Qt::black, fontBox);
    fontLayout->addWidget(m_fontCombo, 1);
    fontLayout->addWidget(m_sizeSpin);
    fontLayout->addWidget(m_colorButton);
    getSettingsLayout()->addWidget(fontBox);

    QGroupBox* marginBox = new QGroupBox(tr("Margins"), this);
    QGridLayout* marginLayout = new QGridLayout(marginBox);
    m_marginTopSpin = createPointSpin(36, 2000, marginBox);
    m_marginBottomSpin = createPointSpin(36, 2000, marginBox);
    m_marginLeftSpin = createPointSpin(72, 2000, marginBox);
    m_marginRightSpin = createPointSpin(72, 2000, marginBox);
    marginLayout->addWidget(new QLabel(tr("Top:"), marginBox), 0, 0);
    marginLayout->addWidget(m_marginTopSpin, 0, 1);
    marginLayout->addWidget(new QLabel(tr("Bottom:"), marginBox), 0, 2);
    marginLayout->addWidget(m_marginBottomSpin, 0, 3);
    marginLayout->addWidget(new QLabel(tr("Left:"), marginBox), 1, 0);
    marginLayout->addWidget(m_marginLeftSpin, 1, 1);
    marginLayout->addWidget(new QLabel(tr("Right:"), marginBox), 1, 2);
    marginLayout->addWidget(m_marginRightSpin, 1, 3);
    getSettingsLayout()->addWidget(marginBox);

    QGroupBox* textBox = new QGroupBox(tr("Text"), this);
    QGridLayout* textLayout = new QGridLayout(textBox);
    textLayout->addWidget(new QLabel(tr("Left"), textBox), 0, 1, Qt::AlignHCenter);
    textLayout->addWidget(new QLabel(tr("Center"), textBox), 0, 2, Qt::AlignHCenter);
    textLayout->addWidget(new QLabel(tr("Right"), textBox), 0, 3, Qt::AlignHCenter);
    textLayout->addWidget(new QLabel(tr("Header:"), textBox), 1, 0);
    textLayout->addWidget(new QLabel(tr("Footer:"), textBox), 2, 0);
    const char* boxNames[PageMarks::BoxCount] = { "headerLeftEdit", "headerCenterEdit", "headerRightEdit", "footerLeftEdit", "footerCenterEdit", "footerRightEdit" };
    for (int box = 0; box < PageMarks::BoxCount; ++box)
    {
        QLineEdit* lineEdit = new QLineEdit(textBox);
        lineEdit->setObjectName(QString::fromLatin1(boxNames[box]));
        lineEdit->setMinimumWidth(130);
        lineEdit->installEventFilter(this);
        textLayout->addWidget(lineEdit, 1 + box / 3, 1 + box % 3);
        m_boxEdits[box] = lineEdit;
    }
    m_lastBoxEdit = m_boxEdits[PageMarks::FooterCenter];

    QHBoxLayout* tokenLayout = new QHBoxLayout();
    tokenLayout->addWidget(new QLabel(tr("Insert:"), textBox));
    const std::pair<QString, QString> tokens[] = {
        { tr("Page Number"), QStringLiteral("<<page>>") },
        { tr("Total Pages"), QStringLiteral("<<pages>>") },
        { tr("Date"), QStringLiteral("<<date>>") },
        { tr("File Name"), QStringLiteral("<<filename>>") },
    };
    for (const auto& [label, token] : tokens)
    {
        QPushButton* button = new QPushButton(label, textBox);
        button->setToolTip(tr("Inserts %1 into the box last clicked").arg(token));
        button->setFocusPolicy(Qt::NoFocus);
        const QString insertedToken = token;
        connect(button, &QPushButton::clicked, this, [this, insertedToken]() { insertToken(insertedToken); });
        tokenLayout->addWidget(button);
    }
    tokenLayout->addStretch(1);
    textLayout->addLayout(tokenLayout, 3, 0, 1, 4);

    m_dateFormatCombo = new QComboBox(textBox);
    for (const QString& format : { QStringLiteral("M/d/yyyy"), QStringLiteral("MM/dd/yyyy"), QStringLiteral("d/M/yyyy"), QStringLiteral("dd.MM.yyyy"),
                                   QStringLiteral("yyyy-MM-dd"), QStringLiteral("MMMM d, yyyy"), QStringLiteral("d MMMM yyyy"), QStringLiteral("MMM d, yyyy") })
    {
        m_dateFormatCombo->addItem(QStringLiteral("%1   (%2)").arg(QDate::currentDate().toString(format), format), format);
    }
    QHBoxLayout* dateLayout = new QHBoxLayout();
    dateLayout->addWidget(new QLabel(tr("Date format:"), textBox));
    dateLayout->addWidget(m_dateFormatCombo, 1);
    textLayout->addLayout(dateLayout, 4, 0, 1, 4);
    m_dateFormatCombo->setToolTip(tr("The format of <<date>>. A date can also have its own format: <<date:yyyy-MM-dd>>"));
    QLabel* exampleLabel = createHint(tr("Example: Page <<page>> of <<pages>>"), textBox);
    exampleLabel->setWordWrap(false);
    textLayout->addWidget(exampleLabel, 5, 0, 1, 4);
    getSettingsLayout()->addWidget(textBox);

    QGroupBox* pagesBox = new QGroupBox(tr("Pages"), this);
    QFormLayout* pagesLayout = new QFormLayout(pagesBox);
    m_pagesEdit = new QLineEdit(QStringLiteral("all"), pagesBox);
    m_pagesEdit->setObjectName(QStringLiteral("headerFooterPagesEdit"));
    m_pagesEdit->setToolTip(tr("For example: all, 1-3, 5, 8- (to the end), odd, even"));
    pagesLayout->addRow(tr("Pages:"), m_pagesEdit);
    m_startNumberSpin = new QSpinBox(pagesBox);
    m_startNumberSpin->setObjectName(QStringLiteral("headerFooterStartSpin"));
    m_startNumberSpin->setRange(-100000, 1000000);
    m_startNumberSpin->setValue(1);
    m_startNumberSpin->setToolTip(tr("The number of the first page of the document - the following pages count on from it. "
                                     "With a cover page, set 0 so the second page is page 1."));
    pagesLayout->addRow(tr("Number of page 1:"), m_startNumberSpin);
    m_skipFirstCheck = new QCheckBox(tr("No header and footer on the first page"), pagesBox);
    m_skipFirstCheck->setObjectName(QStringLiteral("headerFooterSkipFirstCheck"));
    pagesLayout->addRow(QString(), m_skipFirstCheck);
    getSettingsLayout()->addWidget(pagesBox);
    getSettingsLayout()->addStretch(1);

    for (QWidget* widget : std::initializer_list<QWidget*>{ m_fontCombo, m_sizeSpin, m_colorButton, m_marginTopSpin, m_marginBottomSpin, m_marginLeftSpin,
                                                           m_marginRightSpin, m_dateFormatCombo, m_pagesEdit, m_startNumberSpin, m_skipFirstCheck })
    {
        watch(widget);
    }
    for (QLineEdit* lineEdit : m_boxEdits)
    {
        watch(lineEdit);
    }

    schedulePreview();
}

bool HeaderFooterDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn)
    {
        if (QLineEdit* lineEdit = qobject_cast<QLineEdit*>(watched))
        {
            m_lastBoxEdit = lineEdit;
        }
    }
    return PageMarksDialogBase::eventFilter(watched, event);
}

void HeaderFooterDialog::insertToken(const QString& token)
{
    if (m_lastBoxEdit)
    {
        m_lastBoxEdit->insert(token);
        m_lastBoxEdit->setFocus();
    }
}

PageMarks::HeaderFooterSettings HeaderFooterDialog::getSettings() const
{
    PageMarks::HeaderFooterSettings settings;
    for (int box = 0; box < PageMarks::BoxCount; ++box)
    {
        settings.texts[box] = m_boxEdits[box]->text();
    }
    settings.style.font = getFont(m_fontCombo);
    settings.style.fontSize = m_sizeSpin->value();
    settings.style.color = m_colorButton->getColor();
    settings.marginTop = m_marginTopSpin->value();
    settings.marginBottom = m_marginBottomSpin->value();
    settings.marginLeft = m_marginLeftSpin->value();
    settings.marginRight = m_marginRightSpin->value();
    settings.pageRange = m_pagesEdit->text();
    settings.skipFirstPage = m_skipFirstCheck->isChecked();
    settings.startPageNumber = m_startNumberSpin->value();
    settings.dateFormat = m_dateFormatCombo->currentData().toString();
    settings.date = QDate::currentDate();
    settings.fileName = m_fileName;
    return settings;
}

bool HeaderFooterDialog::write(pdf::PDFDocumentBuilder* builder, QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage) const
{
    return PageMarks::addHeaderFooter(builder, m_document, getSettings(), errorMessage, onlyPage);
}

QString HeaderFooterDialog::getPageRange() const
{
    // The preview shows the first page, which gets the header
    if (m_skipFirstCheck->isChecked())
    {
        QString errorMessage;
        std::vector<pdf::PDFInteger> pages = PageMarks::parsePageRange(m_pagesEdit->text(), m_document->getCatalog()->getPageCount(), &errorMessage);
        if (!pages.empty() && pages.front() == 0)
        {
            return pages.size() > 1 ? QString::number(pages[1] + 1) : m_pagesEdit->text();
        }
    }
    return m_pagesEdit->text();
}

BatesDialog::BatesDialog(const pdf::PDFDocument* document, QWidget* parent) :
    PageMarksDialogBase(document, tr("Bates Numbering"), parent)
{
    QGroupBox* numberBox = new QGroupBox(tr("Number"), this);
    QFormLayout* numberLayout = new QFormLayout(numberBox);
    m_prefixEdit = new QLineEdit(numberBox);
    m_prefixEdit->setObjectName(QStringLiteral("batesPrefixEdit"));
    m_prefixEdit->setPlaceholderText(tr("For example ABC-"));
    m_suffixEdit = new QLineEdit(numberBox);
    m_suffixEdit->setObjectName(QStringLiteral("batesSuffixEdit"));
    m_startEdit = new QLineEdit(QStringLiteral("1"), numberBox);
    m_startEdit->setObjectName(QStringLiteral("batesStartEdit"));
    m_startEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{1,15}")), m_startEdit));
    m_digitsSpin = new QSpinBox(numberBox);
    m_digitsSpin->setObjectName(QStringLiteral("batesDigitsSpin"));
    m_digitsSpin->setRange(1, 15);
    m_digitsSpin->setValue(6);
    m_digitsSpin->setToolTip(tr("The number is padded by zeros to this number of digits"));
    m_sampleLabel = new QLabel(numberBox);
    m_sampleLabel->setObjectName(QStringLiteral("batesSampleLabel"));
    numberLayout->addRow(tr("Prefix:"), m_prefixEdit);
    numberLayout->addRow(tr("Start number:"), m_startEdit);
    numberLayout->addRow(tr("Digits:"), m_digitsSpin);
    numberLayout->addRow(tr("Suffix:"), m_suffixEdit);
    numberLayout->addRow(tr("First number:"), m_sampleLabel);
    getSettingsLayout()->addWidget(numberBox);

    QGroupBox* appearanceBox = new QGroupBox(tr("Appearance"), this);
    QFormLayout* appearanceLayout = new QFormLayout(appearanceBox);
    m_positionCombo = new QComboBox(appearanceBox);
    m_positionCombo->setObjectName(QStringLiteral("batesPositionCombo"));
    m_positionCombo->addItem(tr("Top left"), int(PageMarks::HeaderLeft));
    m_positionCombo->addItem(tr("Top center"), int(PageMarks::HeaderCenter));
    m_positionCombo->addItem(tr("Top right"), int(PageMarks::HeaderRight));
    m_positionCombo->addItem(tr("Bottom left"), int(PageMarks::FooterLeft));
    m_positionCombo->addItem(tr("Bottom center"), int(PageMarks::FooterCenter));
    m_positionCombo->addItem(tr("Bottom right"), int(PageMarks::FooterRight));
    m_positionCombo->setCurrentIndex(m_positionCombo->findData(int(PageMarks::FooterRight)));
    appearanceLayout->addRow(tr("Position:"), m_positionCombo);
    m_fontCombo = createFontCombo(PageMarks::Font::HelveticaBold);
    appearanceLayout->addRow(tr("Font:"), m_fontCombo);
    m_sizeSpin = new QDoubleSpinBox(appearanceBox);
    m_sizeSpin->setRange(1, 200);
    m_sizeSpin->setDecimals(1);
    m_sizeSpin->setValue(10);
    m_sizeSpin->setSuffix(tr(" pt"));
    m_colorButton = new PageMarksColorButton(Qt::black, appearanceBox);
    QHBoxLayout* sizeLayout = new QHBoxLayout();
    sizeLayout->addWidget(m_sizeSpin);
    sizeLayout->addWidget(m_colorButton);
    sizeLayout->addStretch(1);
    appearanceLayout->addRow(tr("Size:"), sizeLayout);
    m_marginSpin = createPointSpin(36, 2000, appearanceBox);
    m_marginSpin->setToolTip(tr("Distance from the edges of the page"));
    appearanceLayout->addRow(tr("Margin:"), m_marginSpin);
    getSettingsLayout()->addWidget(appearanceBox);

    QGroupBox* pagesBox = new QGroupBox(tr("Pages"), this);
    QVBoxLayout* pagesLayout = new QVBoxLayout(pagesBox);
    m_pagesEdit = new QLineEdit(QStringLiteral("all"), pagesBox);
    m_pagesEdit->setObjectName(QStringLiteral("batesPagesEdit"));
    pagesLayout->addWidget(m_pagesEdit);
    pagesLayout->addWidget(createHint(tr("Each numbered page gets the next number. To continue the numbers of another document, "
                                         "start with the number after its last one."), pagesBox));
    getSettingsLayout()->addWidget(pagesBox);
    getSettingsLayout()->addStretch(1);

    for (QWidget* widget : std::initializer_list<QWidget*>{ m_prefixEdit, m_suffixEdit, m_startEdit, m_digitsSpin, m_positionCombo, m_fontCombo,
                                                           m_sizeSpin, m_colorButton, m_marginSpin, m_pagesEdit })
    {
        watch(widget);
    }
    connect(m_prefixEdit, &QLineEdit::textChanged, this, &BatesDialog::updateSample);
    connect(m_suffixEdit, &QLineEdit::textChanged, this, &BatesDialog::updateSample);
    connect(m_startEdit, &QLineEdit::textChanged, this, &BatesDialog::updateSample);
    connect(m_digitsSpin, &QSpinBox::valueChanged, this, &BatesDialog::updateSample);

    updateSample();
    schedulePreview();
}

void BatesDialog::updateSample()
{
    const PageMarks::BatesSettings settings = getSettings();
    m_sampleLabel->setText(PageMarks::formatBates(settings.prefix, settings.start, settings.digits, settings.suffix));
}

PageMarks::BatesSettings BatesDialog::getSettings() const
{
    PageMarks::BatesSettings settings;
    settings.prefix = m_prefixEdit->text();
    settings.suffix = m_suffixEdit->text();
    settings.start = m_startEdit->text().toLongLong();
    settings.digits = m_digitsSpin->value();
    settings.position = PageMarks::Box(m_positionCombo->currentData().toInt());
    settings.style.font = getFont(m_fontCombo);
    settings.style.fontSize = m_sizeSpin->value();
    settings.style.color = m_colorButton->getColor();
    settings.marginTop = settings.marginBottom = settings.marginLeft = settings.marginRight = m_marginSpin->value();
    settings.pageRange = m_pagesEdit->text();
    return settings;
}

bool BatesDialog::write(pdf::PDFDocumentBuilder* builder, QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage) const
{
    return PageMarks::addBatesNumbers(builder, m_document, getSettings(), errorMessage, onlyPage);
}

QString BatesDialog::getPageRange() const
{
    return m_pagesEdit->text();
}

BackgroundDialog::BackgroundDialog(const pdf::PDFDocument* document, pdf::PDFInteger currentPageIndex, QWidget* parent) :
    PageMarksDialogBase(document, tr("Add Background"), parent),
    m_currentPageIndex(currentPageIndex)
{
    QGroupBox* sourceBox = new QGroupBox(tr("Source"), this);
    QGridLayout* sourceLayout = new QGridLayout(sourceBox);
    m_fileEdit = new QLineEdit(sourceBox);
    m_fileEdit->setObjectName(QStringLiteral("backgroundFileEdit"));
    m_fileEdit->setPlaceholderText(tr("A PDF file - a letterhead, the background of a form"));
    m_fileEdit->setMinimumWidth(260);
    m_browseButton = new QPushButton(tr("Browse..."), sourceBox);
    m_browseButton->setObjectName(QStringLiteral("backgroundBrowseButton"));
    sourceLayout->addWidget(new QLabel(tr("File:"), sourceBox), 0, 0);
    sourceLayout->addWidget(m_fileEdit, 0, 1, 1, 2);
    sourceLayout->addWidget(m_browseButton, 0, 3);

    m_sourcePageSpin = new QSpinBox(sourceBox);
    m_sourcePageSpin->setObjectName(QStringLiteral("backgroundSourcePageSpin"));
    m_sourcePageSpin->setRange(1, 1);
    m_sourcePageSpin->setToolTip(tr("The page of the file, which becomes the background"));
    m_sourcePageCountLabel = new QLabel(sourceBox);
    QHBoxLayout* sourcePageLayout = new QHBoxLayout();
    sourcePageLayout->addWidget(m_sourcePageSpin);
    sourcePageLayout->addWidget(m_sourcePageCountLabel);
    sourcePageLayout->addStretch(1);
    sourceLayout->addWidget(new QLabel(tr("Page:"), sourceBox), 1, 0);
    sourceLayout->addLayout(sourcePageLayout, 1, 1, 1, 3);
    getSettingsLayout()->addWidget(sourceBox);

    QGroupBox* appearanceBox = new QGroupBox(tr("Appearance"), this);
    QFormLayout* appearanceLayout = new QFormLayout(appearanceBox);
    m_sizeCombo = new QComboBox(appearanceBox);
    m_sizeCombo->setObjectName(QStringLiteral("backgroundSizeCombo"));
    m_sizeCombo->addItem(tr("Fit to page"));
    m_sizeCombo->addItem(tr("Actual size"));
    m_sizeCombo->setToolTip(tr("Fit to page: the page of the file is scaled to the largest size fitting the page. Actual size: it keeps its own size."));
    m_scaleSpin = new QSpinBox(appearanceBox);
    m_scaleSpin->setObjectName(QStringLiteral("backgroundScaleSpin"));
    m_scaleSpin->setRange(1, 1000);
    m_scaleSpin->setValue(100);
    m_scaleSpin->setSuffix(tr(" %"));
    QHBoxLayout* sizeLayout = new QHBoxLayout();
    sizeLayout->addWidget(m_sizeCombo, 1);
    sizeLayout->addWidget(m_scaleSpin);
    appearanceLayout->addRow(tr("Size:"), sizeLayout);

    m_opacitySpin = new QSpinBox(appearanceBox);
    m_opacitySpin->setObjectName(QStringLiteral("backgroundOpacitySpin"));
    m_opacitySpin->setRange(1, 100);
    m_opacitySpin->setValue(100);
    m_opacitySpin->setSuffix(tr(" %"));
    appearanceLayout->addRow(tr("Opacity:"), m_opacitySpin);

    m_positionCombo = new QComboBox(appearanceBox);
    m_positionCombo->setObjectName(QStringLiteral("backgroundPositionCombo"));
    const std::pair<QString, Qt::Alignment> positions[] = {
        { tr("Centered"), Qt::AlignCenter },
        { tr("Top"), Qt::AlignTop | Qt::AlignHCenter },
        { tr("Bottom"), Qt::AlignBottom | Qt::AlignHCenter },
        { tr("Left"), Qt::AlignLeft | Qt::AlignVCenter },
        { tr("Right"), Qt::AlignRight | Qt::AlignVCenter },
        { tr("Top left"), Qt::AlignTop | Qt::AlignLeft },
        { tr("Top right"), Qt::AlignTop | Qt::AlignRight },
        { tr("Bottom left"), Qt::AlignBottom | Qt::AlignLeft },
        { tr("Bottom right"), Qt::AlignBottom | Qt::AlignRight },
    };
    for (const auto& [name, alignment] : positions)
    {
        m_positionCombo->addItem(name, int(alignment));
    }
    appearanceLayout->addRow(tr("Position:"), m_positionCombo);
    appearanceLayout->addRow(createHint(tr("The background is drawn underneath the page content. A scanned page, or a page filled with white, hides it."), appearanceBox));
    getSettingsLayout()->addWidget(appearanceBox);

    QGroupBox* pagesBox = new QGroupBox(tr("Pages"), this);
    QGridLayout* pagesLayout = new QGridLayout(pagesBox);
    m_allPagesRadio = new QRadioButton(tr("All pages"), pagesBox);
    m_allPagesRadio->setObjectName(QStringLiteral("backgroundAllPagesRadio"));
    m_allPagesRadio->setChecked(true);
    m_currentPageRadio = new QRadioButton(tr("Current page (%1)").arg(currentPageIndex + 1), pagesBox);
    m_currentPageRadio->setObjectName(QStringLiteral("backgroundCurrentPageRadio"));
    m_rangeRadio = new QRadioButton(tr("Pages:"), pagesBox);
    m_rangeRadio->setObjectName(QStringLiteral("backgroundRangeRadio"));
    m_pagesEdit = new QLineEdit(pagesBox);
    m_pagesEdit->setObjectName(QStringLiteral("backgroundPagesEdit"));
    m_pagesEdit->setPlaceholderText(tr("1-3, 5, 8-"));
    pagesLayout->addWidget(m_allPagesRadio, 0, 0, 1, 2);
    pagesLayout->addWidget(m_currentPageRadio, 1, 0, 1, 2);
    pagesLayout->addWidget(m_rangeRadio, 2, 0);
    pagesLayout->addWidget(m_pagesEdit, 2, 1);
    pagesLayout->addWidget(createHint(tr("For example: 1-3, 5, 8- (to the end), odd, even"), pagesBox), 3, 1);
    getSettingsLayout()->addWidget(pagesBox);
    getSettingsLayout()->addStretch(1);

    for (QWidget* widget : std::initializer_list<QWidget*>{ m_sourcePageSpin, m_sizeCombo, m_scaleSpin, m_opacitySpin, m_positionCombo,
                                                           m_allPagesRadio, m_currentPageRadio, m_rangeRadio, m_pagesEdit })
    {
        watch(widget);
    }

    connect(m_fileEdit, &QLineEdit::textChanged, this, &BackgroundDialog::readSourceFile);
    connect(m_browseButton, &QPushButton::clicked, this, &BackgroundDialog::browseFile);
    connect(m_rangeRadio, &QRadioButton::toggled, this, &BackgroundDialog::updateEnabled);

    // Typing a range means the range is wanted
    connect(m_pagesEdit, &QLineEdit::textEdited, this, [this]() { m_rangeRadio->setChecked(true); });

    readSourceFile();
    updateEnabled();
}

void BackgroundDialog::updateEnabled()
{
    m_sourcePageSpin->setEnabled(m_sourceDocument != nullptr);
}

void BackgroundDialog::browseFile()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Background File"), m_fileEdit->text(), tr("PDF document (*.pdf *.PDF *.Pdf);;All files (*)"));
    if (!fileName.isEmpty())
    {
        m_fileEdit->setText(fileName);
    }
}

void BackgroundDialog::readSourceFile()
{
    const QString fileName = m_fileEdit->text().trimmed();
    if (fileName == m_sourceFileName)
    {
        return;
    }

    // Typing reads the file only once it names an existing file
    m_sourceFileName = fileName;
    m_sourceDocument.reset();
    m_sourceError.clear();

    if (fileName.isEmpty())
    {
        m_sourceError = tr("Choose the PDF file of the background.");
    }
    else if (!QFileInfo(fileName).isFile())
    {
        m_sourceError = tr("The file '%1' does not exist.").arg(fileName);
    }
    else
    {
        auto queryPassword = [this, fileName](bool* ok)
        {
            return QInputDialog::getText(this, tr("Password"), tr("The file '%1' is protected by a password:").arg(QFileInfo(fileName).fileName()),
                                         QLineEdit::Password, QString(), ok);
        };

        pdf::PDFDocumentReader reader(nullptr, queryPassword, true, false);
        pdf::PDFDocument sourceDocument = reader.readFromFile(fileName);
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            m_sourceError = reader.getReadingResult() == pdf::PDFDocumentReader::Result::Cancelled ? tr("The file '%1' is protected by a password.").arg(fileName)
                                                                                                    : tr("The file '%1' cannot be read. %2").arg(fileName, reader.getErrorMessage());
        }
        else if (sourceDocument.getCatalog()->getPageCount() == 0)
        {
            m_sourceError = tr("The file '%1' has no pages.").arg(fileName);
        }
        else
        {
            const pdf::PDFSecurityHandler* securityHandler = sourceDocument.getStorage().getSecurityHandler();
            if (!securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Assemble) &&
                !securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Modify))
            {
                m_sourceError = tr("The file '%1' does not allow its pages to be used in another document.").arg(fileName);
            }
            else
            {
                m_sourceDocument = std::make_shared<pdf::PDFDocument>(std::move(sourceDocument));
            }
        }
    }

    const int pageCount = m_sourceDocument ? int(m_sourceDocument->getCatalog()->getPageCount()) : 1;
    m_sourcePageSpin->setRange(1, pageCount);
    m_sourcePageCountLabel->setText(m_sourceDocument ? tr("of %1").arg(pageCount) : QString());
    updateEnabled();
    schedulePreview();
}

PageMarks::BackgroundSettings BackgroundDialog::getSettings() const
{
    PageMarks::BackgroundSettings settings;
    settings.sourcePageIndex = m_sourcePageSpin->value() - 1;
    settings.fitToPage = m_sizeCombo->currentIndex() == 0;
    settings.scale = m_scaleSpin->value() / 100.0;
    settings.opacity = m_opacitySpin->value() / 100.0;
    settings.alignment = Qt::Alignment(m_positionCombo->currentData().toInt());
    settings.pageRange = getPageRange();
    return settings;
}

bool BackgroundDialog::write(pdf::PDFDocumentBuilder* builder, QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage) const
{
    if (!m_sourceDocument)
    {
        *errorMessage = m_sourceError.isEmpty() ? tr("Choose the PDF file of the background.") : m_sourceError;
        return false;
    }
    if (m_rangeRadio->isChecked() && m_pagesEdit->text().trimmed().isEmpty())
    {
        // An empty range is not "all" here - the user chose to type the pages
        *errorMessage = tr("Type the pages, for example 1-3, 5.");
        return false;
    }
    return PageMarks::addBackground(builder, m_document, m_sourceDocument.get(), getSettings(), errorMessage, onlyPage);
}

QString BackgroundDialog::getPageRange() const
{
    if (m_currentPageRadio->isChecked())
    {
        return QString::number(m_currentPageIndex + 1);
    }
    return m_rangeRadio->isChecked() ? m_pagesEdit->text() : QStringLiteral("all");
}

RemoveMarksDialog::RemoveMarksDialog(const pdf::PDFDocument* document, QWidget* parent) :
    QDialog(parent)
{
    setWindowTitle(tr("Remove Watermarks, Headers & Footers"));
    setMinimumWidth(420);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(createHint(tr("Removes the watermarks, headers and footers and Bates numbers added by PDF Fire or by Adobe Acrobat, "
                                    "and the backgrounds added by PDF Fire. The rest of the pages is not changed."), this));

    auto createCheck = [this, document, layout](const QString& text, int kind, const char* objectName)
    {
        const int count = PageMarks::countMarkedPages(document, kind);
        QCheckBox* checkBox = new QCheckBox(count == 1 ? tr("%1 (on 1 page)").arg(text) : tr("%1 (on %2 pages)").arg(text).arg(count), this);
        checkBox->setObjectName(QString::fromLatin1(objectName));
        checkBox->setChecked(count > 0);
        checkBox->setEnabled(count > 0);
        layout->addWidget(checkBox);
        return checkBox;
    };

    m_watermarkCheck = createCheck(tr("Watermarks"), PageMarks::Watermark, "removeWatermarksCheck");
    m_headerFooterCheck = createCheck(tr("Headers and footers"), PageMarks::HeaderFooter, "removeHeaderFooterCheck");
    m_batesCheck = createCheck(tr("Bates numbers"), PageMarks::Bates, "removeBatesCheck");
    m_backgroundCheck = createCheck(tr("Backgrounds"), PageMarks::Background, "removeBackgroundsCheck");   // PDF Fire

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Remove"));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

int RemoveMarksDialog::getKinds() const
{
    int kinds = 0;
    if (m_watermarkCheck->isChecked())
    {
        kinds |= PageMarks::Watermark;
    }
    if (m_headerFooterCheck->isChecked())
    {
        kinds |= PageMarks::HeaderFooter;
    }
    if (m_batesCheck->isChecked())
    {
        kinds |= PageMarks::Bates;
    }
    if (m_backgroundCheck->isChecked())
    {
        kinds |= PageMarks::Background;
    }
    return kinds;
}

}   // namespace pdfplugin
