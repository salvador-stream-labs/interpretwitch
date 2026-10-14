// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel.hpp"
#include "ui_text.hpp"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QSignalBlocker>
#include <algorithm>
#include <QVBoxLayout>

namespace awl::chat {
Panel::Panel(QString manifest, QString cache, QString settings, QWidget *parent, bool preview,
             QString helper, InlineChat::Execute execute)
    : QDialog(parent), settings_(settings, QSettings::IniFormat), preview_(preview) {
    worker_ = std::make_unique<Worker>(manifest, cache, nullptr, std::move(helper));
    bridge_ = std::make_unique<InlineChat>(std::move(execute));
    build_ui();
    bind_worker();
    bridge_->message = [this](Message message) {
        if (enabled_->isChecked() && model_state_ == Worker::State::Ready)
            worker_->offer(std::move(message));
        else
            bridge_->deliver(message.id, {});
    };
    bridge_->reset = [this] {
        worker_->clear();
    };
    bridge_->status = [this](QString text) {
        connection_->setText(text);
    };
    bridge_->connected = [this] {
        // A page reconnect must not reset the helper's failure/restart allowance.
        if (!preview_ && enabled_->isChecked() && model_state_ == Worker::State::Stopped)
            prepare_model(false);
    };
    bridge_->set_target(selected_target());
    bridge_->set_appearance(language_label_->isChecked(), background_->isChecked());
    connect(enabled_, &QCheckBox::toggled, this, [this] {
        save_settings();
        change_enabled();
    });
    connect(reconnect_, &QPushButton::clicked, this, [this] {
        worker_->clear();
        if (model_state_ == Worker::State::Failed)
            prepare_model(false);
        bridge_->start();
    });
    connect(download_, &QPushButton::clicked, this, [this] {
        prepare_model(true);
    });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        worker_->cancel_download();
    });
    for (auto checkbox : {language_label_, background_})
        connect(checkbox, &QCheckBox::toggled, this, [this] {
            save_settings();
            bridge_->set_appearance(language_label_->isChecked(), background_->isChecked());
        });
    connect(resource_usage_, &QComboBox::currentIndexChanged, this, [this] {
        update_targets(selected_target());
        change_model();
    });
    for (auto combo : {run_on_, target_language_})
        connect(combo, &QComboBox::currentIndexChanged, this, &Panel::change_model);
}

void Panel::build_ui() {
    setObjectName("interpretwitchSettings");
    setWindowTitle(ui_text("Interpretwitch"));
    setMinimumSize(360, 340);
    resize(460, 620);
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 12);
    auto scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto content = new QWidget(scroll);
    auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->setSpacing(12);
    auto label = [&](const QString &text, QVBoxLayout *where) {
        auto result = new QLabel(text, content);
        result->setWordWrap(true);
        result->setTextFormat(Qt::PlainText);
        where->addWidget(result);
        return result;
    };
    label(ui_text("Translations appear in OBS's Twitch Chat dock. Connect Twitch in "
                  "OBS Settings → Stream. No separate Interpretwitch login is needed."),
          layout);
    enabled_ = new QCheckBox(ui_text("Enable translation"), content);
    enabled_->setObjectName("interpretwitchEnabled");
    enabled_->setChecked(settings_.value("enabled", true).toBool());
    layout->addWidget(enabled_);
    connection_ = label(ui_text("Waiting for OBS's Twitch Chat dock."), layout);
    connection_->setObjectName("interpretwitchConnection");
    reconnect_ = new QPushButton(ui_text("Reconnect to chat"), content);
    reconnect_->setEnabled(enabled_->isChecked());
    layout->addWidget(reconnect_);

    auto translation = new QGroupBox(ui_text("Translation"), content);
    auto controls = new QVBoxLayout(translation);
    auto form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    resource_usage_ = new QComboBox(translation);
    resource_usage_->setObjectName("awlChatResourceUsage");
    resource_usage_->setAccessibleName(ui_text("Resource usage"));
    for (const auto profile : {ModelProfile::Light, ModelProfile::Heavy})
        resource_usage_->addItem(profile_name(profile), model_id(profile));
    resource_usage_->setCurrentIndex(
        settings_.value("model_id").toString() == model_id(ModelProfile::Heavy) ? 1 : 0);
    resource_usage_->setMinimumContentsLength(14);
    resource_usage_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    form->addRow(ui_text("Resource usage"), resource_usage_);
    target_language_ = new QComboBox(translation);
    target_language_->setObjectName("interpretwitchTargetLanguage");
    target_language_->setAccessibleName(ui_text("Translate into"));
    target_language_->setMinimumContentsLength(14);
    target_language_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    update_targets(settings_.value("target_language", "ru").toString());
    form->addRow(ui_text("Translate into"), target_language_);
    controls->addLayout(form);
    profile_help_ = label(profile_description(selected_profile(), selected_target()), controls);
    run_on_ = new QComboBox(translation);
    run_on_->setObjectName("awlChatRunOn");
    run_on_->setAccessibleName(ui_text("Run translation on"));
    run_on_->addItem(ui_text("CPU"), "cpu");
    run_on_->addItem(ui_text("GPU (NVIDIA)"), "gpu");
    run_on_->setCurrentIndex(settings_.value("run_on", "cpu").toString() == "gpu" ? 1 : 0);
#ifdef __APPLE__
    run_on_->setItemText(1, ui_text("GPU (unavailable on Mac)"));
    static_cast<QStandardItemModel *>(run_on_->model())->item(1)->setEnabled(false);
    run_on_->setCurrentIndex(0);
#endif
    auto devices = new QFormLayout;
    devices->setRowWrapPolicy(QFormLayout::WrapLongRows);
    devices->addRow(ui_text("Run on"), run_on_);
    controls->addLayout(devices);
    device_help_ = label({}, controls);
    update_device_help();
    model_ =
        label(ui_text("Download the selected model once. Existing models are reused."), controls);
    model_->setObjectName("awlChatModelStatus");
    auto actions = new QHBoxLayout;
    download_ = new QPushButton(ui_text("Download model"), translation);
    download_->setObjectName("awlChatDownload");
    download_->setEnabled(enabled_->isChecked());
    cancel_ = new QPushButton(ui_text("Cancel download"), translation);
    cancel_->hide();
    actions->addWidget(download_);
    actions->addWidget(cancel_);
    actions->addStretch();
    controls->addLayout(actions);
    layout->addWidget(translation);

    auto appearance = new QGroupBox(ui_text("Appearance"), content);
    auto options = new QVBoxLayout(appearance);
    language_label_ = new QCheckBox(ui_text("Show detected language"), appearance);
    language_label_->setObjectName("interpretwitchLanguageLabel");
    language_label_->setChecked(settings_.value("language_label", true).toBool());
    background_ = new QCheckBox(ui_text("Subtle background around translations"), appearance);
    background_->setObjectName("interpretwitchBackground");
    background_->setChecked(settings_.value("background", true).toBool());
    options->addWidget(language_label_);
    options->addWidget(background_);
    label(ui_text("Original messages, names and moderation controls stay in Twitch's layout."),
          options);
    layout->addWidget(appearance);
    label(ui_text("Chat is translated on your computer. Salvador Stream Labs receives no chat "
                  "messages or Twitch credentials."),
          layout);
    layout->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);
    auto close = new QDialogButtonBox(this);
    close->addButton(ui_text("Close"), QDialogButtonBox::RejectRole);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::hide);
    root->addWidget(close);
}

void Panel::bind_worker() {
    worker_->status = [this](QString text) {
        model_->setText(text);
    };
    worker_->state_changed = [this](Worker::State state, RunOn actual, QString fallback) {
        model_state_ = state;
        const bool ready = state == Worker::State::Ready;
        const bool busy = state == Worker::State::Preparing || state == Worker::State::Recovering;
        run_on_->setEnabled(!busy);
        resource_usage_->setEnabled(!busy);
        target_language_->setEnabled(!busy);
        if (!busy)
            cancel_->hide();
        download_->setEnabled(enabled_->isChecked() && !busy);
        download_->setVisible(!ready);
        if (ready) {
            model_->setText(ui_text("Local translation · %1").arg(run_on_name(actual)));
            if (!fallback.isEmpty()) {
                device_help_->setText(ui_text("GPU could not start. Using CPU. Check the NVIDIA "
                                              "driver and GPU support in this build."));
                device_help_->setToolTip(fallback);
            }
        }
        bridge_->set_ready(ready && enabled_->isChecked(),
                           ready                                ? ui_text("Ready")
                           : state == Worker::State::Recovering ? ui_text("Recovering")
                           : busy                               ? ui_text("Preparing")
                                                                : ui_text("Unavailable"),
                           model_->text());
    };
    worker_->skipped = [this](size_t count) {
        skipped_ += count;
        model_->setText(ui_text("Busy chat · %1 older translations skipped").arg(skipped_));
    };
    worker_->result = [this](Job job, Translation result) {
        bridge_->deliver(job.message.id, result);
    };
    worker_->discarded = [this](QString id) {
        bridge_->deliver(id, {});
    };
}
Panel::~Panel() {
    bridge_.reset();
    worker_.reset();
}
void Panel::start() {
    if (started_)
        return;
    started_ = true;
    change_enabled();
}
void Panel::change_enabled() {
    reconnect_->setEnabled(enabled_->isChecked());
    download_->setEnabled(enabled_->isChecked() && model_state_ != Worker::State::Preparing &&
                          model_state_ != Worker::State::Recovering);
    if (!enabled_->isChecked()) {
        bridge_->stop();
        worker_->shutdown();
        connection_->setText(ui_text("Translation disabled. Twitch chat is unchanged."));
        model_->setText(ui_text("Translation model unloaded."));
    } else if (started_) {
        bridge_->start();
    }
}
void Panel::save_settings() {
    settings_.setValue("enabled", enabled_->isChecked());
    settings_.setValue("language_label", language_label_->isChecked());
    settings_.setValue("background", background_->isChecked());
    settings_.setValue("run_on", run_on_->currentData().toString());
    settings_.setValue("model_id", model_id(selected_profile(), selected_target()));
    settings_.setValue("target_language", selected_target());
    settings_.setValue(selected_profile() == ModelProfile::Light ? "light_target" : "heavy_target",
                       selected_target());
    settings_.sync();
}
RunOn Panel::selected_device() const {
    return run_on_->currentData().toString() == "gpu" ? RunOn::GPU : RunOn::CPU;
}
ModelProfile Panel::selected_profile() const {
    return model_profile(resource_usage_->currentData().toString()).value_or(ModelProfile::Light);
}
QString Panel::selected_target() const {
    return target_language_->currentData().toString();
}
void Panel::update_targets(const QString &preferred) {
    const QSignalBlocker block(target_language_);
    auto codes = target_languages(selected_profile());
    std::sort(codes.begin(), codes.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(language_name(a), language_name(b)) < 0;
    });
    target_language_->clear();
    for (const auto &code : codes)
        target_language_->addItem(language_name(code), code);
    auto target = preferred;
    if (!codes.contains(target))
        target =
            settings_
                .value(selected_profile() == ModelProfile::Light ? "light_target" : "heavy_target",
                       "ru")
                .toString();
    if (!codes.contains(target))
        target = "ru";
    target_language_->setCurrentIndex(target_language_->findData(target));
}
void Panel::change_model() {
    save_settings();
    update_device_help();
    profile_help_->setText(profile_description(selected_profile(), selected_target()));
    bridge_->set_target(selected_target());
    if (!preview_ && enabled_->isChecked() && started_)
        prepare_model(false);
}
void Panel::update_device_help() {
    device_help_->setToolTip({});
#ifdef __APPLE__
    device_help_->setText(
        ui_text("CPU keeps translation local. This engine does not support the Mac GPU."));
#else
    device_help_->setText(
        selected_device() == RunOn::GPU
            ? ui_text(
                  "Uses your NVIDIA graphics card. CPU takes over if GPU translation cannot start.")
            : ui_text("Keeps your graphics card free for the game. Recommended for most chats."));
#endif
}
void Panel::prepare_model(bool download) {
    if (model_state_ == Worker::State::Preparing || model_state_ == Worker::State::Recovering)
        return;
    cancel_->setVisible(download);
    update_device_help();
    model_->setText(
        ui_text("Preparing local translation · %1…").arg(run_on_name(selected_device())));
    bridge_->set_target(selected_target());
    worker_->prepare(download, selected_device(), selected_profile(), selected_target());
}
} // namespace awl::chat
