// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel.hpp"
#include "ui_text.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <iostream>
#include <stdexcept>

using namespace awl::chat;
// Capture explicit browser actions without opening a browser or making requests.
class BrowserTarget : public QObject {
    Q_OBJECT
  public:
    QList<QUrl> visits;
  public slots:
    void open(const QUrl &url) {
        visits.append(url);
    }
};
static void check(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
static void wait_for(const std::function<bool()> &done) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    check(done(), "Localisation worker check timed out");
}
static QStringList context_actions(QWidget *widget) {
    QStringList names;
    QTimer::singleShot(0, [&] {
        if (auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            for (auto action : menu->actions())
                if (!action->isSeparator())
                    names.append(action->text());
            menu->close();
        }
    });
    QMetaObject::invokeMethod(widget, "customContextMenuRequested", Qt::DirectConnection,
                              Q_ARG(QPoint, QPoint(10, 10)));
    return names;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc != 2)
        return 2;
    BrowserTarget browser;
    QDesktopServices::setUrlHandler("https", &browser, "open");
    try {
        for (const auto &locale : {"ru-RU", "RU_ru", "ru"}) {
            set_ui_locale(locale);
            check(ui_locale() == "ru-RU" && ui_text("Settings") == "Настройки",
                  "Russian locale selection");
        }
        for (const auto &locale : {"en-US", "en-GB", "de-DE", "", "rubbish"}) {
            set_ui_locale(locale);
            check(ui_locale() == "en-US" && ui_text("Settings") == "Settings", "English fallback");
        }
        // The real compiled resource, not a separately loaded developer file.
        set_ui_locale("ru-RU");
        check(ui_text("Settings") == "Настройки", "Compiled catalogue missing");
        QFile catalogue(":/awl-chat/locale/ru-RU.json");
        check(catalogue.open(QIODevice::ReadOnly), "Catalogue could not open");
        const auto messages = QJsonDocument::fromJson(catalogue.readAll()).object();
        check(messages.size() > 100, "Incomplete compiled catalogue");
        for (auto it = messages.begin(); it != messages.end(); ++it)
            check(ui_text(it.key().toUtf8().constData()) == it.value().toString(),
                  "Russian entry missing");
        check(ui_error("/private/native/library/error", "GPU initialization failed. Using CPU.") ==
                  "Не удалось инициализировать GPU. Используется CPU.",
              "Unknown native error leaked into Russian UI");

        for (const auto &locale : {"en-US", "ru-RU"}) {
            set_ui_locale(locale);
            check(ui_text("Awl Interpretwitch") ==
                      (ui_locale() == "ru-RU" ? "Awl Интерпретвич" : "Awl Interpretwitch"),
                  "Localized product name");
            browser.visits.clear();
            QTemporaryDir temporary;
            const auto path = temporary.filePath("settings.ini");
            Panel panel("missing-catalogue", temporary.path(), path, nullptr, true);
            panel.resize(300, 680);
            panel.show();
            app.processEvents();
            auto setup = panel.findChild<TwitchSetup *>();
            check(setup && setup->isVisible(), "First run did not open setup");
            auto save = setup->findChild<QPushButton *>("awlSetupSave");
            auto cancel = setup->findChild<QPushButton *>("awlSetupCancel");
            auto client = setup->findChild<QLineEdit *>("awlSetupClientId");
            const auto content = setup->findChild<QWidget *>("awlSetupContent");
            const auto scroll = setup->findChild<QScrollArea *>();
            check(save && save->text() == ui_text("Save") && client->isVisible(),
                  "Client ID and Save must be available immediately");
            check(browser.visits.isEmpty(), "Setup opened a website without a click");
            int headings = 0;
            for (auto label : content->findChildren<QLabel *>()) {
                check(!label->isHidden(), "Setup instructions must share one visible page");
                if (label->text().size() > 2 && label->text()[0].isDigit() &&
                    label->text()[1] == '.')
                    ++headings;
            }
            check(headings == 4, "All four setup steps must be present together");
            check(!setup->findChild<QPushButton *>("awlSetupNext") &&
                      !setup->findChild<QPushButton *>("awlSetupBack"),
                  "Wizard navigation remains");
            const auto fields = setup->findChild<QLabel *>("awlSetupFields")->text().split('\n');
            check(fields.size() == 4, "Registration fields missing");
            for (const auto &field : fields)
                check(field.contains(": "), "Registration must use Field: Value format");
            check(fields[1].endsWith(": https://localhost") &&
                      fields[2].endsWith(": Chat Bot") && fields[3].endsWith(": Public"),
                  "Registration values changed");
            check(scroll->horizontalScrollBar()->maximum() == 0,
                  "Setup overflows a 300-pixel dock horizontally");
            check(scroll->verticalScrollBar()->value() == 0,
                  "New setup must start at the top of the guide");
            for (const auto name : {"awlSetupBrowser", "awlSetupApplications"})
                setup->findChild<QPushButton *>(name)->click();
            check(browser.visits == QList<QUrl>{QUrl("https://dev.twitch.tv/console/apps"),
                                               QUrl("https://dev.twitch.tv/console/apps")},
                  "Browser actions did not open the application console");
            setup->findChild<QPushButton *>("awlSetupCopyUrl")->click();
            check(app.clipboard()->text() == "https://localhost", "Wrong redirect URL copied");
            app.clipboard()->clear();
            setup->findChild<QPushButton *>("awlSetupExisting")->click();
            check(scroll->verticalScrollBar()->value() > 0 && client->hasFocus(),
                  "Existing-ID shortcut must scroll to the input");
            save->click();
            check(!setup->findChild<QLabel *>("awlSetupError")->text().isEmpty(),
                  "Empty Client ID accepted");
            client->setText("bad token value");
            save->click();
            check(setup->isVisible() &&
                      !QSettings(path, QSettings::IniFormat).contains("client_id"),
                  "Invalid draft persisted");
            client->setText(" testclient1234 ");
            client->selectAll();
            check(context_actions(client).contains(ui_text("Paste")),
                  "Edit context menu not localised");
            save->click();
            app.processEvents();
            check(!setup->isVisible(), "Save did not return to chat");
            QSettings saved(path, QSettings::IniFormat);
            check(saved.value("client_id") == "testclient1234", "Trimmed Client ID not saved");
            check(saved.value("connected") == false, "Saving setup started sign-in");
            check(panel.findChild<QPushButton *>("awlChatConnect")->text() ==
                      ui_text("Connect Twitch"),
                  "Connect label not localised");
            check(panel.findChild<QToolButton *>("awlChatConnectionToggle")->accessibleName() ==
                      ui_text("Show chat settings"),
                  "Accessibility label not localised");
            const auto view = panel.findChild<ChatView *>();
            check(view->accessibleName() == ui_text("Private translated Twitch chat"),
                  "Chat accessibility label");
            Event event;
            event.kind = EventKind::Message;
            event.message = {"1", "user", "Name", "This is the original"};
            panel.receive(event);
            view->model()->translated("1", {event.message.original, false, true});
            check(view->toPlainText().contains(ui_text("Original shown · translation unavailable")),
                  "Translation failure text");
            check(context_actions(view).contains(ui_text("Copy")),
                  "Chat context menu not localised");
            panel.findChild<QPushButton *>("awlChatSetup")->click();
            check(client->text() == "testclient1234", "Existing ID not restored");
            client->setText("discardme1234");
            cancel->click();
            saved.sync();
            check(saved.value("client_id") == "testclient1234", "Cancel overwrote saved ID");
            Panel restored("missing-catalogue", temporary.path(), path, nullptr, true);
            restored.show();
            app.processEvents();
            check(!restored.findChild<TwitchSetup *>()->isVisible(),
                  "Upgrade forced re-registration");

            // A real child has its own globals; this verifies locale propagation
            // across the IPC boundary and a model-file error from the helper.
            Worker worker(temporary.filePath("missing.json"), temporary.path(), nullptr, argv[1]);
            QString status;
            bool done = false;
            worker.status = [&](QString value) {
                status = value;
            };
            worker.ready = [&](bool ready, RunOn, QString) {
                check(!ready, "Missing model unexpectedly loaded");
                done = true;
            };
            worker.prepare(false);
            wait_for([&] {
                return done;
            });
            check(status == ui_text("Model catalogue is missing. Reinstall Awl Interpretwitch."),
                  "Child error did not follow OBS locale");
        }
        set_ui_locale("ru-RU");
        TwitchClient twitch;
        QString status;
        twitch.status = [&](QString value) {
            status = value;
        };
        twitch.start("invalid", "channel", false);
        check(status == "Завершите настройку Twitch, указав публичный Client ID.",
              "Twitch error not localised");
        std::cout << "English/Russian setup, saving/cancellation, menus, accessibility, locale "
                     "fallback and real helper errors passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#include "localisation_tests.moc"
