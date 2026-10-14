// SPDX-License-Identifier: GPL-2.0-or-later
// Uses existing model files, never downloads. Optional --require-gpu is for a
// CUDA-enabled build on an NVIDIA host; Mac validates real CPU fallback only.
#include "worker.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace awl::chat;
static void check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
static void wait_for(const std::function<bool()> &done) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 15000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    check(done(), "Worker did not complete within 15 seconds");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::cerr << "usage: awl-chat-device-check MANIFEST MODEL_DIRECTORY [--require-gpu] "
                     "[--helper PATH] [--profiles]\n";
        return 2;
    }
    try {
        bool require_gpu = false;
        bool profiles = false;
        QString helper;
        for (int i = 3; i < argc; ++i) {
            const auto option = QString::fromUtf8(argv[i]);
            if (option == "--require-gpu")
                require_gpu = true;
            else if (option == "--profiles")
                profiles = true;
            else if (option == "--helper" && i + 1 < argc)
                helper = QString::fromUtf8(argv[++i]);
            else
                throw std::runtime_error("Unknown device-check option");
        }
        struct State {
            bool ready;
            RunOn device;
            QString fallback;
        };
        QVector<State> states;
        QVector<QString> delivered;
        QVector<Translation> translations;
        QStringList discarded;
        Worker worker(argv[1], argv[2], nullptr, helper);
        worker.state_changed = [&](Worker::State state, RunOn device, QString fallback) {
            if (state == Worker::State::Preparing || state == Worker::State::Recovering)
                return;
            const bool ready = state == Worker::State::Ready;
            states.push_back({ready, device, fallback});
        };
        worker.result = [&](Job job, Translation value) {
            delivered.push_back(job.message.id);
            translations.push_back(value);
        };
        worker.discarded = [&](QString id) {
            discarded << id;
        };
        Message message;
        message.id = "cpu";
        message.original = "Hello everyone!";
        message.parts = protect_text(message.original);
        worker.prepare(false, RunOn::CPU);
        wait_for([&] {
            return states.size() == 1;
        });
        check(states.back().ready && states.back().device == RunOn::CPU &&
                  states.back().fallback.isEmpty(),
              "CPU load failed");
        check(worker.findChild<QProcess *>()->processId() > 0 &&
                  worker.findChild<QProcess *>()->processId() != QCoreApplication::applicationPid(),
              "Inference did not start a separate process");
        worker.offer(message);
        wait_for([&] {
            return delivered.size() == 1;
        });
        check(translations.back().translated && !translations.back().text.isEmpty(),
              "CPU translation failed");
        message.id = "stale";
        worker.offer(message);
        worker.prepare(false, RunOn::GPU);
        wait_for([&] {
            return states.size() == 2;
        });
        check(states.back().ready, "GPU request left translation unavailable");
        check(!delivered.contains("stale"), "Old-device result escaped cancellation");
        if (require_gpu)
            check(states.back().device == RunOn::GPU && states.back().fallback.isEmpty(),
                  "Actual GPU required, but CPU fallback occurred");
#ifdef __APPLE__
        check(states.back().device == RunOn::CPU && !states.back().fallback.isEmpty(),
              "CPU-only build did not explicitly report GPU fallback");
#endif
        std::cout << "GPU request actually loaded "
                  << run_on_name(states.back().device).toStdString()
                  << (states.back().fallback.isEmpty()
                          ? ".\n"
                          : " with an explicit GPU-startup warning.\n");
        message.id = "after-switch";
        worker.offer(message);
        wait_for([&] {
            return delivered.size() == 2;
        });
        check(translations.back().translated, "Translation did not resume after device request");
        // Do not process UI events until all requests are submitted: obsolete
        // ready/status callbacks must not override the final requested device.
        for (int i = 0; i < 5; ++i) {
            worker.prepare(false, RunOn::GPU);
            worker.prepare(false, RunOn::CPU);
        }
        wait_for([&] {
            return states.size() >= 3;
        });
        QCoreApplication::processEvents();
        check(states.size() == 3 && states.back().ready && states.back().device == RunOn::CPU &&
                  states.back().fallback.isEmpty(),
              "Rapid switches delivered obsolete readiness or wrong device");
        message.id = "final-cpu";
        worker.offer(message);
        wait_for([&] {
            return delivered.size() == 3;
        });
        check(translations.back().translated, "CPU translation failed after switching back");
        if (profiles) {
            for (const auto profile : {ModelProfile::Heavy, ModelProfile::Light}) {
                const auto count = states.size();
                const auto received = delivered.size();
                const auto old_pid = worker.findChild<QProcess *>()->processId();
                message.id = "old-profile";
                worker.offer(message);
                // Heavy also exercises startup on the requested GPU/CPU fallback.
                worker.prepare(false, profile == ModelProfile::Heavy ? RunOn::GPU : RunOn::CPU,
                               profile);
                wait_for([&] {
                    return states.size() > count;
                });
                check(states.size() == count + 1 && states.back().ready &&
                          worker.findChild<QProcess *>()->processId() != old_pid &&
                          !delivered.contains("old-profile") && discarded.contains("old-profile"),
                      "Model switch failed, reused old process or left stale result pending");
                if (require_gpu && profile == ModelProfile::Heavy)
                    check(states.back().device == RunOn::GPU && states.back().fallback.isEmpty(),
                          "Heavy required GPU but fell back to CPU");
#ifdef __APPLE__
                if (profile == ModelProfile::Heavy)
                    check(states.back().device == RunOn::CPU && !states.back().fallback.isEmpty(),
                          "Heavy GPU request did not explicitly fall back on Mac");
#endif
                message.id = model_id(profile);
                message.original = "Bonjour tout le monde";
                message.parts = protect_text(message.original);
                worker.offer(message);
                wait_for([&] {
                    return delivered.size() > received;
                });
                check(translations.back().translated == (profile == ModelProfile::Heavy),
                      "Selected profile did not control multilingual translation");
            }
            std::cout
                << "Light → Heavy → Light: distinct processes, verified cached models, "
                   "multilingual routing, pending cancellation and Heavy device fallback passed.\n";
        }
        bool missing_done = false, missing_ready = true;
        Worker missing("missing-model-catalogue", argv[2], nullptr, helper);
        missing.state_changed = [&](Worker::State state, RunOn, QString) {
            if (state == Worker::State::Preparing || state == Worker::State::Recovering)
                return;
            const bool ready = state == Worker::State::Ready;
            missing_ready = ready;
            missing_done = true;
        };
        missing.prepare(false, RunOn::GPU);
        wait_for([&] {
            return missing_done;
        });
        check(!missing_ready, "GPU fallback concealed missing model files");
        std::cout << "Device checks passed: real CPU inference, startup fallback/actual device, "
                     "stale-result cancellation, rapid switches, continued translation and "
                     "missing-model failure.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
