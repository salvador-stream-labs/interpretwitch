// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "limits.hpp"
#include "chat_core.hpp"
#include "model_profile.hpp"
#include <QObject>
#include <QProcess>
#include <QTimer>

namespace awl::chat {
// Parent-side supervisor. No inference libraries are linked into this class.
class Worker : public QObject {
  public:
    enum class State { Stopped, Preparing, Ready, Recovering, Failed };
    struct Limits {
        int startup_ms = 30000, translation_ms = limits::translation_age_ms, download_ms = 600000,
            restart_ms = 500, gpu_download_ms = 1800000;
    };
    Worker(QString manifest, QString directory, QObject *parent = nullptr, QString helper = {});
    Worker(QString manifest, QString directory, QObject *parent, QString helper, Limits limits);
    ~Worker();
    std::function<void(QString)> status;
    std::function<void(State, RunOn, QString)> state_changed;
    std::function<void(Job, Translation)> result;
    std::function<void(size_t)> skipped;
    std::function<void(QString)> discarded;
    void prepare(bool download, RunOn device = RunOn::CPU,
                 ModelProfile profile = ModelProfile::Light, QString target = "ru",
                 bool gpu_download = false);
    void cancel_download();
    void shutdown();
    void offer(Message message);
    void clear();
    // Windows GPU mode: catalogue and per-user folder of the optional cuBLAS pack.
    void set_gpu_support(QString manifest, QString directory);
    // Last helper GPU report: issue code ("" when none) and an unknown error's text.
    QString gpu_issue() const {
        return gpu_issue_;
    }
    QString gpu_detail() const {
        return gpu_detail_;
    }

  private:
    enum class Phase { Stopped, Preparing, Idle, Translating, Clearing };
    QString manifest_, directory_, helper_, request_id_, recovery_reason_;
    QProcess process_;
    QTimer deadline_, restart_;
    QByteArray input_;
    JobQueue queue_;
    std::optional<Job> active_;
    Limits limits_;
    quint64 epoch_ = 1, sequence_ = 0;
    RunOn requested_ = RunOn::CPU, launch_device_ = RunOn::CPU, actual_ = RunOn::CPU;
    ModelProfile profile_ = ModelProfile::Light;
    QString target_ = "ru";
    QString gpu_manifest_, gpu_directory_, gpu_issue_, gpu_detail_;
    Phase phase_ = Phase::Stopped;
    bool wanted_ = false, stopping_ = false, download_ = false, clear_cache_ = false,
         cancel_sent_ = false, gpu_download_ = false;
    int failures_ = 0;
    int status_updates_ = 0;
    qint64 translate_started_ = 0;
    void launch();
    void stop();
    void send(QJsonObject value, int timeout);
    void read();
    void receive(const QJsonObject &value);
    void pump();
    void fail(const QString &reason, bool recover = true);
    void discard(const Job &job);
    void discard_waiting();
};
} // namespace awl::chat
