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

#include "signdialog.h"
#include "ui_signdialog.h"

#include "pdfcertificatemanager.h"
#include "pdfcertificatelisthelper.h"
#include "pdffiresigningrecord.h"
#include "pdffirecertificateauthority.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QComboBox>
#include <QMessageBox>
#include <QSettings>
#include <QUrl>

namespace pdfplugin
{

namespace signdialog
{

constexpr const char* SETTINGS_GROUP = "SignaturePlugin";
constexpr const char* SETTINGS_SIGNATURE_TYPE = "SignatureType";
constexpr const char* SETTINGS_TIMESTAMP_URL = "TimestampUrl";
constexpr const char* SETTINGS_APPEARANCE_FONT = "AppearanceFont";
constexpr const char* SETTINGS_APPEARANCE_DETAILS = "AppearanceDetails";
constexpr const char* SETTINGS_APPEARANCE_NAME = "AppearanceName";
// The details of the computer in the signing record (all off, unless the user turns them on)
constexpr const char* SETTINGS_RECORD_OPERATING_SYSTEM = "Record/OperatingSystem";
constexpr const char* SETTINGS_RECORD_COMPUTER = "Record/Computer";
constexpr const char* SETTINGS_RECORD_LOCAL_ADDRESSES = "Record/LocalAddresses";
constexpr const char* SETTINGS_RECORD_PUBLIC_ADDRESS = "Record/PublicAddress";

/// Timestamp authorities offered to the user. The combo box is editable, so
/// any other authority can be used, too.
constexpr const char* DEFAULT_TIMESTAMP_URLS[] =
{
    "http://timestamp.digicert.com",
    "http://timestamp.sectigo.com",
    "https://freetsa.org/tsr"
};

}   // namespace signdialog

using namespace signdialog;

SignDialog::SignDialog(QWidget* parent, bool isSceneEmpty) :
    QDialog(parent),
    ui(new Ui::SignDialog)
{
    ui->setupUi(this);

    ui->methodCombo->addItem(tr("Sign digitally (visible signature)"), SignDigitally);
    ui->methodCombo->addItem(tr("Sign digitally (invisible signature)"), SignDigitallyInvisible);
    ui->methodCombo->setCurrentIndex(isSceneEmpty ? 1 : 0);

    ui->signatureTypeCombo->addItem(tr("Signature"), SignatureOnly);
    ui->signatureTypeCombo->addItem(tr("Signature with timestamp"), SignatureWithTimestamp);
    ui->signatureTypeCombo->addItem(tr("Document timestamp only"), TimestampOnly);
    // PDF Fire: a timestamp authority attests the time of the signing - the time
    // of the computer can be set to anything. It also keeps the signature valid,
    // when the certificate is revoked later (a member leaves the department).
    ui->signatureTypeCombo->setCurrentIndex(ui->signatureTypeCombo->findData(SignatureWithTimestamp));

    for (const char* timestampUrl : DEFAULT_TIMESTAMP_URLS)
    {
        ui->timestampUrlCombo->addItem(QString::fromLatin1(timestampUrl));
    }
    ui->timestampUrlCombo->setCurrentIndex(0);

    m_certificates = pdf::PDFCertificateManager::getCertificates(pdf::PDFCertificateUsageFilter::DigitalSignature);

    pdf::PDFCertificateListHelper::initComboBox(ui->certificateCombo);
    pdf::PDFCertificateListHelper::fillComboBox(ui->certificateCombo, m_certificates);

    createAppearanceGroup();
    createRecordWidgets();

    // PDF Fire: certification - the author signs the document and states, which
    // changes are allowed after it (none, form filling and signing, also comments)
    if (QGridLayout* methodLayout = qobject_cast<QGridLayout*>(ui->selectCertificateGroupBox->layout()))
    {
        const int row = methodLayout->rowCount();
        m_certificationCombo = new QComboBox(ui->selectCertificateGroupBox);
        m_certificationCombo->setObjectName("certificationCombo");
        m_certificationCombo->addItem(tr("None - an ordinary signature"), 0);
        m_certificationCombo->addItem(tr("Certify - no changes allowed"), 1);
        m_certificationCombo->addItem(tr("Certify - form filling and signing allowed"), 2);
        m_certificationCombo->addItem(tr("Certify - form filling, signing and comments allowed"), 3);
        m_certificationCombo->setToolTip(tr("A certified document shows, that it comes from you. Any change, which the certification "
                                            "doesn't allow, breaks it - Adobe Acrobat and PDF Fire show it. PDF Fire also disables the "
                                            "tools, which would make such a change."));
        methodLayout->addWidget(new QLabel(tr("Certification"), ui->selectCertificateGroupBox), row, 0);
        methodLayout->addWidget(m_certificationCombo, row, 1);
    }
    loadSettings();

    connect(ui->signatureTypeCombo, &QComboBox::currentIndexChanged, this, &SignDialog::updateUi);
    connect(ui->certificateCombo, &QComboBox::currentIndexChanged, this, &SignDialog::onCertificateChanged);
    connect(ui->certificatePasswordEdit, &QLineEdit::textChanged, this, &SignDialog::onCertificateChanged);
    onCertificateChanged();
    updateUi();
}

QStringList SignDialog::getHandwritingFontFamilies()
{
    // PDF Fire: free handwriting fonts (SIL Open Font License / Apache 2.0, see
    // fonts/README.md) - they are a part of the plugin and loaded once
    static const QStringList families = []()
    {
        QStringList result;
        for (const char* fileName : { "GreatVibes-Regular.ttf", "DancingScript.ttf", "Allura-Regular.ttf", "Sacramento-Regular.ttf", "HomemadeApple-Regular.ttf", "Caveat.ttf" })
        {
            const int id = QFontDatabase::addApplicationFont(QString(":/pdfplugins/signatureplugin/fonts/%1").arg(QString::fromLatin1(fileName)));
            if (id != -1 && !QFontDatabase::applicationFontFamilies(id).isEmpty())
            {
                result << QFontDatabase::applicationFontFamilies(id).front();
            }
        }
        return result;
    }();
    return families;
}

void SignDialog::createRecordWidgets()
{
    // PDF Fire: the signer states the intent to sign electronically (ESIGN Act,
    // UETA) - the box is never checked in advance, it is checked for every signing
    QGridLayout* layout = qobject_cast<QGridLayout*>(ui->parametersGroupBox->layout());
    Q_ASSERT(layout);
    const int row = layout->rowCount();

    m_consentCheckBox = new QCheckBox(pdf::PDFFireSigningRecord::getConsentText(), ui->parametersGroupBox);
    m_consentCheckBox->setObjectName("consentCheckBox");
    m_consentCheckBox->setChecked(false);
    layout->addWidget(m_consentCheckBox, row, 0, 1, layout->columnCount());

    // The details of the computer are recorded only, when the signer wants it -
    // or when the department, which issued the certificate, decided so
    m_recordGroup = new QGroupBox(tr("Signing record"), this);
    QVBoxLayout* recordLayout = new QVBoxLayout(m_recordGroup);
    QLabel* alwaysLabel = new QLabel(tr("Always recorded inside the signature: your name, the date and time, a fingerprint of the document "
                                        "and your consent. It can't be changed without breaking the signature. Also record:"), m_recordGroup);
    alwaysLabel->setWordWrap(true);
    recordLayout->addWidget(alwaysLabel);

    m_operatingSystemCheckBox = new QCheckBox(tr("Operating system"), m_recordGroup);
    m_operatingSystemCheckBox->setObjectName("operatingSystemCheckBox");
    m_computerCheckBox = new QCheckBox(tr("Computer name and user account"), m_recordGroup);
    m_computerCheckBox->setObjectName("computerCheckBox");
    m_localAddressCheckBox = new QCheckBox(tr("IP addresses of this computer"), m_recordGroup);
    m_localAddressCheckBox->setObjectName("localAddressCheckBox");
    m_publicAddressCheckBox = new QCheckBox(tr("Public IP address (looked up online)"), m_recordGroup);
    m_publicAddressCheckBox->setObjectName("publicAddressCheckBox");
    for (QCheckBox* checkBox : { m_operatingSystemCheckBox, m_computerCheckBox, m_localAddressCheckBox, m_publicAddressCheckBox })
    {
        recordLayout->addWidget(checkBox);
    }

    m_policyLabel = new QLabel(m_recordGroup);
    m_policyLabel->setObjectName("policyLabel");
    m_policyLabel->setWordWrap(true);
    m_policyLabel->setVisible(false);
    recordLayout->addWidget(m_policyLabel);

    // Under the settings of the signature, above the buttons. PDF Fire: the look of the
    // signature is next to the record (in one column the dialog was taller than a laptop
    // screen, and the squeezed preview covered the text under it)
    if (QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(this->layout()))
    {
        mainLayout->removeWidget(m_appearanceGroup);
        QHBoxLayout* rowLayout = new QHBoxLayout();
        rowLayout->addWidget(m_recordGroup, 1);
        rowLayout->addWidget(m_appearanceGroup, 0);
        mainLayout->insertLayout(mainLayout->indexOf(ui->parametersGroupBox) + 1, rowLayout);
    }

    // The preview shows the chosen details
    for (QCheckBox* checkBox : { m_operatingSystemCheckBox, m_computerCheckBox, m_localAddressCheckBox, m_publicAddressCheckBox })
    {
        connect(checkBox, &QCheckBox::toggled, this, &SignDialog::updatePreview);
    }
    connect(ui->reasonEdit, &QLineEdit::textChanged, this, &SignDialog::updatePreview);
}

void SignDialog::updateSigningPolicy()
{
    // PDF Fire: a certificate issued by a department authority carries the details,
    // which the department wants recorded; the certificate is readable without the
    // password, other certificates are read with it
    const pdf::PDFCertificateEntry* certificate = getCertificate();
    const std::optional<pdf::PDFFireSigningPolicy> policy = certificate ? pdf::PDFFireCertificateAuthority::readSigningPolicy(*certificate, ui->certificatePasswordEdit->text())
                                                                        : std::nullopt;
    const QString policyKey = policy ? QString::fromLatin1(policy->encode()) + policy->authorityName : QString();
    if (policyKey == m_appliedPolicyKey)
    {
        // The choices of the user are kept, until another certificate is chosen
        return;
    }
    m_appliedPolicyKey = policyKey;
    m_isPolicyApplied = policy.has_value();
    m_isPolicyLocked = policy && policy->isLocked;

    if (policy)
    {
        m_operatingSystemCheckBox->setChecked(policy->recordOperatingSystem);
        m_computerCheckBox->setChecked(policy->recordComputer);
        m_localAddressCheckBox->setChecked(policy->recordLocalAddresses);
        m_publicAddressCheckBox->setChecked(policy->recordPublicAddress);
        m_policyLabel->setText(policy->isLocked ? tr("Set by %1 - it can't be changed for this certificate.").arg(policy->authorityName)
                                                : tr("Turned on by %1 for this certificate.").arg(policy->authorityName));
    }
    else
    {
        QSettings settings;
        settings.beginGroup(SETTINGS_GROUP);
        m_operatingSystemCheckBox->setChecked(settings.value(SETTINGS_RECORD_OPERATING_SYSTEM, false).toBool());
        m_computerCheckBox->setChecked(settings.value(SETTINGS_RECORD_COMPUTER, false).toBool());
        m_localAddressCheckBox->setChecked(settings.value(SETTINGS_RECORD_LOCAL_ADDRESSES, false).toBool());
        m_publicAddressCheckBox->setChecked(settings.value(SETTINGS_RECORD_PUBLIC_ADDRESS, false).toBool());
        settings.endGroup();
    }
    m_policyLabel->setVisible(m_isPolicyApplied);
    updateUi();
}

bool SignDialog::isOperatingSystemRecorded() const
{
    return m_operatingSystemCheckBox->isChecked();
}

bool SignDialog::isComputerRecorded() const
{
    return m_computerCheckBox->isChecked();
}

bool SignDialog::isLocalAddressRecorded() const
{
    return m_localAddressCheckBox->isChecked();
}

void SignDialog::setCertification(bool isAvailable, int certification)
{
    if (!m_certificationCombo)
    {
        return;
    }

    m_certificationCombo->setCurrentIndex(qMax(0, m_certificationCombo->findData(isAvailable ? certification : 0)));
    m_certificationCombo->setEnabled(isAvailable);
    if (!isAvailable)
    {
        m_certificationCombo->setToolTip(tr("The document is already signed - only the first signature of a document can certify it."));
    }
}

int SignDialog::getCertification() const
{
    return (m_certificationCombo && getSignatureType() != TimestampOnly) ? m_certificationCombo->currentData().toInt() : 0;
}

bool SignDialog::isConsentAccepted() const
{
    return m_consentCheckBox && m_consentCheckBox->isChecked();
}

bool SignDialog::isPublicAddressRecorded() const
{
    return m_publicAddressCheckBox && m_publicAddressCheckBox->isChecked();
}

void SignDialog::createAppearanceGroup()
{
    m_appearanceGroup = new QGroupBox(tr("How the signature looks on the page"), this);
    QFormLayout* layout = new QFormLayout(m_appearanceGroup);

    m_nameEdit = new QLineEdit(m_appearanceGroup);
    m_nameEdit->setPlaceholderText(tr("Your name"));
    connect(m_nameEdit, &QLineEdit::textEdited, this, [this]() { m_isNameEdited = true; updatePreview(); });
    layout->addRow(tr("Name:"), m_nameEdit);

    m_styleCombo = new QComboBox(m_appearanceGroup);
    for (const QString& family : getHandwritingFontFamilies())
    {
        m_styleCombo->addItem(family, family);
        QFont itemFont(family);
        itemFont.setPointSize(16);
        m_styleCombo->setItemData(m_styleCombo->count() - 1, itemFont, Qt::FontRole);
    }
    m_styleCombo->addItem(tr("Plain (no handwriting)"), QString());
    connect(m_styleCombo, &QComboBox::currentIndexChanged, this, &SignDialog::updatePreview);
    layout->addRow(tr("Style:"), m_styleCombo);

    // The proportions of the signature placed on the page (300 x 75)
    m_previewLabel = new QLabel(m_appearanceGroup);
    m_previewLabel->setFixedSize(400, 100);
    m_previewLabel->setFrameShape(QFrame::StyledPanel);
    layout->addRow(tr("Preview:"), m_previewLabel);

    m_detailsCheckBox = new QCheckBox(tr("Show the details next to the name"), m_appearanceGroup);
    m_detailsCheckBox->setToolTip(tr("Digitally signed by, the date and the reason - and the operating system, computer and "
                                     "IP addresses, which are chosen under Signing record"));
    m_detailsCheckBox->setChecked(true);
    connect(m_detailsCheckBox, &QCheckBox::toggled, this, &SignDialog::updatePreview);
    layout->addRow(QString(), m_detailsCheckBox);

    // Before the buttons (and the spacer above them)
    ui->verticalLayout->insertWidget(2, m_appearanceGroup);
    m_appearanceGroup->setVisible(false);
}

void SignDialog::setNameAppearanceUsed(bool used)
{
    m_isNameAppearanceUsed = used;
    m_appearanceGroup->setVisible(used);
    ui->methodLabel->setVisible(!used);
    ui->methodCombo->setVisible(!used);
    if (used)
    {
        ui->methodCombo->setCurrentIndex(ui->methodCombo->findData(SignDigitally));
    }
    updatePreview();
    adjustSize();
}

void SignDialog::onCertificateChanged()
{
    // The name of the owner of the certificate, unless the user typed another name
    // A protected certificate tells its name only with the right password - until then
    // the name used last time is shown
    if (!m_isNameEdited)
    {
        const pdf::PDFCertificateEntry* certificate = getCertificate();
        const QString name = certificate ? pdf::PDFCertificateManager::getCertificateOwnerName(*certificate, ui->certificatePasswordEdit->text()) : QString();
        if (!name.isEmpty())
        {
            m_nameEdit->setText(name);
        }
        else if (m_nameEdit->text().isEmpty())
        {
            m_nameEdit->setText(QSettings().value(QString("%1/%2").arg(SETTINGS_GROUP, SETTINGS_APPEARANCE_NAME)).toString());
        }
    }
    updateSigningPolicy();
    updatePreview();
}

QString SignDialog::getAppearanceName() const
{
    return m_nameEdit->text().trimmed();
}

QFont SignDialog::getAppearanceFont() const
{
    const QString family = m_styleCombo->currentData().toString();
    return family.isEmpty() ? QFont(QStringLiteral("Sans Serif")) : QFont(family);
}

bool SignDialog::isAppearanceDetailsShown() const
{
    return m_detailsCheckBox->isChecked();
}

QStringList SignDialog::getAppearanceDetails(const QString& name, const QDateTime& dateTime, const QString& reason, const QString& operatingSystem,
                                             const QString& computer, const QStringList& localAddresses, const QString& publicAddress)
{
    // The recorded texts carry explanations in brackets ("Ubuntu 26.04 (linux ...)",
    // "203.0.113.5 (reported by ...)") - the page shows only the value
    auto shortValue = [](const QString& value) { return value.section(QStringLiteral(" ("), 0, 0).trimmed(); };

    QStringList details;
    details << tr("Digitally signed by %1").arg(name);
    details << tr("Date: %1").arg(QLocale().toString(dateTime, QLocale::ShortFormat));
    if (!reason.trimmed().isEmpty())
    {
        details << tr("Reason: %1").arg(reason.trimmed());
    }
    if (!operatingSystem.isEmpty())
    {
        details << tr("OS: %1").arg(shortValue(operatingSystem));
    }
    if (!computer.isEmpty())
    {
        details << tr("Computer: %1").arg(computer);
    }
    if (!localAddresses.isEmpty())
    {
        details << tr("IP: %1").arg(localAddresses.mid(0, 2).join(QStringLiteral(", ")));
    }
    if (!publicAddress.isEmpty())
    {
        details << tr("Public IP: %1").arg(shortValue(publicAddress));
    }
    return details;
}

QStringList SignDialog::getPreviewDetails(const QDateTime& dateTime) const
{
    if (!isAppearanceDetailsShown())
    {
        return QStringList();
    }

    pdf::PDFFireSigningRecord record;
    record.collectComputerInformation(isOperatingSystemRecorded(), isComputerRecorded(), isLocalAddressRecorded());
    const QString computer = record.hostName.isEmpty() ? QString() : (record.userAccount.isEmpty() ? record.hostName : QString("%1 (%2)").arg(record.hostName, record.userAccount));
    return getAppearanceDetails(getAppearanceName(), dateTime, ui->reasonEdit->text(), record.operatingSystem, computer, record.localAddresses,
                                isPublicAddressRecorded() ? tr("looked up when signing") : QString());
}

void SignDialog::drawNameAppearance(QPainter* painter, const QRectF& rect, const QString& name, const QFont& font, const QStringList& details)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    // PDF Fire: the name at the left side, the details at the right side of it
    const bool showDetails = !details.isEmpty();
    const qreal margin = qMin(rect.width(), rect.height()) * 0.06;
    QRectF nameRect = rect.adjusted(margin, margin, -margin, -margin);
    QRectF detailsRect;
    // The details are written in one or two columns - whichever gives the larger
    // letters (two columns help with many short lines, one with a few long ones)
    QFont detailsFont(QStringLiteral("Sans Serif"));
    detailsFont.setPixelSize(100);
    const QFontMetricsF metrics(detailsFont);
    int columnCount = 1;
    qreal scale = 0.0;
    if (showDetails)
    {
        qreal maxLineWidth = 0.0;
        for (const QString& line : details)
        {
            maxLineWidth = qMax(maxLineWidth, metrics.horizontalAdvance(line));
        }

        for (const int columns : { 1, 2 })
        {
            const qreal nameShare = columns == 2 ? 0.40 : 0.46;
            const qreal width = (rect.width() - 2 * margin) * (1.0 - nameShare) - margin;
            const qreal columnWidth = (width - (columns - 1) * margin) / columns;
            const int lines = (int(details.size()) + columns - 1) / columns;
            const qreal lineHeight = (rect.height() - 2 * margin) / qMax(lines, 2);
            const qreal candidate = qMin((lineHeight * 0.78) / metrics.height(), maxLineWidth > 0 ? columnWidth / maxLineWidth : 1.0);
            if (candidate > scale * 1.05)
            {
                scale = candidate;
                columnCount = columns;
            }
        }

        const qreal nameShare = columnCount == 2 ? 0.40 : 0.46;
        nameRect.setWidth((rect.width() - 2 * margin) * nameShare);
        detailsRect = QRectF(QPointF(nameRect.right() + margin, rect.top() + margin), QPointF(rect.right() - margin, rect.bottom() - margin));
    }

    // The texts are drawn as outlines - they fit the box exactly on any device, and
    // look the same in every reader (no font has to be embedded or substituted)
    auto drawFitted = [painter](const QString& text, QFont font, const QRectF& target, Qt::Alignment alignment, const QColor& color)
    {
        font.setPixelSize(100);
        QPainterPath path;
        path.addText(0, 0, font, text);
        const QRectF bounds = path.boundingRect();
        if (bounds.isEmpty() || target.isEmpty())
        {
            return;
        }

        const qreal scale = qMin(target.width() / bounds.width(), target.height() / bounds.height());
        const QSizeF size = bounds.size() * scale;
        qreal x = target.left();
        if (alignment.testFlag(Qt::AlignHCenter))
        {
            x = target.center().x() - size.width() * 0.5;
        }
        const qreal y = target.center().y() - size.height() * 0.5;

        QTransform transform;
        transform.translate(x, y);
        transform.scale(scale, scale);
        transform.translate(-bounds.left(), -bounds.top());

        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawPath(transform.map(path));
        painter->restore();
    };

    drawFitted(name.isEmpty() ? QStringLiteral(" ") : name, font, nameRect.adjusted(0, nameRect.height() * 0.06, 0, -nameRect.height() * 0.06), Qt::AlignLeft, QColor(15, 30, 90));

    if (showDetails)
    {
        // All lines in one size (chosen above) - as a small print
        const int linesPerColumn = (int(details.size()) + columnCount - 1) / columnCount;
        const qreal columnGap = margin;
        const qreal columnWidth = (detailsRect.width() - (columnCount - 1) * columnGap) / columnCount;

        // The block of the lines is centred vertically next to the name
        const qreal usedLineHeight = metrics.height() * scale / 0.78;
        const qreal blockTop = detailsRect.top() + (detailsRect.height() - usedLineHeight * linesPerColumn) * 0.5;
        for (int i = 0; i < int(details.size()); ++i)
        {
            const int column = i / linesPerColumn;
            const int row = i % linesPerColumn;

            QPainterPath path;
            path.addText(0, metrics.ascent(), detailsFont, details[i]);
            QTransform transform;
            transform.translate(detailsRect.left() + column * (columnWidth + columnGap), blockTop + row * usedLineHeight + (usedLineHeight - metrics.height() * scale) * 0.5);
            transform.scale(scale, scale);

            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(70, 70, 70));
            painter->drawPath(transform.map(path));
            painter->restore();
        }
    }

    painter->restore();
}

void SignDialog::updatePreview()
{
    if (!m_previewLabel)
    {
        return;
    }

    const qreal ratio = devicePixelRatioF();
    QPixmap pixmap(m_previewLabel->size() * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::white);
    QPainter painter(&pixmap);
    drawNameAppearance(&painter, QRectF(QPointF(0, 0), QSizeF(m_previewLabel->size())), getAppearanceName(), getAppearanceFont(),
                       getPreviewDetails(QDateTime::currentDateTime()));
    painter.end();
    m_previewLabel->setPixmap(pixmap);
}

SignDialog::~SignDialog()
{
    delete ui;
}

SignDialog::SignMethod SignDialog::getSignMethod() const
{
    return static_cast<SignMethod>(ui->methodCombo->currentData().toInt());
}

SignDialog::SignatureType SignDialog::getSignatureType() const
{
    return static_cast<SignatureType>(ui->signatureTypeCombo->currentData().toInt());
}

QString SignDialog::getPassword() const
{
    return ui->certificatePasswordEdit->text();
}

QString SignDialog::getReasonText() const
{
    return ui->reasonEdit->text();
}

QString SignDialog::getContactInfoText() const
{
    return ui->contactInfoEdit->text();
}

QString SignDialog::getTimestampUrl() const
{
    return ui->timestampUrlCombo->currentText().trimmed();
}

const pdf::PDFCertificateEntry* SignDialog::getCertificate() const
{
    const int index = ui->certificateCombo->currentIndex();
    if (index >= 0 && index < m_certificates.size())
    {
        return &m_certificates.at(index);
    }

    return nullptr;
}

void SignDialog::accept()
{
    const SignatureType signatureType = getSignatureType();

    // A document timestamp is not signed by a certificate of the user, it is
    // signed by the timestamp authority, so no certificate is needed for it.
    if (signatureType != TimestampOnly)
    {
        const pdf::PDFCertificateEntry* certificate = getCertificate();

        // Check certificate
        if (!certificate)
        {
            QMessageBox::critical(this, tr("Error"), tr("Certificate does not exist."));
            ui->certificateCombo->setFocus();
            return;
        }

        // Check we can access the certificate
        if (!pdf::PDFCertificateManager::isCertificateValid(*certificate, ui->certificatePasswordEdit->text()))
        {
            QMessageBox::critical(this, tr("Error"), tr("Password to open certificate is invalid."));
            ui->certificatePasswordEdit->setFocus();
            return;
        }
    }

    if (signatureType != TimestampOnly && !isConsentAccepted())
    {
        QMessageBox::critical(this, tr("Error"), tr("Check the box confirming that you intend to sign this document electronically."));
        m_consentCheckBox->setFocus();
        return;
    }

    if (m_isNameAppearanceUsed && signatureType != TimestampOnly && getAppearanceName().isEmpty())
    {
        QMessageBox::critical(this, tr("Error"), tr("Type the name shown in the signature."));
        m_nameEdit->setFocus();
        return;
    }

    if (signatureType != SignatureOnly)
    {
        const QUrl timestampUrl(getTimestampUrl());
        if (!timestampUrl.isValid() || timestampUrl.scheme().isEmpty() || timestampUrl.host().isEmpty())
        {
            QMessageBox::critical(this, tr("Error"), tr("Address of the timestamp authority is not valid."));
            ui->timestampUrlCombo->setFocus();
            return;
        }
    }

    saveSettings();
    QDialog::accept();
}

void SignDialog::updateUi()
{
    const SignatureType signatureType = getSignatureType();
    const bool isCertificateUsed = signatureType != TimestampOnly;
    const bool isTimestampUsed = signatureType != SignatureOnly;

    // A document timestamp has no visible appearance and no signer, so the
    // settings of the signature are not used by it.
    ui->methodLabel->setEnabled(isCertificateUsed);
    ui->methodCombo->setEnabled(isCertificateUsed);
    ui->certificateLabel->setEnabled(isCertificateUsed);
    ui->certificateCombo->setEnabled(isCertificateUsed);
    ui->passwordLabel->setEnabled(isCertificateUsed);
    ui->certificatePasswordEdit->setEnabled(isCertificateUsed);
    ui->reasonLabel->setEnabled(isCertificateUsed);
    ui->reasonEdit->setEnabled(isCertificateUsed);
    ui->contactInfoLabel->setEnabled(isCertificateUsed);
    ui->contactInfoEdit->setEnabled(isCertificateUsed);
    m_consentCheckBox->setEnabled(isCertificateUsed);
    m_recordGroup->setEnabled(isCertificateUsed);
    for (QCheckBox* checkBox : { m_operatingSystemCheckBox, m_computerCheckBox, m_localAddressCheckBox, m_publicAddressCheckBox })
    {
        checkBox->setEnabled(!m_isPolicyLocked);
    }

    ui->timestampUrlLabel->setEnabled(isTimestampUsed);
    ui->timestampUrlCombo->setEnabled(isTimestampUsed);
}

void SignDialog::loadSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);

    const int signatureTypeIndex = ui->signatureTypeCombo->findData(settings.value(SETTINGS_SIGNATURE_TYPE, SignatureWithTimestamp).toInt());
    if (signatureTypeIndex != -1)
    {
        ui->signatureTypeCombo->setCurrentIndex(signatureTypeIndex);
    }

    const QString timestampUrl = settings.value(SETTINGS_TIMESTAMP_URL).toString();
    if (!timestampUrl.isEmpty())
    {
        ui->timestampUrlCombo->setCurrentText(timestampUrl);
    }

    const int fontIndex = m_styleCombo->findData(settings.value(SETTINGS_APPEARANCE_FONT, m_styleCombo->itemData(0)).toString());
    if (fontIndex != -1)
    {
        m_styleCombo->setCurrentIndex(fontIndex);
    }
    m_detailsCheckBox->setChecked(settings.value(SETTINGS_APPEARANCE_DETAILS, true).toBool());

    settings.endGroup();
}

void SignDialog::saveSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue(SETTINGS_SIGNATURE_TYPE, int(getSignatureType()));
    settings.setValue(SETTINGS_TIMESTAMP_URL, getTimestampUrl());
    settings.setValue(SETTINGS_APPEARANCE_FONT, m_styleCombo->currentData().toString());
    settings.setValue(SETTINGS_APPEARANCE_DETAILS, isAppearanceDetailsShown());
    // The choices made for a certificate of a department are the department's, not the user's
    if (!m_isPolicyApplied)
    {
        settings.setValue(SETTINGS_RECORD_OPERATING_SYSTEM, isOperatingSystemRecorded());
        settings.setValue(SETTINGS_RECORD_COMPUTER, isComputerRecorded());
        settings.setValue(SETTINGS_RECORD_LOCAL_ADDRESSES, isLocalAddressRecorded());
        settings.setValue(SETTINGS_RECORD_PUBLIC_ADDRESS, isPublicAddressRecorded());
    }
    if (m_isNameAppearanceUsed && !getAppearanceName().isEmpty())
    {
        settings.setValue(SETTINGS_APPEARANCE_NAME, getAppearanceName());
    }
    settings.endGroup();
}

}   // namespace pdfplugin



