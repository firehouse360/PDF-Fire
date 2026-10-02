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

#ifndef DEPARTMENTAUTHORITYDIALOG_H
#define DEPARTMENTAUTHORITYDIALOG_H

#include "pdffirecertificateauthority.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace pdfplugin
{

/// PDF Fire: the certificate authority of a department - creates the authority,
/// issues the signing certificates of the members, revokes them, and makes the
/// authority trusted on this computer
class DepartmentAuthorityDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DepartmentAuthorityDialog(QWidget* parent);

private:
    void onCreateAuthority();
    void onOpenAuthority();
    void onTrustAuthorityFile();
    void onIssueCertificate();
    void onRevokeCertificate();
    void onTrustOpenAuthority();
    void onShareCertificate();
    void onPublishRevocationList();
    void onRemoveTrustedAuthority();
    void updateTrustedAuthorities();

    void updateUi();
    void updateButtons();
    void showError(const QString& message);

    pdf::PDFFireCertificateAuthority m_authority;

    QLabel* m_statusLabel = nullptr;
    QTreeWidget* m_certificatesWidget = nullptr;
    QPushButton* m_issueButton = nullptr;
    QPushButton* m_revokeButton = nullptr;
    QPushButton* m_trustButton = nullptr;
    QPushButton* m_shareButton = nullptr;
    QPushButton* m_publishButton = nullptr;
    QTreeWidget* m_trustedWidget = nullptr;
    QPushButton* m_removeTrustedButton = nullptr;
};

}   // namespace pdfplugin

#endif // DEPARTMENTAUTHORITYDIALOG_H
