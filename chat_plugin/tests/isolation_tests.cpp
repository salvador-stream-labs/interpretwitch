// SPDX-License-Identifier: GPL-2.0-or-later
#include "worker.hpp"
#include "ipc.hpp"
#include "panel.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace awl::chat;
static void check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
static void wait_for(const std::function<bool()> &done, int timeout = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    check(done(), "Worker test timed out");
}
static int starts(const QString &directory) {
    QFile file(directory + "/starts");
    return file.open(QIODevice::ReadOnly) ? file.readAll().toInt() : 0;
}
static Message sample(QString id = "message") {
    Message message;
    message.id = id;
    message.original = "Hello everyone!";
    message.parts = protect_text(message.original);
    return message;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc != 3)
        return 2;
    const QString fixture = QString::fromUtf8(argv[1]), actual_helper = QString::fromUtf8(argv[2]);
    try {
        Worker::Limits limits{2000, 150, 2000, 10};
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(5);
        QObject::connect(&heartbeat, &QTimer::timeout, [&] {
            ++ticks;
        });
        heartbeat.start();
        for (const auto &mode : QStringList{"unavailable", "wrong-model"}) {
            QTemporaryDir cache;
            bool stopped = false;
            QStringList discarded;
            Worker worker(mode, cache.path(), nullptr, fixture, limits);
            worker.ready = [&](bool good, RunOn, QString) {
                check(!good, "Unavailable or mismatched model declared ready");
                stopped = true;
            };
            worker.discarded = [&](QString id) {
                discarded << id;
            };
            worker.prepare(false);
            worker.offer(sample("during-prepare"));
            wait_for([&] {
                return stopped;
            });
            check(discarded.contains("during-prepare"), "Failed preparation left a pending job");
        }
        for (const auto &mode :
             QStringList{"crash-once", "hang", "oversize", "bad-json", "wrong-id"}) {
            QTemporaryDir cache;
            int loaded = 0, failed = 0, translated = 0;
            QStringList discarded;
            QString status;
            Worker worker(mode, cache.path(), nullptr, fixture, limits);
            worker.status = [&](QString value) {
                status = value;
            };
            worker.ready = [&](bool good, RunOn, QString) {
                if (good)
                    ++loaded;
                else
                    ++failed;
            };
            worker.result = [&](Job, Translation value) {
                if (value.translated)
                    ++translated;
            };
            worker.discarded = [&](QString id) {
                discarded << id;
            };
            worker.prepare(false);
            wait_for([&] {
                return loaded == 1;
            });
            const auto before = ticks;
            worker.offer(sample());
            worker.offer(sample("queued"));
            wait_for([&] {
                return loaded == 2;
            });
            check(starts(cache.path()) == 2 && translated == 0,
                  "Failed message retried or recovery count wrong");
            check(discarded.contains("message") && discarded.contains("queued"),
                  "Worker failure left active or queued messages pending");
            check(ticks > before, "Parent event loop stopped during worker recovery");
            worker.offer(sample("after-restart"));
            if (mode == "crash-once")
                wait_for([&] {
                    return translated == 1;
                });
            else {
                wait_for([&] {
                    return failed == 1;
                });
                check(starts(cache.path()) == 2 && status.contains("Chat remains available"),
                      "Persistent failure created a restart loop");
            }
        }
        {
            QTemporaryDir cache;
            int loaded = 0, failures = 0;
            QString fallback;
            RunOn actual = RunOn::GPU;
            Worker worker("crash-always", cache.path(), nullptr, fixture, limits);
            worker.ready = [&](bool good, RunOn device, QString reason) {
                if (good) {
                    ++loaded;
                    actual = device;
                    fallback = reason;
                } else
                    ++failures;
            };
            worker.prepare(false, RunOn::GPU);
            wait_for([&] {
                return loaded == 1;
            });
            worker.offer(sample());
            wait_for([&] {
                return loaded == 2;
            });
            check(actual == RunOn::CPU && !fallback.isEmpty(),
                  "GPU process crash did not explicitly fall back to CPU");
            worker.offer(sample());
            wait_for([&] {
                return failures == 1;
            });
        }
        {
            QTemporaryDir cache;
            int loaded = 0, delivered = 0;
            Worker worker("late", cache.path(), nullptr, fixture, limits);
            QStringList discarded;
            worker.discarded = [&](QString id) {
                discarded << id;
            };
            worker.ready = [&](bool good, RunOn, QString) {
                if (good)
                    ++loaded;
            };
            worker.result = [&](Job job, Translation) {
                check(job.message.id == "new", "Cleared result escaped cancellation");
                ++delivered;
            };
            worker.prepare(false);
            wait_for([&] {
                return loaded == 1;
            });
            worker.offer(sample("old"));
            worker.offer(sample("old-queued"));
            worker.clear();
            check(discarded.contains("old") && discarded.contains("old-queued"),
                  "Cancellation left translations pending");
            worker.offer(sample("new"));
            wait_for([&] {
                return delivered == 1;
            });
        }
        {
            QTemporaryDir cache;
            bool stopped = false;
            Worker worker("load-hang", cache.path(), nullptr, fixture, limits);
            worker.ready = [&](bool good, RunOn, QString) {
                stopped = !good;
            };
            worker.prepare(true);
            wait_for([&] {
                return starts(cache.path()) == 1;
            });
            QElapsedTimer timer;
            timer.start();
            worker.cancel_download();
            wait_for([&] {
                return stopped && worker.findChild<QProcess *>()->state() == QProcess::NotRunning;
            });
            check(timer.elapsed() < 1000 && starts(cache.path()) == 1,
                  "Download cancel waited for native work or restarted");
        }
        {
            QTemporaryDir cache;
            QElapsedTimer timer;
            {
                Worker worker("load-hang", cache.path(), nullptr, fixture, limits);
                worker.prepare(false);
                wait_for([&] {
                    return starts(cache.path()) == 1;
                });
                timer.start();
            }
            check(timer.elapsed() < 1000, "Destroying worker waited for hung inference");
        }
        {
            QTemporaryDir cache;
            bool stopped = false;
            Worker worker("load-hang", cache.path(), nullptr, fixture, {150, 150, 300, 10});
            worker.ready = [&](bool good, RunOn, QString) {
                stopped = !good;
            };
            worker.prepare(false);
            wait_for([&] {
                return stopped;
            });
            check(starts(cache.path()) == 2,
                  "Stalled model loading did not stop after one recovery");
        }
        {
            QTemporaryDir cache;
            bool loaded = false;
            Worker worker("stderr", cache.path(), nullptr, fixture, limits);
            worker.ready = [&](bool good, RunOn, QString) {
                loaded = good;
            };
            worker.prepare(false);
            wait_for([&] {
                return loaded;
            });
            check(worker.findChild<QProcess *>()->readAllStandardError().isEmpty(),
                  "Native stderr accumulated in OBS");
        }
        {
            bool stopped = false;
            Worker worker("unused", "unused", nullptr, actual_helper + ".missing", limits);
            worker.ready = [&](bool good, RunOn, QString) {
                stopped = !good;
            };
            worker.prepare(false);
            wait_for([&] {
                return stopped;
            });
        }
#ifndef Q_OS_WIN
        {
            QTemporaryDir directory;
            const auto path = directory.filePath("invalid-worker");
            QFile invalid(path);
            check(invalid.open(QIODevice::WriteOnly), "Invalid helper fixture open");
            invalid.write("invalid executable");
            invalid.close();
            check(invalid.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                         QFileDevice::ExeOwner),
                  "Invalid helper permissions");
            bool stopped = false, changed = false;
            Worker worker("unused", directory.path(), nullptr, path, limits);
            worker.ready = [&](bool good, RunOn, QString) {
                stopped = !good;
            };
            QObject::connect(worker.findChild<QProcess *>(), &QProcess::stateChanged, &worker,
                             [&](QProcess::ProcessState state) {
                                 if (state == QProcess::Starting && !changed) {
                                     changed = true;
                                     worker.prepare(false);
                                 }
                             });
            worker.prepare(false);
            wait_for([&] {
                return stopped;
            });
            check(changed, "Failed-start device change was not exercised");
        }
#endif
        // The production helper monitors EOF on a separate reader thread.
        {
            QTemporaryDir cache;
            Panel panel("crash-always", cache.path(), cache.filePath("settings.ini"), nullptr, true,
                        fixture);
            QPushButton *start = nullptr;
            for (auto button : panel.findChildren<QPushButton *>())
                if (button->text() == "Download model")
                    start = button;
            check(start, "Missing model start control");
            const auto model_status = panel.findChild<QLabel *>("awlChatModelStatus");
            start->click();
            wait_for([&] {
                return model_status->text().startsWith("Local translation");
            });
            Event message;
            message.kind = EventKind::Message;
            message.message = sample("first");
            panel.receive(message);
            wait_for([&] {
                return starts(cache.path()) == 2 &&
                       model_status->text().startsWith("Local translation");
            });
            message.message = sample("second");
            panel.receive(message);
            wait_for([&] {
                return model_status->text().contains("Chat remains available");
            });
            message.message = sample("after-failure");
            panel.receive(message);
            const auto view = panel.findChild<ChatView *>();
            check(view && view->model()->rows.size() == 3 &&
                      view->toPlainText().contains("Hello everyone!"),
                  "Dock lost history or stopped displaying original chat after helper failures");
            for (const auto &row : view->model()->rows)
                check(!row.pending, "Dock left failed translation pending");
        }
        QProcess helper;
        helper.setStandardErrorFile(QProcess::nullDevice());
        helper.start(actual_helper, {"--stdio"});
        check(helper.waitForStarted(2000), "Real helper could not start");
        helper.closeWriteChannel();
        check(helper.waitForFinished(2000), "Parent pipe closure left an orphan helper");
        check(helper.exitCode() == 0, "Real helper failed on parent EOF");
        // Input size/role checks run on both sides without accepting chat metadata.
        auto invalid = ipc::message(sample());
        invalid["original"] = QString(2001, 'x');
        bool rejected = false;
        try {
            ipc::message(invalid);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Oversized helper input accepted");
        std::cout
            << "Process isolation passed: real child crashes, hangs, malformed/oversized/wrong-id "
               "replies, one recovery, CPU fallback, continued event loop and native dock/history, "
               "cancellation, bounded shutdown, missing helper, stderr flood and parent EOF.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
