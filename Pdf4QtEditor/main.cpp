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

#include "pdfeditormainwindow.h"
#include "pdfconstants.h"
#include "pdfsecurityhandler.h"
#include "pdfwidgetutils.h"
#include "pdfviewersettings.h"
#include "pdfapplicationtranslator.h"
#include "pdfsettings.h"

#include <QSettings>
#include "pdffirenaturalspeech.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>

#include "pdffiretheme.h"

#include "pdfdbgheap.h"

void runPDFFireDeveloperTools(QMainWindow* window);

/// PDF Fire: a start of PDF Fire, which never finished, is noticed at the next start.
/// A note is written next to the settings when PDF Fire starts, and removed a few seconds
/// after its window is open. If the note of an earlier start is still there (older than a
/// running start could be), that start froze or crashed - the saved settings are set
/// aside (kept as a backup) and PDF Fire starts with its defaults. (2026-10-03: a Windows
/// PC froze at every start with the settings saved after a document was closed while it
/// was read aloud; deleting the settings was the only way out.)
class PDFFireStartupGuard
{
public:
    PDFFireStartupGuard()
    {
        const QString settingsFileName = QSettings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName()).fileName();
        const QFileInfo settingsFileInfo(settingsFileName);
        m_markerFileName = settingsFileInfo.absolutePath() + QStringLiteral("/startup-in-progress");

        const QFileInfo markerInfo(m_markerFileName);
        if (markerInfo.exists() && markerInfo.lastModified().secsTo(QDateTime::currentDateTime()) > 20 && settingsFileInfo.exists())
        {
            m_backupFileName = settingsFileName + QStringLiteral(".failed-start-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
            if (!QFile::rename(settingsFileName, m_backupFileName))
            {
                m_backupFileName.clear();
            }
        }

        QDir().mkpath(settingsFileInfo.absolutePath());
        QFile marker(m_markerFileName);
        if (marker.open(QFile::WriteOnly | QFile::Truncate))
        {
            marker.write(QString("%1 %2\n").arg(QCoreApplication::applicationPid()).arg(QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
        }
    }

    /// The window is open - the start succeeded
    void finished() { QFile::remove(m_markerFileName); }

    /// Settings of a failed start were set aside (the name of the backup), or empty
    const QString& getBackupFileName() const { return m_backupFileName; }

private:
    QString m_markerFileName;
    QString m_backupFileName;
};

int main(int argc, char *argv[])
{
#if defined(PDF4QT_USE_DBG_HEAP)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    // PDF Fire: the sound system for Read Aloud, before the application exists
    pdfviewer::PDFFireNaturalSpeech::prepareAudioBackend();

    QApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, true);
    QApplication application(argc, argv);

    // PDF Fire: the application identity. The settings and the certificate directories are
    // derived from the organization and application name. The desktop file name is the
    // application id on Wayland, it binds the window to the launcher and its icon.
    QCoreApplication::setOrganizationName("PDF Fire");
    QCoreApplication::setApplicationName("PDF Fire");
    QCoreApplication::setApplicationVersion(pdf::PDF_LIBRARY_VERSION);
    QApplication::setApplicationDisplayName(QApplication::translate("Application", "PDF Fire"));
    QGuiApplication::setDesktopFileName("pdf-fire");

    QCommandLineOption noDrm("no-drm", "Disable DRM settings of documents.");
    QCommandLineOption lightGui("theme-light", "Use a light theme for the GUI.");
    QCommandLineOption darkGui("theme-dark", "Use a dark theme for the GUI.");
    QCommandLineOption configPath = pdf::PDFSettings::getConfigPathOption();

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::applicationName());
    parser.addOption(noDrm);
    parser.addOption(lightGui);
    parser.addOption(darkGui);
    parser.addOption(configPath);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "The PDF file to open.");
    parser.process(application);
    pdf::PDFSettings::applyCommandLineSettingsPath(parser);

    // PDF Fire: before anything reads the settings
    PDFFireStartupGuard startupGuard;

    if (parser.isSet(noDrm))
    {
        pdf::PDFSecurityHandler::setNoDRMMode();
    }

    pdf::PDFApplicationTranslator translator;
    translator.loadSettings();
    translator.installTranslator();

    bool isLightGui = false;
    bool isDarkGui = false;
    const pdfviewer::PDFViewerSettings::ColorScheme colorScheme = pdfviewer::PDFViewerSettings::getColorSchemeStatic();
    switch (colorScheme)
    {
        case pdfviewer::PDFViewerSettings::AutoScheme:
            isLightGui = parser.isSet(lightGui);
            isDarkGui = parser.isSet(darkGui);
            break;

        case pdfviewer::PDFViewerSettings::LightScheme:
            isLightGui = true;
            break;

        case pdfviewer::PDFViewerSettings::DarkScheme:
            isDarkGui = true;
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    pdf::PDFWidgetUtils::setDarkTheme(isLightGui, isDarkGui);
    pdfviewer::PDFFireTheme::apply();

    QIcon appIcon(":/app-icon.svg");
    QApplication::setWindowIcon(appIcon);

    pdfviewer::PDFEditorMainWindow mainWindow;
    mainWindow.show();

    // PDF Fire: the start succeeded, when the window is open and responding for a while
    QTimer::singleShot(5000, &mainWindow, [&startupGuard]() { startupGuard.finished(); });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, [&startupGuard]() { startupGuard.finished(); });
    if (!startupGuard.getBackupFileName().isEmpty())
    {
        QTimer::singleShot(0, &mainWindow, [&mainWindow, &startupGuard]()
        {
            QMessageBox::information(&mainWindow, QApplication::applicationDisplayName(),
                                     QApplication::translate("Application", "PDF Fire did not finish starting the last time, so it started with its default settings now.\n\n"
                                                                            "Your previous settings were kept in:\n%1").arg(QDir::toNativeSeparators(startupGuard.getBackupFileName())));
        });
    }

    QStringList arguments = parser.positionalArguments();
    if (!arguments.isEmpty())
    {
        mainWindow.getProgramController()->openDocument(arguments.front());
    }

    // PDF Fire: development aid (scripted screenshots), see pdffiredevtools.cpp
    runPDFFireDeveloperTools(&mainWindow);

    return application.exec();
}
