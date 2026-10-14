// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "inline_chat.hpp"
#include "worker.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <memory>

namespace awl::chat {
// A modeless settings window; closing it does not stop chat translation.
class Panel : public QDialog {
  public:
    Panel(QString manifest, QString cache, QString settings, QWidget *parent = nullptr,
          bool preview = false, QString helper = {}, InlineChat::Execute execute = {});
    ~Panel();
    void start();

  private:
    QSettings settings_;
    std::unique_ptr<Worker> worker_;
    std::unique_ptr<InlineChat> bridge_;
    QCheckBox *enabled_, *language_label_, *background_;
    QComboBox *run_on_, *resource_usage_, *target_language_;
    QPushButton *download_, *cancel_, *reconnect_;
    QLabel *connection_, *model_, *device_help_, *profile_help_;
    Worker::State model_state_ = Worker::State::Stopped;
    bool preview_ = false, started_ = false;
    size_t skipped_ = 0;
    void build_ui();
    void bind_worker();
    void save_settings();
    void change_enabled();
    void prepare_model(bool download);
    void update_device_help();
    void update_targets(const QString &preferred);
    void change_model();
    QString selected_target() const;
    RunOn selected_device() const;
    ModelProfile selected_profile() const;
};
} // namespace awl::chat
