// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_list.hpp"
#include "limits.hpp"

namespace awl::chat {
QVariant ChatList::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rows.size())
        return {};
    const auto &row = rows[index.row()];
    if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole)
        return row.message.name + ": " +
               (row.translation.translated ? row.translation.text : row.message.original);
    return {};
}
void ChatList::add(Message message, bool pending) {
    for (const auto &row : rows)
        if (row.message.id == message.id)
            return;
    if (rows.size() >= limits::history_messages) {
        beginRemoveRows({}, 0, 0);
        rows.removeFirst();
        endRemoveRows();
    }
    const auto count = int(rows.size());
    beginInsertRows({}, count, count);
    rows.push_back({std::move(message), {}, pending});
    endInsertRows();
}
void ChatList::translated(const QString &id, Translation translation) {
    for (qsizetype i = 0; i < rows.size(); ++i)
        if (rows[i].message.id == id) {
            rows[i].translation = std::move(translation);
            rows[i].pending = false;
            emit dataChanged(index(int(i)), index(int(i)));
            return;
        }
}
void ChatList::discarded(const QString &id) {
    for (const auto &row : rows)
        if (row.message.id == id && row.pending) {
            translated(id, {row.message.original, false, true});
            return;
        }
}
void ChatList::remove(const std::function<bool(const Row &)> &predicate) {
    for (qsizetype i = rows.size(); i-- > 0;)
        if (predicate(rows[i])) {
            beginRemoveRows({}, int(i), int(i));
            rows.removeAt(i);
            endRemoveRows();
        }
}
void ChatList::clear() {
    beginResetModel();
    rows.clear();
    endResetModel();
}
void ChatList::show_originals(bool value) {
    originals = value;
    if (!rows.isEmpty())
        emit dataChanged(index(0), index(int(rows.size() - 1)));
}
} // namespace awl::chat
