// SPDX-License-Identifier: GPL-2.0-or-later
#include "limits.hpp"
#include "chat_core.hpp"
#include <QRegularExpression>

namespace awl::chat {
QVector<Part> protect_text(const QString &text) {
    // Twitch emotes are protected separately by their typed fragments. Also
    // keep links, mentions and common third-party emote names out of the model.
    static const QRegularExpression re(
        R"((?:https?://|www\.)\S+|@[A-Za-z0-9_]+|\b(?:Kappa|PogChamp|Pog|KEKW|LUL|OMEGALUL|PepeHands|FeelsBadMan|FeelsGoodMan|MonkaS|monkaS|GG|gg|EZ|ez)\b)");
    QVector<Part> out;
    qsizetype at = 0;
    auto matches = re.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (match.capturedStart() > at)
            out.push_back({text.mid(at, match.capturedStart() - at), false});
        out.push_back({match.captured(), true});
        at = match.capturedEnd();
    }
    if (at < text.size())
        out.push_back({text.mid(at), false});
    return out;
}
bool repetitive(const QString &text) {
    const auto words = text.toCaseFolded().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (int width = 1; width <= 6; ++width) {
        int run = 0;
        for (qsizetype i = width; i < words.size(); ++i) {
            run = words[i] == words[i - width] ? run + 1 : 0;
            if (run >= width * 5)
                return true; // six consecutive identical phrases
        }
    }
    return false;
}
size_t JobQueue::offer(Job job, const Discard &discard) {
    jobs_.push_back(std::move(job));
    if (jobs_.size() <= limits::queued_translations)
        return 0;
    if (discard)
        discard(jobs_.front());
    jobs_.pop_front();
    return 1;
}
std::optional<Job> JobQueue::take(qint64 now, size_t &expired, const Discard &discard) {
    while (!jobs_.empty() && now - jobs_.front().arrived > limits::translation_age_ms) {
        if (discard)
            discard(jobs_.front());
        jobs_.pop_front();
        ++expired;
    }
    if (jobs_.empty())
        return {};
    Job result = std::move(jobs_.front());
    jobs_.pop_front();
    return result;
}
} // namespace awl::chat
