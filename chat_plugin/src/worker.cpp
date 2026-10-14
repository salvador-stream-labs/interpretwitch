// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui_text.hpp"
#include "diagnostics.hpp"
#include "limits.hpp"
#include "worker.hpp"
#include "ipc.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStringList>
#include <algorithm>
#include <chrono>
#include <utility>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace awl::chat {
static qint64 now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
Worker::Worker(QString manifest, QString directory, QObject *parent, QString helper)
    : Worker(std::move(manifest), std::move(directory), parent, std::move(helper), Limits{}) {}

Worker::Worker(QString manifest, QString directory, QObject *parent, QString helper, Limits limits)
    : QObject(parent), manifest_(std::move(manifest)), directory_(std::move(directory)),
      helper_(std::move(helper)), process_(this), limits_(limits) {
    if (helper_.isEmpty()) {
        helper_ = QDir(QCoreApplication::applicationDirPath()).filePath("awl-chat-worker");
#ifdef Q_OS_WIN
        helper_ += ".exe";
#endif
    }
    deadline_.setSingleShot(true);
    restart_.setSingleShot(true);
    process_.setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
#ifdef Q_OS_UNIX
    process_.setUnixProcessParameters(QProcess::UnixProcessFlag::CloseFileDescriptors |
                                      QProcess::UnixProcessFlag::ResetIds |
                                      QProcess::UnixProcessFlag::DisableCoreDumps);
#endif
    connect(&restart_, &QTimer::timeout, this, &Worker::launch);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        log_event(LogLevel::Warning,
                  QString("Translation worker missed its deadline while %1")
                      .arg(phase_ == Phase::Preparing     ? "preparing the model"
                           : phase_ == Phase::Translating ? "translating"
                           : phase_ == Phase::Clearing    ? "clearing its cache"
                                                          : "idle"));
        fail(ui_text("Translation worker stopped responding."));
    });
    connect(&process_, &QProcess::started, this, [this] {
        if (stopping_) {
            process_.kill();
            return;
        }
        log_event(LogLevel::Info,
                  QString("Translation worker started (process %1)").arg(process_.processId()));
        QJsonObject request{
            {"type", "prepare"},
            {"manifest",
             QFileInfo(profile_manifest(manifest_, profile_, target_)).absoluteFilePath()},
            {"directory",
             QFileInfo(profile_directory(directory_, profile_, target_)).absoluteFilePath()},
            {"model_id", model_id(profile_, target_)},
            {"target_language", target_},
            {"download", download_},
            {"locale", ui_locale()},
            {"device", launch_device_ == RunOn::GPU ? "gpu" : "cpu"}};
        if (!gpu_manifest_.isEmpty() && !gpu_directory_.isEmpty()) {
            request["gpu_manifest"] = QFileInfo(gpu_manifest_).absoluteFilePath();
            request["gpu_directory"] = QFileInfo(gpu_directory_).absoluteFilePath();
            request["gpu_download"] = gpu_download_ && launch_device_ == RunOn::GPU;
        }
        // The optional GPU pack is about 550 MB; allow slower connections.
        send(request, gpu_download_ && launch_device_ == RunOn::GPU
                          ? std::max(limits_.gpu_download_ms, limits_.download_ms)
                      : download_ ? limits_.download_ms
                                  : limits_.startup_ms);
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, &Worker::read);
    connect(&process_, &QProcess::errorOccurred, this, [this](auto error) {
        if (stopping_) {
            // FailedToStart has no finished signal. Complete a cancelled launch
            // here so a rapid device change cannot leave the supervisor stuck.
            if (error == QProcess::FailedToStart) {
                stopping_ = false;
                if (wanted_)
                    restart_.start(0);
            }
            return;
        }
        if (error != QProcess::Crashed)
            log_event(LogLevel::Warning,
                      "Translation worker process error: " + process_.errorString());
        if (error == QProcess::FailedToStart)
            fail(ui_text("Translation worker could not start. Reinstall Interpretwitch."), false);
        else if (error == QProcess::ReadError || error == QProcess::WriteError)
            fail(ui_text("Translation worker connection failed."));
        // Crashed is followed by finished; handle that once below.
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        deadline_.stop();
        input_.clear();
        if (stopping_) {
            stopping_ = false;
            if (wanted_)
                restart_.start(failures_ ? limits_.restart_ms : 0);
        } else if (wanted_) {
            log_event(LogLevel::Warning,
                      QString("Translation worker exited unexpectedly (%1, exit code 0x%2)")
                          .arg(exit == QProcess::CrashExit ? "crashed" : "normal exit")
                          .arg(quint32(code), 0, 16));
            fail(ui_text("Translation worker exited unexpectedly."));
        }
    });
}

Worker::~Worker() {
    wanted_ = false;
    restart_.stop();
    deadline_.stop();
    process_.disconnect(this);
    process_.kill();
    // No inference thread is joined inside OBS. The OS terminates the helper.
    if (process_.state() != QProcess::NotRunning)
        process_.waitForFinished(1000);
}
void Worker::stop() {
    deadline_.stop();
    restart_.stop();
    phase_ = Phase::Stopped;
    input_.clear();
    active_.reset();
    cancel_sent_ = false;
    stopping_ = process_.state() != QProcess::NotRunning;
    if (stopping_)
        process_.kill();
    else if (wanted_)
        restart_.start(failures_ ? limits_.restart_ms : 0);
}
void Worker::set_gpu_support(QString manifest, QString directory) {
    gpu_manifest_ = std::move(manifest);
    gpu_directory_ = std::move(directory);
}
void Worker::prepare(bool download, RunOn device, ModelProfile profile, QString target,
                     bool gpu_download) {
    gpu_download_ = gpu_download && device == RunOn::GPU;
    gpu_issue_.clear();
    gpu_detail_.clear();
    if (!supports_target(profile, target)) {
        shutdown();
        if (status)
            status(ui_text("Unsupported target language."));
        return;
    }
    target_ = std::move(target);
    ++epoch_;
    discard_waiting();
    clear_cache_ = false;
    requested_ = launch_device_ = device;
    profile_ = profile;
    download_ = download;
    wanted_ = true;
    failures_ = 0;
    recovery_reason_.clear();
    stop();
    if (state_changed)
        state_changed(State::Preparing, requested_, {});
}
void Worker::shutdown() {
    wanted_ = false;
    ++epoch_;
    discard_waiting();
    stop();
    if (state_changed)
        state_changed(State::Stopped, actual_, {});
}
void Worker::cancel_download() {
    shutdown();
    if (status)
        status(ui_text("Download cancelled."));
}
void Worker::launch() {
    if (!wanted_ || stopping_ || process_.state() != QProcess::NotRunning)
        return;
    const QFileInfo helper(helper_);
    if (!helper.isAbsolute() || !helper.isFile() || helper.isSymLink() || !helper.isExecutable()) {
        fail(ui_text("Translation worker is missing. Reinstall Interpretwitch."), false);
        return;
    }
    QProcessEnvironment environment;
    const auto inherited = QProcessEnvironment::systemEnvironment();
    // Pass OS/runtime essentials, not the host's entire environment. No Twitch
    // credentials or separate channel/user metadata are sent to the helper.
    for (const auto &name : {"PATH", "SystemRoot", "WINDIR", "TEMP", "TMP", "TMPDIR", "HOME",
                             "USERPROFILE", "LOCALAPPDATA", "APPDATA", "LANG", "LC_ALL"})
        if (inherited.contains(name))
            environment.insert(name, inherited.value(name));
#ifdef Q_OS_WIN
    // The helper uses OBS's matching QtCore DLL without replacing/copying it.
    QString path = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    // GPU mode loads CUDA 12 cuBLAS on demand. NVIDIA's installer records each
    // toolkit as CUDA_PATH_V12_<minor>, which is not on PATH for a cuBLAS-only install.
    for (const auto &name : inherited.keys())
        if (name.startsWith("CUDA_PATH_V12_", Qt::CaseInsensitive))
            path += ";" + QDir::toNativeSeparators(QDir(inherited.value(name)).filePath("bin"));
    environment.insert("PATH", path + ";" + environment.value("PATH"));
#endif
    log_event(LogLevel::Info, QString("Starting translation worker: %1 → %2 on %3%4")
                                  .arg(model_id(profile_, target_), target_,
                                       QString(launch_device_ == RunOn::GPU ? "GPU" : "CPU"),
                                       QString(download_ ? " (download allowed)" : "")));
    process_.setProcessEnvironment(environment);
    process_.setWorkingDirectory(helper.absolutePath());
    phase_ = Phase::Preparing;
    status_updates_ = 0;
    input_.clear();
    if (process_.isOpen())
        process_.readAllStandardOutput();
    deadline_.start(limits_.startup_ms);
    process_.start(helper.absoluteFilePath(), {"--stdio"});
}
void Worker::send(QJsonObject value, int timeout) {
    request_id_ = QString::number(++sequence_);
    value["id"] = request_id_;
    try {
        const auto bytes = ipc::encode(value);
        if (process_.bytesToWrite() + bytes.size() > ipc::frame_limit ||
            process_.write(bytes) != bytes.size())
            throw std::runtime_error("Worker pipe is full.");
        deadline_.start(std::max(1, timeout));
    } catch (...) {
        fail(ui_text("Translation worker connection failed."));
    }
}
void Worker::read() {
    if (stopping_ || !wanted_) {
        process_.readAllStandardOutput();
        return;
    }
    if (process_.bytesAvailable() > 2 * ipc::frame_limit) {
        fail(ui_text("Translation worker response was too large."));
        return;
    }
    while (process_.bytesAvailable()) {
        const auto bytes = process_.read(4096);
        for (const char byte : bytes) {
            if (byte == '\n') {
                const auto line = std::exchange(input_, {});
                try {
                    receive(ipc::decode(line));
                } catch (const std::exception &error) {
                    // Supervisor validation messages only; reply content is never logged.
                    log_event(LogLevel::Warning,
                              QString("Rejected a worker reply: %1").arg(error.what()));
                    fail(ui_text("Translation worker returned invalid data."));
                } catch (...) {
                    fail(ui_text("Translation worker returned invalid data."));
                }
                if (stopping_ || !wanted_)
                    return;
            } else {
                input_ += byte;
                if (input_.size() >= ipc::frame_limit) {
                    fail(ui_text("Translation worker response was too large."));
                    return;
                }
            }
        }
    }
}
void Worker::receive(const QJsonObject &value) {
    if (value["id"].toString() != request_id_)
        throw std::runtime_error("Wrong worker request.");
    const auto type = value["type"].toString();
    if (type == "status" && phase_ == Phase::Preparing) {
        const auto message = value["text"].toString();
        // A complete catalogue emits at most six checks + 101 progress steps
        // per file. Bound valid-looking output too, so a faulty helper cannot
        // keep the OBS event loop occupied indefinitely.
        if (!value["text"].isString() || message.size() > limits::status_characters ||
            ++status_updates_ > 640)
            throw std::runtime_error("Worker status too long.");
        if (status)
            status(message);
        return;
    }
    deadline_.stop();
    if (type == "ready" && phase_ == Phase::Preparing && value["ok"].isBool()) {
        const auto device = value["device"].toString();
        const auto fallback = value["fallback"].toString();
        // Known GPU issue codes only; mirrors gpu_runtime's codes (helper-only module).
        static const QStringList issues{"",       "no-driver", "no-device", "old-driver",
                                        "libraries", "memory", "unknown"};
        const auto issue = value["gpu_issue"].toString();
        const auto detail = value["gpu_detail"].toString();
        if ((device != "cpu" && device != "gpu") || fallback.size() > limits::status_characters ||
            value["model_id"].toString() != model_id(profile_, target_) ||
            value["target_language"].toString() != target_ || !issues.contains(issue) ||
            detail.size() > limits::status_characters)
            throw std::runtime_error("Invalid worker device.");
        actual_ = device == "gpu" ? RunOn::GPU : RunOn::CPU;
        gpu_issue_ = issue;
        gpu_detail_ = detail;
        if (!issue.isEmpty())
            log_event(LogLevel::Warning,
                      QString("GPU not used (%1)%2")
                          .arg(issue, detail.isEmpty() ? QString() : ": " + detail));
        const bool good = value["ok"].toBool();
        if (good) {
            phase_ = Phase::Idle;
            download_ = false;
            gpu_download_ = false;
        } else {
            wanted_ = false;
            discard_waiting();
            stop();
        }
        if (state_changed)
            state_changed(good ? State::Ready : State::Failed, actual_,
                          recovery_reason_.isEmpty() ? fallback : recovery_reason_);
    } else if (type == "result" && phase_ == Phase::Translating && active_) {
        const auto text = value["text"].toString();
        const auto language = value["source_language"].toString();
        const bool valid_language =
            language.isEmpty() || ((language.size() == 2 || language.size() == 3) &&
                                   std::all_of(language.begin(), language.end(), [](QChar c) {
                                       return c >= QLatin1Char('a') && c <= QLatin1Char('z');
                                   }));
        if (!value["text"].isString() || text.size() > limits::translation_characters ||
            !value["translated"].isBool() || !valid_language ||
            !value["source_language"].isString())
            throw std::runtime_error("Invalid translation result.");
        const auto job = std::exchange(active_, {});
        phase_ = Phase::Idle;
        cancel_sent_ = false;
        if (const auto took = now() - translate_started_; took > 5000)
            log_event(LogLevel::Info, QString("Slow translation: %1 ms").arg(took));
        if (job->epoch == epoch_ && result)
            result(*job, {text, value["translated"].toBool(), language});
    } else if (type == "cleared" && phase_ == Phase::Clearing)
        phase_ = Phase::Idle;
    else
        throw std::runtime_error("Unexpected worker response.");
    pump();
}
void Worker::offer(Message message) {
    if (!wanted_) {
        if (discarded)
            discarded(message.id);
        return;
    }
    const auto drops = queue_.offer({std::move(message), epoch_, now()}, [this](const Job &job) {
        discard(job);
    });
    if (drops && skipped)
        skipped(drops);
    pump();
}
void Worker::clear() {
    ++epoch_;
    discard_waiting();
    clear_cache_ = true;
    if (phase_ == Phase::Translating && !cancel_sent_ && !stopping_) {
        const auto bytes = ipc::encode({{"type", "cancel"}});
        if (process_.bytesToWrite() + bytes.size() <= ipc::frame_limit) {
            process_.write(bytes);
            cancel_sent_ = true;
        }
    }
    pump();
}
void Worker::pump() {
    if (!wanted_ || stopping_ || phase_ != Phase::Idle)
        return;
    if (clear_cache_) {
        clear_cache_ = false;
        phase_ = Phase::Clearing;
        send({{"type", "clear"}}, limits_.startup_ms);
        return;
    }
    size_t expired = 0;
    active_ = queue_.take(now(), expired, [this](const Job &job) {
        discard(job);
    });
    if (expired && skipped)
        skipped(expired);
    if (!active_)
        return;
    phase_ = Phase::Translating;
    translate_started_ = now();
    send({{"type", "translate"}, {"message", ipc::message(active_->message)}},
         int(std::min<qint64>(
             limits_.translation_ms,
             std::max<qint64>(1, limits::translation_age_ms - (now() - active_->arrived)))));
}
void Worker::fail(const QString &reason, bool recover) {
    const auto dropped = queue_.size();
    discard_waiting();
    if (dropped && skipped)
        skipped(dropped);
    const bool retry = recover && wanted_ && failures_++ == 0;
    log_event(retry ? LogLevel::Warning : LogLevel::Error,
              QString("Translation failure: %1 %2")
                  .arg(reason, QString(retry ? "Restarting the worker once."
                                             : "Translation stopped until reconnect or model "
                                               "change.")));
    wanted_ = retry;
    if (retry && requested_ == RunOn::GPU) {
        launch_device_ = RunOn::CPU;
        gpu_download_ = false;
        recovery_reason_ = ui_text("GPU worker stopped. Translation restarted on CPU.");
    }
    stop();
    if (status)
        status(reason + (retry ? ui_text(" Restarting translation…")
                               : ui_text(" Chat remains available. Use Download model to retry.")));
    if (state_changed)
        state_changed(retry ? State::Recovering : State::Failed, actual_, {});
}
void Worker::discard(const Job &job) {
    if (discarded)
        discarded(job.message.id);
}
void Worker::discard_waiting() {
    queue_.clear([this](const Job &job) {
        discard(job);
    });
    if (active_)
        discard(*active_);
}
} // namespace awl::chat
