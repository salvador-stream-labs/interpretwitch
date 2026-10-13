// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QTextEdit>
#include <QSet>
#include <QTimer>
#include <functional>
namespace awl::chat {
class ChatList;
struct Row;
class ChatView : public QTextEdit {
    Q_OBJECT
  public:
    explicit ChatView(ChatList *model, QWidget *parent = nullptr);
    ChatList *model() const {
        return model_;
    }
    void latest();
    bool following() const;
    std::function<void(bool)> reading_older;

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  private:
    ChatList *model_;
    QSet<QString> deferred_;
    bool editing_ = false, mouse_down_ = false;
    QTimer spinner_;
    QRegion waiting_region_;
    int spinner_angle_ = 0;
    void mutate(const std::function<void()> &operation);
    void write_row(QTextCursor &cursor, const Row &row);
    void update_rows(int first, int last);
    void flush_deferred();
    void notify_reading();
};
} // namespace awl::chat
