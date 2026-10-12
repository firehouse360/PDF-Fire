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

#ifndef FIREHOUSE360DIALOG_H
#define FIREHOUSE360DIALOG_H

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;

namespace pdfplugin
{

/// PDF Fire: Sign ▸ Firehouse 360 - the optional connection to the department's
/// Firehouse 360 (Secure Share): connect with a one-time code, the department's
/// authority trusted from there, signatures registered for online verification,
/// and an encrypted copy of the member's certificate online.
class Firehouse360Dialog : public QDialog
{
    Q_OBJECT

public:
    explicit Firehouse360Dialog(QWidget* parent);

private:
    void updateStatus(bool fetchFromServer);
    void onEnabledToggled(bool checked);
    bool connectWithCode();
    void onDisconnect();
    void syncTrust(bool interactive);
    void onSendPending();
    void onSaveCertificateOnline();
    void onGetCertificateOnline();
    void onDeleteCertificateOnline();
    void showError(const QString& message);

    QCheckBox* m_enabledCheckBox = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_connectButton = nullptr;
    QPushButton* m_disconnectButton = nullptr;
    QPushButton* m_trustButton = nullptr;
    QPushButton* m_sendButton = nullptr;
    QPushButton* m_saveKeyButton = nullptr;
    QPushButton* m_getKeyButton = nullptr;
    QPushButton* m_deleteKeyButton = nullptr;
    bool m_hasOnlineCertificate = false;
    bool m_updating = false;
};

}   // namespace pdfplugin

#endif // FIREHOUSE360DIALOG_H
