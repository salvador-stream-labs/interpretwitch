// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>
#include <functional>
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;

namespace awl::chat {
// Registration guidance only. No OAuth, model loading, network requests or
// persistent writes. The panel saves a validated ID only after explicit Save.
class TwitchSetup : public QWidget {
    Q_OBJECT
  public:
    explicit TwitchSetup(QWidget *parent = nullptr);
    void begin(const QString &client_id);
    std::function<void(QString)> saved;
    std::function<void()> cancelled;

  private:
    QLabel *error_;
    QLineEdit *client_;
    QPushButton *copy_url_;
    QScrollArea *scroll_;
};
} // namespace awl::chat
