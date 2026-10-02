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

#ifndef SIGNDIALOG_H
#define SIGNDIALOG_H

#include "pdfcertificatemanager.h"

#include <QDialog>
#include <QFont>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;

namespace Ui
{
class SignDialog;
}

namespace pdfplugin
{

class SignDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SignDialog(QWidget* parent, bool isSceneEmpty);
    virtual ~SignDialog() override;

    virtual void accept() override;

    enum SignMethod
    {
        SignDigitally,
        SignDigitallyInvisible
    };

    enum SignatureType
    {
        /// Digital signature of the document
        SignatureOnly,

        /// Digital signature of the document, the time of the signing is
        /// attested by a timestamp authority
        SignatureWithTimestamp,

        /// Document timestamp, which attests, that the document existed at
        /// the time of the timestamp, without signing it
        TimestampOnly
    };

    SignMethod getSignMethod() const;
    SignatureType getSignatureType() const;
    QString getPassword() const;
    QString getReasonText() const;
    QString getContactInfoText() const;
    QString getTimestampUrl() const;
    const pdf::PDFCertificateEntry* getCertificate() const;

    /// PDF Fire: the signature is shown on the page as a typed name (in a signature
    /// line, or in a box placed on the page). The dialog then asks for the name and
    /// its style (a handwriting font), and the method is always the visible signature.
    void setNameAppearanceUsed(bool used);
    bool isNameAppearanceUsed() const { return m_isNameAppearanceUsed; }
    QString getAppearanceName() const;
    QFont getAppearanceFont() const;
    bool isAppearanceDetailsShown() const;

    /// PDF Fire: the signer agreed to sign electronically (it is a part of the
    /// signing record, the signing is not possible without it)
    bool isConsentAccepted() const;

    /// PDF Fire: the details of the computer written into the signing record -
    /// chosen by the user, or by the department authority, which issued the certificate
    bool isOperatingSystemRecorded() const;
    bool isComputerRecorded() const;
    bool isLocalAddressRecorded() const;
    bool isPublicAddressRecorded() const;

    /// PDF Fire: certification of the document (DocMDP) - 0 = an ordinary signature,
    /// 1 = no changes allowed, 2 = form filling and signing, 3 = also comments
    /// \param isAvailable The document has no signature yet (a certification must be the first)
    /// \param certification Preselected certification
    void setCertification(bool isAvailable, int certification);
    int getCertification() const;

    /// The details recorded were set by a department authority, which issued the certificate
    bool isSigningPolicyApplied() const { return m_isPolicyApplied; }
    bool isSigningPolicyLocked() const { return m_isPolicyLocked; }

    /// Families of the handwriting fonts coming with PDF Fire (loaded on the first call)
    static QStringList getHandwritingFontFamilies();

    /// Draws the look of a signature: the name in the font, scaled to fit, and
    /// optionally the line "Digitally signed by ..." with the date under it
    static void drawNameAppearance(QPainter* painter, const QRectF& rect, const QString& name, const QFont& font,
                                   bool showDetails, const QDateTime& dateTime, const QString& reason);

private:
    /// Enables only the settings, which are used by the selected signature type
    void updateUi();

    void loadSettings();
    void saveSettings();
    void createAppearanceGroup();
    void createRecordWidgets();
    void updateSigningPolicy();
    void updatePreview();
    void onCertificateChanged();

    QGroupBox* m_appearanceGroup = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_styleCombo = nullptr;
    QLabel* m_previewLabel = nullptr;
    QCheckBox* m_detailsCheckBox = nullptr;
    QCheckBox* m_consentCheckBox = nullptr;
    QCheckBox* m_publicAddressCheckBox = nullptr;
    QCheckBox* m_operatingSystemCheckBox = nullptr;
    QCheckBox* m_computerCheckBox = nullptr;
    QCheckBox* m_localAddressCheckBox = nullptr;
    QGroupBox* m_recordGroup = nullptr;
    QComboBox* m_certificationCombo = nullptr;
    QLabel* m_policyLabel = nullptr;
    QString m_appliedPolicyKey = QStringLiteral("-");   ///< Policy applied to the boxes ("-" = none yet)
    bool m_isPolicyApplied = false;
    bool m_isPolicyLocked = false;
    bool m_isNameAppearanceUsed = false;
    bool m_isNameEdited = false;

    Ui::SignDialog* ui;
    pdf::PDFCertificateEntries m_certificates;
};

}   // namespace pdfplugin

#endif // SIGNDIALOG_H
