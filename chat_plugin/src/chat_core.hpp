// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <deque>
#include <functional>
#include <optional>

namespace awl::chat {
struct Part {
    QString text;
    bool protected_text = false;
};
struct Message {
    QString id, original;
    QVector<Part> parts;
};
struct Translation {
    QString text;
    bool translated = false;
    QString source_language;
};
enum class RunOn { CPU, GPU };
inline QString run_on_name(RunOn device) {
    return device == RunOn::GPU ? "GPU" : "CPU";
}
QVector<Part> protect_text(const QString &text);
bool repetitive(const QString &text);
struct Job {
    Message message;
    quint64 epoch = 0;
    qint64 arrived = 0;
};
class JobQueue {
  public:
    using Discard = std::function<void(const Job &)>;
    size_t offer(Job job, const Discard &discard = {});
    std::optional<Job> take(qint64 now, size_t &expired, const Discard &discard = {});
    void clear(const Discard &discard = {}) {
        if (discard)
            for (const auto &job : jobs_)
                discard(job);
        jobs_.clear();
    }
    size_t size() const {
        return jobs_.size();
    }

  private:
    std::deque<Job> jobs_;
};
} // namespace awl::chat
