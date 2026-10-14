// SPDX-License-Identifier: GPL-2.0-or-later
#include "inline_chat.hpp"
#include "diagnostics.hpp"
#include "panel.hpp"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace awl::chat;
static void check(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
static void spin(int milliseconds = 100) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
}
static void wait_for(const std::function<bool()> &done, int timeout = 4000) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeout)
        spin(1);
    check(done(), "Inline bridge test timed out");
}
static QUrl endpoint(const QString &script) {
    const auto match = QRegularExpression("ws://127\\.0\\.0\\.1:[0-9]+/[a-f0-9]{64}").match(script);
    check(match.hasMatch(), "Missing bounded local endpoint in injected resource");
    return QUrl(match.captured());
}
static void open(QWebSocket &socket, QUrl url, QByteArray origin = "https://www.twitch.tv") {
    QNetworkRequest request(url);
    request.setRawHeader("Origin", origin);
    socket.open(request);
}
static void request(QWebSocket &socket, QString id, QString text = "This is a message.") {
    socket.sendTextMessage(QString::fromUtf8(
        QJsonDocument(
            QJsonObject{{"type", "translate"},
                        {"id", id},
                        {"parts", QJsonArray{QJsonObject{{"text", text}, {"protected", false}}}}})
            .toJson(QJsonDocument::Compact)));
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QStringList logged;
    set_log_sink([&](LogLevel, const QString &event) {
        logged << event;
    });
    const auto logged_any = [&](const QString &needle) {
        return std::any_of(logged.begin(), logged.end(), [&](const QString &line) {
            return line.contains(needle);
        });
    };
    try {
        QString script;
        InlineChat bridge([&](const QString &value) {
            script = value;
            return true;
        });
        if (argc == 5 && QString::fromLocal8Bit(argv[1]) == "--model") {
            QTemporaryDir settings;
            Panel panel(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]),
                        settings.filePath("settings.ini"), nullptr, false,
                        QString::fromLocal8Bit(argv[4]), [&](const QString &value) {
                            script = value;
                            return true;
                        });
            panel.start();
            bool ready = false;
            QWebSocket client;
            QString translated;
            QString language;
            int responses = 0;
            QObject::connect(&client, &QWebSocket::textMessageReceived, [&](QString text) {
                auto reply = QJsonDocument::fromJson(text.toUtf8()).object();
                if (reply["type"] == "state")
                    ready = reply["ready"].toBool();
                if (reply["type"] == "result") {
                    ++responses;
                    translated = reply["text"].toString();
                    language = reply["source_language"].toString();
                }
            });
            open(client, endpoint(script));
            wait_for([&] {
                return ready;
            });
            panel.show();
            panel.hide(); // Closing settings must leave the active bridge/helper running.
            request(client, QUuid::createUuid().toString(QUuid::WithoutBraces));
            wait_for([&] {
                return !translated.isEmpty();
            });
            check(QRegularExpression("[А-Яа-я]").match(translated).hasMatch(),
                  "Real helper did not return Russian");
            check(language == "en", "Detected language did not reach the browser bridge");
            auto profiles = panel.findChild<QComboBox *>("awlChatResourceUsage");
            auto targets = panel.findChild<QComboBox *>("interpretwitchTargetLanguage");
            ready = false;
            profiles->setCurrentIndex(1);
            wait_for(
                [&] {
                    return ready;
                },
                30000);
            ready = false;
            targets->setCurrentIndex(targets->findData("en"));
            wait_for(
                [&] {
                    return ready;
                },
                30000);
            int previous = responses;
            request(client, QUuid::createUuid().toString(QUuid::WithoutBraces),
                    "Привет, как дела?");
            wait_for([&] {
                return responses > previous;
            });
            check(language == "ru" && !translated.isEmpty() &&
                      !QRegularExpression("[А-Яа-я]").match(translated).hasMatch(),
                  "Heavy English target did not reach the real helper");
            previous = responses;
            request(client, QUuid::createUuid().toString(QUuid::WithoutBraces), "Hello everyone!");
            wait_for([&] {
                return responses > previous;
            });
            check(translated.isEmpty(), "Already-English chat was retranslated");
            ready = false;
            profiles->setCurrentIndex(0);
            wait_for(
                [&] {
                    return ready;
                },
                30000);
            ready = false;
            targets->setCurrentIndex(targets->findData("fr"));
            wait_for(
                [&] {
                    return ready;
                },
                30000);
            previous = responses;
            request(client, QUuid::createUuid().toString(QUuid::WithoutBraces), "Hello everyone!");
            wait_for([&] {
                return responses > previous;
            });
            check(language == "en" && translated.startsWith("Bonjour"),
                  "Light French model selection did not reach the helper");
            panel.findChild<QCheckBox *>("interpretwitchEnabled")->setChecked(false);
            wait_for([&] {
                return client.state() == QAbstractSocket::UnconnectedState;
            });
            std::cout << "Default startup → settings closed → isolated CPU translation + language "
                         "→ Heavy English → Light French → disable passed.\n";
            return 0;
        }
        // A real local socket + real Chromium DOM, with deterministic delayed
        // translations. No Twitch requests, credentials or model downloads.
        if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--fixture") {
            bridge.message = [&](Message m) {
                if (m.original == "Hold for timeout")
                    return;
                QTimer::singleShot(180, &bridge, [&, m] {
                    if (m.original == "Уже на русском") {
                        bridge.deliver(m.id, {});
                        return;
                    }
                    const QString text = m.original == "Hello from chat" ? "Всем привет!"
                                         : m.original == "This is another message in English."
                                             ? "Это ещё одно сообщение на английском."
                                             : "Перевод: " + m.original;
                    bridge.deliver(
                        m.id, {text, true, m.original == "Bonjour tout le monde" ? "fr" : "en"});
                });
            };
            bridge.set_ready(true);
            check(bridge.start(), "Fixture listener failed");
            QFile file(QString::fromLocal8Bit(argv[2]));
            check(file.open(QIODevice::WriteOnly), "Fixture script output failed");
            file.write(script.toUtf8());
            file.close();
            QTimer::singleShot(60000, &app, &QApplication::quit);
            return app.exec();
        }
        check(bridge.start(), "Loopback listener failed");
        const auto url = endpoint(script);
        int offered = 0, reset = 0;
        QString work_id;
        bridge.message = [&](Message m) {
            ++offered;
            work_id = m.id;
        };
        bridge.reset = [&] {
            ++reset;
        };
        bridge.set_ready(true);
        for (bool bad_origin : {true, false}) {
            QWebSocket rejected;
            auto address = url;
            if (!bad_origin)
                address.setPath("/wrong-token");
            open(rejected, address,
                 bad_origin ? "https://unrelated.example" : "https://www.twitch.tv");
            spin(120);
            check(rejected.state() == QAbstractSocket::UnconnectedState,
                  "Unauthorized page accepted");
        }
        QWebSocket client;
        QList<QJsonObject> replies;
        QObject::connect(&client, &QWebSocket::textMessageReceived, [&](QString text) {
            replies.append(QJsonDocument::fromJson(text.toUtf8()).object());
        });
        open(client, url);
        wait_for([&] {
            return replies.size() >= 2;
        });
        check(replies.front()["ready"].toBool(), "Ready state missing");
        check(replies.back()["type"] == "appearance" && replies.back()["show_original"].toBool(),
              "Original messages must be shown by default on connection");
        bridge.set_appearance(false, false, false);
        wait_for([&] {
            return replies.back()["type"] == "appearance" && !replies.back()["background"].toBool();
        });
        check(!replies.back()["language_label"].toBool(), "Language label preference missing");
        check(replies.back().contains("show_original") && !replies.back()["show_original"].toBool(),
              "Show original preference missing");
        QWebSocket second;
        open(second, url);
        spin(100);
        check(second.state() == QAbstractSocket::UnconnectedState, "Accepted a second browser");
        const auto page_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        request(client, page_id);
        wait_for([&] {
            return offered == 1;
        });
        check(work_id != page_id, "Worker IDs must be independent of page IDs");
        bridge.deliver(work_id, {"Это сообщение.", true, "en"});
        wait_for([&] {
            return replies.back()["type"] == "result";
        });
        check(replies.back()["id"] == page_id && replies.back()["text"] == "Это сообщение." &&
                  replies.back()["source_language"] == "en",
              "Translation routing failed");
        for (int i = 0; i < 25; ++i)
            request(client, QUuid::createUuid().toString(QUuid::WithoutBraces));
        wait_for([&] {
            return offered == 25;
        });
        spin();
        check(offered == 25 && replies.back()["text"] == "", "Pending translation bound failed");
        bridge.set_ready(false);
        bridge.set_ready(true);
        request(client, QUuid::createUuid().toString(QUuid::WithoutBraces));
        wait_for([&] {
            return offered == 26;
        });
        // The page's stop reason is logged and does not itself drop the page.
        client.sendTextMessage(
            R"({"type":"stopping","reason":"timeout","pending":3,"detail":"x"})");
        wait_for([&] {
            return logged_any("no reply for 20 s (page waiting for 3");
        });
        spin();
        check(client.state() == QAbstractSocket::ConnectedState, "Stop report dropped the page");
        check(logged_any("origin https://unrelated.example") && logged_any("wrong capability") &&
                  logged_any("a page is already connected"),
              "Rejected connections were not logged");
        client.abort();
        wait_for([&] {
            return reset == 1;
        });
        replies.clear();
        open(client, url);
        wait_for([&] {
            return !replies.isEmpty();
        });
        bridge.deliver(work_id, {"Stale translation", true});
        spin();
        check(replies.size() == 2, "Old page result reached new page");
        bridge.set_ready(false);
        request(client, QUuid::createUuid().toString(QUuid::WithoutBraces));
        spin();
        check(offered == 26, "Paused bridge submitted inference");
        bridge.set_ready(true);
        client.sendTextMessage("{bad json");
        wait_for([&] {
            return client.state() == QAbstractSocket::UnconnectedState;
        });
        open(client, url);
        wait_for([&] {
            return client.state() == QAbstractSocket::ConnectedState;
        });
        client.sendTextMessage(QString(65537, 'x'));
        wait_for([&] {
            return client.state() == QAbstractSocket::UnconnectedState;
        });
        // State-only diagnostics: no capability, chat text or translation.
        check(logged_any("Twitch page connected") && logged_any("Twitch page disconnected") &&
                  !logged_any(url.path().mid(1)) && !logged_any("This is a message") &&
                  !logged_any("Это сообщение") && !logged_any("Stale translation"),
              "Bridge diagnostics missing or include private content");
        bridge.stop();
        check(!bridge.active() && script.contains("?.stop()"), "Disable failed to detach page");
        check(bridge.start() && endpoint(script) != url, "Enable reused capability");
        bridge.stop();

        {
            InlineChat unavailable([](const QString &) -> bool {
                throw std::runtime_error("Simulated browser interface error");
            });
            check(unavailable.start(), "Could not start browser-failure fixture");
            unavailable.stop();
            check(!unavailable.active(), "Browser exception prevented bridge cleanup");
        }

        if (argc == 2) {
            QTemporaryDir cache;
            Panel recovery("crash-always", cache.path(), cache.filePath("settings.ini"), nullptr,
                           false, QString::fromLocal8Bit(argv[1]), [&](const QString &value) {
                               script = value;
                               return true;
                           });
            recovery.start();
            QWebSocket page;
            int loaded = 0;
            bool available = false;
            QString health;
            QObject::connect(&page, &QWebSocket::textMessageReceived, [&](QString text) {
                const auto value = QJsonDocument::fromJson(text.toUtf8()).object();
                if (value["type"] == "state") {
                    available = value["ready"].toBool();
                    health = value["health"].toString();
                    if (available)
                        ++loaded;
                }
            });
            const auto address = endpoint(script);
            open(page, address);
            wait_for([&] {
                return loaded == 1;
            });
            request(page, QUuid::createUuid().toString(QUuid::WithoutBraces));
            wait_for([&] {
                return !available && health == "Recovering";
            });
            wait_for([&] {
                return loaded == 2;
            });
            request(page, QUuid::createUuid().toString(QUuid::WithoutBraces));
            wait_for([&] {
                return !available && health == "Unavailable";
            });
            check(page.state() == QAbstractSocket::ConnectedState,
                  "Worker failure disconnected the page");
            page.abort();
            spin();
            open(page, address);
            spin(700);
            QFile starts(cache.filePath("starts"));
            check(starts.open(QIODevice::ReadOnly) && starts.readAll().toInt() == 2 && !available &&
                      loaded == 2,
                  "Page reconnect bypassed the helper's failure limit");
        }
#ifdef _WIN32
        if (argc == 2) {
            // GPU selected without cuBLAS: CPU translation, Download GPU support,
            // then a GPU start that requested the pack download.
            QTemporaryDir cache;
            {
                QSettings saved(cache.filePath("settings.ini"), QSettings::IniFormat);
                saved.setValue("run_on", "gpu");
            }
            Panel gpu("gpu-libraries", cache.path(), cache.filePath("settings.ini"), nullptr,
                      false, QString::fromLocal8Bit(argv[1]), [&](const QString &value) {
                          script = value;
                          return true;
                      });
            gpu.start();
            QWebSocket page;
            bool available = false;
            QObject::connect(&page, &QWebSocket::textMessageReceived, [&](QString text) {
                const auto value = QJsonDocument::fromJson(text.toUtf8()).object();
                if (value["type"] == "state")
                    available = value["ready"].toBool();
            });
            open(page, endpoint(script));
            auto button = gpu.findChild<QPushButton *>("interpretwitchGpuDownload");
            auto notice = gpu.findChild<QLabel *>("interpretwitchGpuNotice");
            auto help = gpu.findChild<QLabel *>("interpretwitchDeviceHelp");
            wait_for([&] {
                return available && !button->isHidden();
            });
            check(!notice->isHidden() && notice->text().contains("docs.nvidia.com/cuda/eula") &&
                      help->text().contains("one-time download") && button->isEnabled(),
                  "Missing cuBLAS did not offer the GPU support download");
            available = false;
            button->click();
            wait_for([&] {
                return available && QFile::exists(QDir(cache.path()).filePath("gpu-download"));
            });
            check(button->isHidden() && notice->isHidden() &&
                      gpu.findChild<QLabel *>("awlChatModelStatus")->text().contains("GPU"),
                  "GPU support download did not finish on GPU");
            page.abort();
        }
#endif

        QTemporaryDir temp;
        Panel panel("missing-model", temp.path(), temp.filePath("settings.ini"), nullptr, true, {},
                    [](const QString &) {
                        return true;
                    });
        auto toggle = panel.findChild<QCheckBox *>("interpretwitchEnabled");
        check(toggle && toggle->isChecked(), "Bridge must be enabled by default");
        check(!panel.findChild<QPushButton *>("awlChatConnect") &&
                  !panel.findChild<QWidget *>("awlTwitchSetup"),
              "Standalone auth remains");
        panel.start();
        toggle->setChecked(false);
        check(panel.findChild<QLabel *>("interpretwitchConnection")->text().contains("disabled"),
              "Disable status missing");
        std::cout << "Inline bridge: origin/capability, one client, routing, stale replies, pause, "
                     "invalid input, shutdown and no-auth UI passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
