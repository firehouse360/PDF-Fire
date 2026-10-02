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

// PDF Fire: development aid, not a feature. The editor can be driven by a small script
// and photographed without a display (QT_QPA_PLATFORM=offscreen), so a change of the user
// interface can be checked by looking at it:
//
//   PDFFIRE_SCREENSHOT=<file.png>      one picture of the window, then quit
//   PDFFIRE_SCREENSHOT_TAB=<index>     tab of the ribbon shown on that picture
//   PDFFIRE_SCRIPT="<step>;<step>;..." steps executed one after another, then quit. The opened
//                                      document MUST be inside the directory PDFFIRE_SCRIPT_SANDBOX
//                                      (a copy, never a real document of the user).
//       wait:<ms>                      wait
//       size:<w>x<h>                   resize the window
//       tab:<index>                    select a tab of the ribbon
//       action:<objectName>            trigger an action
//       click:<x>,<y>                  click the left mouse button (window coordinates)
//       rclick:<x>,<y>                 click the right mouse button
//       dclick:<x>,<y>                 double click the left mouse button
//       accept / reject                close the opened dialog by OK / Cancel
//       menu:<x>,<y>                   open the context menu
//       drag:<x1>,<y1>,<x2>,<y2>       press, move and release the left mouse button
//       key:<text>                     type a text
//       press:<key>                    press a key by its name (Return, Escape, Ctrl+Z...)
//       shot:<file.png>                save a picture of the window
//       wheel:<x>,<y>,<delta>          turn the mouse wheel
//       exit                           leave at once, without the question about saving

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QMenu>
#include <QSet>
#include <QLineEdit>
#include <QContextMenuEvent>
#include <QDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMainWindow>
#include <QMouseEvent>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QWheelEvent>

#include <cstdlib>

namespace
{

class DeveloperScript : public QObject
{
public:
    DeveloperScript(QMainWindow* window, QStringList steps) :
        QObject(window),
        m_window(window),
        m_steps(qMove(steps))
    {
    }

    void next()
    {
        if (m_steps.isEmpty())
        {
            QCoreApplication::quit();
            return;
        }

        const QString step = m_steps.takeFirst().trimmed();
        const QString command = step.section(QChar(':'), 0, 0);
        const QString argument = step.section(QChar(':'), 1);

        // The next step is scheduled before this one is executed: a step which opens a dialog
        // (or a menu) does not return until it is closed, and the following steps work with it.
        QTimer::singleShot(command == "wait" ? argument.toInt() : 250, this, [this]() { next(); });
        execute(command, argument, step);
    }

private:
    void execute(const QString& command, const QString& argument, const QString& step)
    {
        if (command == "wait")
        {
            // Only the delay of the next step
        }
        else if (command == "size")
        {
            m_window->resize(argument.section('x', 0, 0).toInt(), argument.section('x', 1, 1).toInt());
        }
        else if (command == "tab")
        {
            if (QTabBar* tabBar = m_window->findChild<QTabBar*>("ribbonTabBar"))
            {
                tabBar->setCurrentIndex(argument.toInt());
            }
        }
        else if (command == "action")
        {
            // The actions of the plugins are not children of the window, but they are its actions
            QAction* action = m_window->findChild<QAction*>(argument);
            for (QAction* windowAction : m_window->actions())
            {
                if (!action && windowAction->objectName() == argument)
                {
                    action = windowAction;
                }
            }

            if (action)
            {
                action->trigger();
            }
            else
            {
                qWarning("PDFFIRE_SCRIPT: action '%s' not found", qPrintable(argument));
            }
        }
        else if (command == "click" || command == "rclick")
        {
            const QPoint point = parsePoint(argument, 0);
            const Qt::MouseButton button = command == "click" ? Qt::LeftButton : Qt::RightButton;

            // The tools track the mouse (snapping to points), a real click is preceded by moves
            sendMouse(QEvent::MouseMove, point - QPoint(1, 1), Qt::NoButton, Qt::NoButton);
            sendMouse(QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
            sendMouse(QEvent::MouseButtonPress, point, button, button);
            sendMouse(QEvent::MouseButtonRelease, point, button, Qt::NoButton);
        }
        else if (command == "menu")
        {
            // menu:<x>,<y> - request of the context menu (a synthetic click does not create it)
            const QPoint point = parsePoint(argument, 0);
            if (QWidget* widget = m_window->childAt(point))
            {
                QContextMenuEvent event(QContextMenuEvent::Mouse, widget->mapFrom(m_window, point), m_window->mapToGlobal(point));
                QApplication::sendEvent(widget, &event);
            }
        }
        else if (command == "dclick")
        {
            const QPoint point = parsePoint(argument, 0);
            sendMouse(QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
            sendMouse(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
            sendMouse(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
            sendMouse(QEvent::MouseButtonDblClick, point, Qt::LeftButton, Qt::LeftButton);
            sendMouse(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
        }
        else if (command == "accept" || command == "reject")
        {
            // Closes the dialog opened by a previous step, as its OK (or Cancel) button does
            if (QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
            {
                command == "accept" ? dialog->accept() : dialog->reject();
            }
        }
        else if (command == "combo")
        {
            // combo:<item> - chooses the item in the first combo box of the dialog, which has it
            QWidget* window = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : static_cast<QWidget*>(m_window);
            for (QComboBox* comboBox : window->findChildren<QComboBox*>())
            {
                const int index = comboBox->findText(argument);
                if (index != -1)
                {
                    comboBox->setCurrentIndex(index);
                    break;
                }
            }
        }
        else if (command == "text")
        {
            // text:<objectName>=<value> - sets the text of a line edit in the dialog
            // opened by a previous step (e.g. text:certificatePasswordEdit=secret)
            const QString objectName = argument.section(QChar('='), 0, 0);
            const QString value = argument.section(QChar('='), 1);
            QWidget* window = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : static_cast<QWidget*>(m_window);
            QLineEdit* lineEdit = !objectName.isEmpty() ? window->findChild<QLineEdit*>(objectName) : nullptr;
            if (objectName.isEmpty())
            {
                // text:=<value> - the first visible line edit (a dialog with one unnamed edit)
                for (QLineEdit* candidate : window->findChildren<QLineEdit*>())
                {
                    if (candidate->isVisible() && candidate->isEnabled())
                    {
                        lineEdit = candidate;
                        break;
                    }
                }
            }
            if (lineEdit)
            {
                lineEdit->setText(value);
            }
        }
        else if (command == "select")
        {
            // select:<objectName>=<text> - selects the row with the text (first column) in a tree widget
            const QString objectName = argument.section(QChar('='), 0, 0);
            const QString value = argument.section(QChar('='), 1);
            QWidget* window = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : static_cast<QWidget*>(m_window);
            if (QTreeWidget* treeWidget = window->findChild<QTreeWidget*>(objectName))
            {
                const QList<QTreeWidgetItem*> items = treeWidget->findItems(value, Qt::MatchExactly);
                if (!items.isEmpty())
                {
                    treeWidget->setCurrentItem(items.front());
                }
            }
        }
        else if (command == "button")
        {
            // button:<text> - clicks the button with the text (without the "&" of its
            // shortcut) in the dialog opened by a previous step, e.g. button:Yes
            if (QWidget* dialog = QApplication::activeModalWidget())
            {
                for (QAbstractButton* button : dialog->findChildren<QAbstractButton*>())
                {
                    if (button->isVisible() && button->text().remove(QChar('&')).compare(argument, Qt::CaseInsensitive) == 0)
                    {
                        button->click();
                        break;
                    }
                }
            }
        }
        else if (command == "move")
        {
            // move:x,y - moves the mouse without a button
            sendMouse(QEvent::MouseMove, parsePoint(argument, 0), Qt::NoButton, Qt::NoButton);
        }
        else if (command == "drag")
        {
            const QPoint from = parsePoint(argument, 0);
            const QPoint to = parsePoint(argument, 2);
            sendMouse(QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
            sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
            for (int i = 1; i <= 8; ++i)
            {
                sendMouse(QEvent::MouseMove, from + (to - from) * i / 8, Qt::NoButton, Qt::LeftButton);
            }
            sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        }
        else if (command == "key")
        {
            // Without a window system (offscreen) no window is active, and the application
            // has no focus widget - the focus widget of the window is used then
            QWidget* focusWidget = QApplication::focusWidget();
            if (!focusWidget)
            {
                QWidget* window = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : static_cast<QWidget*>(m_window);
                focusWidget = window->focusWidget();
            }

            if (QWidget* widget = focusWidget)
            {
                if (qEnvironmentVariableIsSet("PDFFIRE_DEBUG_SCRIPT"))
                {
                    qInfo() << "PDFFIRE_SCRIPT key: focus widget" << widget->metaObject()->className() << widget->objectName();
                }
                for (const QChar character : argument)
                {
                    // The key code of the character too - some editors use it (letters,
                    // digits and the common ASCII characters have the key code of their code)
                    const int key = (character.unicode() >= 0x20 && character.unicode() < 0x7F) ? int(character.toUpper().unicode()) : 0;
                    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(character));
                    QApplication::sendEvent(widget, &press);
                    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, QString(character));
                    QApplication::sendEvent(widget, &release);
                }
            }
        }
        else if (command == "press")
        {
            // press:<key> - a key by its name (Return, Escape, Tab, Delete, Ctrl+Z...), sent to
            // the active window, which can be a dialog opened by a previous step
            const QKeyCombination combination = QKeySequence::fromString(argument)[0];
            QWidget* target = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : static_cast<QWidget*>(m_window);
            if (QWidget* focusWidget = target->focusWidget())
            {
                target = focusWidget;
            }
            QKeyEvent press(QEvent::KeyPress, combination.key(), combination.keyboardModifiers());
            QApplication::sendEvent(target, &press);
            QKeyEvent release(QEvent::KeyRelease, combination.key(), combination.keyboardModifiers());
            QApplication::sendEvent(target, &release);
        }
        else if (command == "shot")
        {
            // A dialog opened by a previous step is photographed instead of the window
            QWidget* target = m_window;
            if (QApplication::activePopupWidget())
            {
                target = QApplication::activePopupWidget();
            }
            else if (QApplication::activeModalWidget())
            {
                target = QApplication::activeModalWidget();
            }
            target->grab().save(argument);
        }
        else if (command == "wheel")
        {
            // wheel:<x>,<y>,<delta> - delta is in the eighths of a degree, as in QWheelEvent
            const QPoint point = parsePoint(argument, 0);
            QWidget* widget = m_window->childAt(point);
            if (widget)
            {
                QWheelEvent event(widget->mapFrom(m_window, point), m_window->mapToGlobal(point), QPoint(), QPoint(0, argument.section(',', 2, 2).toInt()),
                                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(widget, &event);
            }
        }
        else if (command == "widget")
        {
            // widget:<objectName> - clicks the button with the object name in the window
            // (sidebar buttons, which are not actions), e.g. widget:speechPlayButton
            if (QAbstractButton* button = m_window->findChild<QAbstractButton*>(argument))
            {
                button->click();
            }
            else
            {
                qWarning("PDFFIRE_SCRIPT: button '%s' not found", qPrintable(argument));
            }
        }
        else if (command == "dialogtab")
        {
            // dialogtab:<index> - shows the page of the first tab widget of the dialog
            if (QWidget* dialog = QApplication::activeModalWidget())
            {
                if (QTabWidget* tabWidget = dialog->findChild<QTabWidget*>())
                {
                    tabWidget->setCurrentIndex(argument.toInt());
                }
            }
        }
        else if (command == "listactions")
        {
            // Prints the names of all actions of the window (for the crash sweep)
            QSet<QString> names;
            QList<QAction*> actions = m_window->findChildren<QAction*>();
            actions += m_window->actions();
            for (QAction* action : actions)
            {
                if (!action->objectName().isEmpty() && !action->menu() && !names.contains(action->objectName()))
                {
                    names.insert(action->objectName());
                    qInfo().noquote() << "PDFFIRE-ACTION" << action->objectName() << (action->isEnabled() ? "enabled" : "disabled") << action->text().remove(QChar('&'));
                }
            }
        }
        else if (command == "exit")
        {
            // Leaves immediately, without the question about the unsaved document
            std::_Exit(0);
        }
        else if (!command.isEmpty())
        {
            qWarning("PDFFIRE_SCRIPT: unknown step '%s'", qPrintable(step));
        }
    }

    static QPoint parsePoint(const QString& argument, int firstIndex)
    {
        return QPoint(argument.section(',', firstIndex, firstIndex).toInt(), argument.section(',', firstIndex + 1, firstIndex + 1).toInt());
    }

    void sendMouse(QEvent::Type type, QPoint windowPoint, Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        QWidget* widget = m_window->childAt(windowPoint);
        if (!widget)
        {
            widget = m_window;
        }

        // A click gives the keyboard focus to the widget, as a real click does (the
        // focus is given by the window system path, which a sent event does not take)
        if (type == QEvent::MouseButtonPress)
        {
            for (QWidget* focusCandidate = widget; focusCandidate; focusCandidate = focusCandidate->parentWidget())
            {
                if (focusCandidate->focusPolicy() & Qt::ClickFocus)
                {
                    focusCandidate->setFocus(Qt::MouseFocusReason);
                    break;
                }
            }
        }

        const QPointF localPoint = widget->mapFrom(m_window, windowPoint);
        QMouseEvent event(type, localPoint, m_window->mapToGlobal(windowPoint), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(widget, &event);
    }

    QMainWindow* m_window;
    QStringList m_steps;
};

}   // namespace

void runPDFFireDeveloperTools(QMainWindow* window)
{
    const QString screenshotFileName = qEnvironmentVariable("PDFFIRE_SCREENSHOT");
    QString script = qEnvironmentVariable("PDFFIRE_SCRIPT");

    if (!screenshotFileName.isEmpty())
    {
        script = QString("size:1500x900;tab:%1;wait:4000;shot:%2").arg(qEnvironmentVariableIntValue("PDFFIRE_SCREENSHOT_TAB")).arg(screenshotFileName);
    }

    if (!script.isEmpty())
    {
        // SAFETY: a script clicks through the application, and the application saves what it is
        // told to save. On 2026-09-30 a test script answered the "save changes?" question of a
        // real document of the user, and overwrote it. A script therefore runs only on files in
        // a sandbox directory named by PDFFIRE_SCRIPT_SANDBOX - the tests work on copies.
        const QString sandbox = QFileInfo(qEnvironmentVariable("PDFFIRE_SCRIPT_SANDBOX")).canonicalFilePath();
        const QStringList arguments = QCoreApplication::arguments();
        for (qsizetype i = 1; i < arguments.size(); ++i)
        {
            const QFileInfo fileInfo(arguments[i]);
            if (!fileInfo.isFile())
            {
                continue;
            }

            const QString fileName = fileInfo.canonicalFilePath();
            if (sandbox.isEmpty() || !fileName.startsWith(sandbox + QChar('/')))
            {
                qCritical("PDFFIRE_SCRIPT refused: '%s' is not inside the sandbox directory PDFFIRE_SCRIPT_SANDBOX ('%s'). "
                          "Copy the document into the sandbox, and run the script on the copy.", qPrintable(fileName), qPrintable(sandbox));
                std::_Exit(3);
            }
        }

        DeveloperScript* developerScript = new DeveloperScript(window, script.split(QChar(';'), Qt::SkipEmptyParts));
        QTimer::singleShot(0, developerScript, [developerScript]() { developerScript->next(); });
    }
}
