// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "limits.hpp"
#include "chat_core.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <stdexcept>

namespace awl::chat::ipc {
constexpr qsizetype frame_limit = limits::ipc_frame_bytes;
constexpr int version = 2;
inline QByteArray encode(QJsonObject value) {
    value["v"] = version;
    auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    if (bytes.size() >= frame_limit)
        throw std::runtime_error("Translation message exceeds its size limit.");
    return bytes + '\n';
}
inline QJsonObject decode(const QByteArray &bytes) {
    const auto value = QJsonDocument::fromJson(bytes).object();
    if (bytes.size() > frame_limit || value["v"].toInt() != version || !value["type"].isString())
        throw std::runtime_error("Invalid translation worker response.");
    return value;
}
inline QJsonObject message(const Message &value) {
    QJsonArray parts;
    for (const auto &part : value.parts)
        parts.append(QJsonObject{{"text", part.text}, {"protected", part.protected_text}});
    return {{"original", value.original}, {"parts", parts}};
}
inline Message message(const QJsonObject &value) {
    Message out;
    out.original = value["original"].toString();
    const auto parts = value["parts"].toArray();
    if (out.original.isEmpty() || out.original.size() > limits::message_characters ||
        parts.size() > limits::message_characters)
        throw std::runtime_error("Invalid translation input.");
    QString reconstructed;
    for (const auto &item : parts) {
        const auto part = item.toObject();
        if (!part["text"].isString() || !part["protected"].isBool())
            throw std::runtime_error("Invalid translation input.");
        reconstructed += part["text"].toString();
        if (reconstructed.size() > limits::message_characters)
            throw std::runtime_error("Invalid translation input.");
        out.parts.push_back({part["text"].toString(), part["protected"].toBool()});
    }
    if (reconstructed != out.original)
        throw std::runtime_error("Invalid translation input.");
    return out;
}
} // namespace awl::chat::ipc
