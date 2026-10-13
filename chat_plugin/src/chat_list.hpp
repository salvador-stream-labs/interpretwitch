// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat_core.hpp"
#include <QAbstractListModel>

namespace awl::chat {
// Bounded, memory-only display history. No network, settings or worker access.
struct Row {
    Message message;
    Translation translation;
    bool pending = false;
};
class ChatList : public QAbstractListModel {
  public:
    explicit ChatList(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    QVector<Row> rows;
    bool originals = true;
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(rows.size());
    }
    QVariant data(const QModelIndex &index, int role) const override;
    void add(Message message, bool pending = false);
    void translated(const QString &id, Translation text);
    void discarded(const QString &id);
    void remove(const std::function<bool(const Row &)> &predicate);
    void clear();
    void show_originals(bool value);
};
} // namespace awl::chat
