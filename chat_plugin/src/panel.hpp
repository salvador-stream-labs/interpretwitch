// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "twitch.hpp"
#include "worker.hpp"
#include <QWidget>
#include "chat_list.hpp"
#include "chat_view.hpp"
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QSettings>
#include <QToolButton>
#include <QComboBox>
#include <memory>
#include <QStackedWidget>
#include "twitch_setup.hpp"
namespace awl::chat {
class Panel : public QWidget {
  public:
    Panel(QString manifest, QString cache, QString settings, QWidget *parent = nullptr,
          bool preview = false, QString helper = {});
    ~Panel();
    void receive(Event event);

  private:
    QSettings settings_;
    TwitchClient twitch_;
    std::unique_ptr<Worker> worker_;
    ChatList rows_;
    ChatView *list_;
    QLineEdit *channel_;
    QString client_id_;
    QPushButton *setup_button_;
    QLabel *setup_status_;
    QStackedWidget *pages_;
    QWidget *chat_page_;
    TwitchSetup *setup_;
    QPushButton *connect_, *download_, *cancel_, *jump_, *approve_;
    QCheckBox *remember_, *originals_, *pause_;
    QComboBox *run_on_, *resource_usage_;
    QLabel *connection_, *model_, *empty_, *device_help_, *profile_help_;
    QToolButton *connection_toggle_;
    QString authorization_url_;
    bool model_ready_ = false, model_loading_ = false, desired_ = false, preview_ = false;
    size_t skipped_ = 0;
    void build_ui();
    void open_setup();
    void update_setup_status();
    void bind_worker();
    void bind_twitch();
    void bind_controls();
    void restore_connection();
    void save_settings();
    void begin_connection();
    void clear_chat();
    RunOn selected_device() const;
    ModelProfile selected_profile() const;
    void prepare_model(bool download);
    void update_device_help();
};
} // namespace awl::chat
