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

#ifndef PDFFIREEMAILDIALOG_H
#define PDFFIREEMAILDIALOG_H

#include "pdfviewerglobal.h"
#include "pdffiresmtpclient.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace pdfviewer
{

/// PDF Fire: settings of the mail server used to send documents. The password is not
/// stored - it is asked for once, and remembered only until the application is closed.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFFireMailServerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFFireMailServerDialog(QWidget* parent);

    /// Settings stored in the settings of the application (without the password)
    static pdf::PDFSmtpClient::Settings loadSettings();
    static void saveSettings(const pdf::PDFSmtpClient::Settings& settings);
    static bool isConfigured();

    virtual void accept() override;

private:
    QLineEdit* m_hostEdit;
    QSpinBox* m_portEdit;
    QComboBox* m_securityCombo;
    QLineEdit* m_userNameEdit;
    QLineEdit* m_fromAddressEdit;
    QLineEdit* m_fromNameEdit;
};

/// PDF Fire: sends the document as an attachment of an e-mail, through the mail server
class PDFFireEmailDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFFireEmailDialog(QByteArray attachment, QString attachmentFileName, QString subject, QWidget* parent);

    virtual void accept() override;

private:
    void updateServerLabel();

    QByteArray m_attachment;
    QString m_attachmentFileName;
    QLineEdit* m_toEdit;
    QLineEdit* m_subjectEdit;
    QPlainTextEdit* m_textEdit;
    QLabel* m_serverLabel;
};

}   // namespace pdfviewer

#endif // PDFFIREEMAILDIALOG_H
