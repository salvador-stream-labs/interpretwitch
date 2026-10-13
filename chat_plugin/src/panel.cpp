// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel.hpp"
#include "chat_style.hpp"
#include "ui_text.hpp"
#include "text_menu.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QScrollBar>
#include <QScrollArea>
#include <QDesktopServices>
#include <QToolButton>
#include <QTimer>
#include <QStandardItemModel>
#include <algorithm>

namespace awl::chat {
Panel::Panel(QString manifest, QString cache, QString settings, QWidget *parent, bool preview,
             QString helper)
    : QWidget(parent), settings_(settings, QSettings::IniFormat), twitch_(this), rows_(this),
      preview_(preview) {
    worker_ = std::make_unique<Worker>(manifest, cache, nullptr, std::move(helper));
    client_id_ = settings_.value("client_id").toString().trimmed();
    build_ui();
    bind_worker();
    bind_twitch();
    bind_controls();
    restore_connection();
    if (!valid_client_id(client_id_))
        open_setup();
}

void Panel::build_ui() {
    setObjectName("awlChatPanel");
    setMinimumSize(300, 340);
    setStyleSheet(QString::fromUtf8(chat_style));
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    pages_ = new QStackedWidget(this);
    root->addWidget(pages_);
    chat_page_ = new QWidget(pages_);
    pages_->addWidget(chat_page_);
    auto layout = new QVBoxLayout(chat_page_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto header = new QWidget(this);
    header->setObjectName("awlChatHeader");
    auto header_row = new QHBoxLayout(header);
    header_row->setContentsMargins(12, 10, 12, 9);
    auto title = new QLabel(ui_text("Stream Chat"), header);
    QFont title_font = title->font();
    title_font.setPointSizeF(11);
    title_font.setBold(true);
    title->setFont(title_font);
    title->setToolTip(ui_text("Private chat translated into Russian. Visible in this OBS dock."));
    header_row->addWidget(title);
    header_row->addStretch();
    connection_toggle_ = new QToolButton(header);
    connection_toggle_->setObjectName("awlChatConnectionToggle");
    connection_toggle_->setText(ui_text("Settings"));
    connection_toggle_->setCheckable(true);
    connection_toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connection_toggle_->setAccessibleName(ui_text("Show chat settings"));
    header_row->addWidget(connection_toggle_);
    layout->addWidget(header);
    connection_ = new QLabel(ui_text("Chat → Russian · private"), this);
    connection_->setObjectName("awlChatStatus");
    connection_->setWordWrap(true);
    connection_->setTextFormat(Qt::PlainText);
    layout->addWidget(connection_);
    auto connection_settings = new QWidget(this);
    connection_settings->setObjectName("awlChatConnection");
    auto settings_scroll = new QScrollArea(this);
    settings_scroll->setObjectName("awlChatSettingsScroll");
    settings_scroll->setWidgetResizable(true);
    settings_scroll->setFrameShape(QFrame::NoFrame);
    settings_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settings_scroll->setMaximumHeight(420);
    settings_scroll->setWidget(connection_settings);
    auto details = new QVBoxLayout(connection_settings);
    details->setContentsMargins(12, 8, 12, 12);
    details->setSpacing(8);
    const bool show_connection = !settings_.value("connected", false).toBool();
    settings_scroll->setVisible(show_connection);
    connection_toggle_->setChecked(show_connection);
    connection_toggle_->setArrowType(show_connection ? Qt::DownArrow : Qt::RightArrow);
    connect(connection_toggle_, &QToolButton::toggled, this, [this, settings_scroll](bool checked) {
        settings_scroll->setVisible(checked);
        connection_toggle_->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    });
    layout->addWidget(settings_scroll);
    auto channel_row = new QHBoxLayout;
    channel_ = new QLineEdit(settings_.value("channel").toString(), connection_settings);
    channel_->setPlaceholderText(ui_text("Twitch channel"));
    channel_->setAccessibleName(ui_text("Twitch channel"));
    channel_->setMaxLength(26);
    connect_ = new QPushButton(ui_text("Connect Twitch"), connection_settings);
    connect_->setObjectName("awlChatConnect");
    channel_row->addWidget(channel_, 1);
    channel_row->addWidget(connect_);
    details->addLayout(channel_row);
    setup_button_ = new QPushButton(connection_settings);
    setup_button_->setObjectName("awlChatSetup");
    setup_status_ = new QLabel(connection_settings);
    setup_status_->setWordWrap(true);
    setup_status_->setTextFormat(Qt::PlainText);
    details->addWidget(setup_button_);
    details->addWidget(setup_status_);
    connect(setup_button_, &QPushButton::clicked, this, &Panel::open_setup);
    remember_ = new QCheckBox(ui_text("Remember sign-in on this computer"), connection_settings);
    remember_->setChecked(settings_.value("remember", true).toBool());
    details->addWidget(remember_);
    update_setup_status();
    install_text_menu(channel_);
    approve_ = new QPushButton(connection_settings);
    approve_->hide();
    details->addWidget(approve_);
    connect(approve_, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl(authorization_url_));
    });
    auto resource_label = new QLabel(ui_text("Resource usage"), connection_settings);
    resource_usage_ = new QComboBox(connection_settings);
    resource_usage_->setObjectName("awlChatResourceUsage");
    resource_usage_->setAccessibleName(ui_text("Resource usage"));
    resource_label->setBuddy(resource_usage_);
    for (const auto profile : {ModelProfile::Light, ModelProfile::Heavy})
        resource_usage_->addItem(profile_name(profile), model_id(profile));
    resource_usage_->setCurrentIndex(
        settings_.value("model_id").toString() == model_id(ModelProfile::Heavy) ? 1 : 0);
    details->addWidget(resource_label);
    details->addWidget(resource_usage_);
    profile_help_ = new QLabel(profile_description(selected_profile()), connection_settings);
    profile_help_->setObjectName("awlChatProfileHelp");
    profile_help_->setWordWrap(true);
    profile_help_->setTextFormat(Qt::PlainText);
    details->addWidget(profile_help_);
    auto device_row = new QFormLayout;
    run_on_ = new QComboBox(connection_settings);
    run_on_->setObjectName("awlChatRunOn");
    run_on_->setAccessibleName(ui_text("Run translation on"));
    run_on_->addItem(ui_text("CPU"), "cpu");
    run_on_->addItem(ui_text("GPU (NVIDIA)"), "gpu");
    run_on_->setCurrentIndex(settings_.value("run_on", "cpu").toString() == "gpu" ? 1 : 0);
#ifdef __APPLE__
    // This engine has no Metal backend. Do not offer a control that claims to
    // use the Apple GPU, including when settings were copied from Windows.
    run_on_->setItemText(1, ui_text("GPU (unavailable on Mac)"));
    static_cast<QStandardItemModel *>(run_on_->model())->item(1)->setEnabled(false);
    run_on_->setCurrentIndex(0);
#endif
    device_row->addRow(ui_text("Run on"), run_on_);
    details->addLayout(device_row);
    device_help_ = new QLabel(connection_settings);
    device_help_->setObjectName("awlChatDeviceHelp");
    device_help_->setWordWrap(true);
    device_help_->setTextFormat(Qt::PlainText);
    details->addWidget(device_help_);
    update_device_help();
    auto model_row = new QHBoxLayout;
    model_ = new QLabel(ui_text("Download the selected model once."), connection_settings);
    model_->setObjectName("awlChatModelStatus");
    model_->setWordWrap(true);
    model_->setTextFormat(Qt::PlainText);
    download_ = new QPushButton(ui_text("Download model"), connection_settings);
    download_->setObjectName("awlChatDownload");
    cancel_ = new QPushButton(ui_text("Cancel"), connection_settings);
    cancel_->hide();
    model_row->addWidget(model_, 1);
    model_row->addWidget(download_);
    model_row->addWidget(cancel_);
    details->addLayout(model_row);
    empty_ = new QLabel(ui_text("Connect a channel to read chat in Russian.\nMessages will "
                                "appear here as they arrive."),
                        this);
    empty_->setWordWrap(true);
    empty_->setMargin(12);
    layout->addWidget(empty_);
    rows_.originals = settings_.value("originals", true).toBool();
    list_ = new ChatView(&rows_, this);
    layout->addWidget(list_, 1);
    jump_ = new QPushButton(ui_text("↓ Latest messages"), this);
    jump_->hide();
    layout->addWidget(jump_);
    connect(jump_, &QPushButton::clicked, list_, &ChatView::latest);
    list_->reading_older = [this](bool older) {
        jump_->setVisible(older);
    };
    auto footer = new QWidget(this);
    footer->setObjectName("awlChatFooter");
    auto controls = new QHBoxLayout(footer);
    controls->setContentsMargins(12, 9, 12, 9);
    controls->setSpacing(12);
    originals_ = new QCheckBox(ui_text("Show original"), footer);
    originals_->setChecked(rows_.originals);
    originals_->setToolTip(ui_text("Show the original message below each Russian translation"));
    pause_ = new QCheckBox(ui_text("Pause"), footer);
    pause_->setAccessibleName(ui_text("Pause translation"));
    pause_->setToolTip(ui_text("Pause translation; incoming original messages still appear"));
    auto clear = new QToolButton(footer);
    clear->setText(ui_text("Clear"));
    clear->setAccessibleName(ui_text("Clear history"));
    clear->setToolTip(ui_text("Clear chat history"));
    controls->addWidget(originals_);
    controls->addWidget(pause_);
    controls->addStretch();
    controls->addWidget(clear);
    layout->addWidget(footer);
    connect(clear, &QToolButton::clicked, this, [this] {
        clear_chat();
    });
    setup_ = new TwitchSetup(pages_);
    pages_->addWidget(setup_);
    setup_->saved = [this](QString id) {
        if (desired_ || twitch_.active())
            return;
        client_id_ = std::move(id);
        save_settings();
        update_setup_status();
        connection_->setText(ui_text("Client ID saved. Enter your channel and connect to Twitch."));
        pages_->setCurrentWidget(chat_page_);
        connection_toggle_->setChecked(true);
        channel_->setFocus();
    };
    setup_->cancelled = [this] {
        pages_->setCurrentWidget(chat_page_);
    };
}

void Panel::open_setup() {
    if (desired_ || twitch_.active())
        return;
    setup_->begin(client_id_);
    pages_->setCurrentWidget(setup_);
}
void Panel::update_setup_status() {
    const bool configured = valid_client_id(client_id_);
    setup_button_->setText(configured ? ui_text("Edit Twitch setup") : ui_text("Set up Twitch"));
    setup_status_->setText(configured
                               ? ui_text("Client ID saved")
                               : ui_text("Complete the one-time Client ID setup to connect."));
}

void Panel::bind_worker() {
    worker_->status = [this](QString text) {
        model_->setText(text);
    };
    worker_->ready = [this](bool ready, RunOn actual, QString fallback) {
        model_ready_ = ready;
        model_loading_ = false;
        run_on_->setEnabled(true);
        resource_usage_->setEnabled(true);
        cancel_->hide();
        cancel_->setEnabled(true);
        download_->setEnabled(true);
        download_->setVisible(!ready);
        if (ready) {
            model_->setText(ui_text("Local translation · %1").arg(run_on_name(actual)));
            if (!fallback.isEmpty()) {
                device_help_->setText(ui_text("GPU could not start. Using CPU. Check the NVIDIA "
                                              "driver and GPU support in this build."));
                device_help_->setToolTip(fallback);
                connection_toggle_->setChecked(true);
            }
            if (desired_ && !twitch_.active())
                begin_connection();
        } else if (!twitch_.active()) {
            desired_ = false;
            save_settings();
        }
    };
    worker_->skipped = [this](size_t count) {
        skipped_ += count;
        model_->setText(ui_text("Busy chat · %1 older translations skipped").arg(skipped_));
    };
    worker_->result = [this](Job job, Translation translation) {
        rows_.translated(job.message.id, std::move(translation));
    };
    worker_->discarded = [this](QString id) {
        rows_.discarded(id);
    };
}

void Panel::bind_twitch() {
    twitch_.status = [this](QString text) {
        connection_->setText(text);
        if (!twitch_.active()) {
            desired_ = false;
            save_settings();
            approve_->hide();
            connection_toggle_->setChecked(true);
        }
    };
    twitch_.connected = [this](bool connected) {
        if (connected) {
            approve_->hide();
            connection_toggle_->setChecked(false);
        }
    };
    twitch_.authorize = [this](QString url, QString code) {
        authorization_url_ = url;
        approve_->setText(ui_text("Open Twitch · code %1").arg(code));
        approve_->show();
        QDesktopServices::openUrl(QUrl(url));
    };
    twitch_.event = [this](Event value) {
        receive(std::move(value));
    };
}

void Panel::bind_controls() {
    connect(download_, &QPushButton::clicked, this, [this] {
        prepare_model(true);
    });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        worker_->cancel_download();
        cancel_->setEnabled(false);
    });
    connect(originals_, &QCheckBox::toggled, this, [this](bool value) {
        rows_.show_originals(value);
        save_settings();
    });
    connect(pause_, &QCheckBox::toggled, this, [this](bool) {
        worker_->clear();
    });
    connect(run_on_, &QComboBox::currentIndexChanged, this, [this](int) {
        save_settings();
        update_device_help();
        if (!preview_ && (model_ready_ || desired_))
            prepare_model(false);
    });
    connect(resource_usage_, &QComboBox::currentIndexChanged, this, [this](int) {
        save_settings();
        profile_help_->setText(profile_description(selected_profile()));
        if (!preview_ && (model_ready_ || desired_ || twitch_.active()))
            prepare_model(false);
        else
            model_->setText(ui_text("Download the selected model once."));
    });
    connect(connect_, &QPushButton::clicked, this, [this] {
        if (desired_ || twitch_.active()) {
            connection_->setText(ui_text("Disconnected"));
            desired_ = false;
            twitch_.disconnect_chat(true);
            approve_->hide();
            clear_chat();
            // Preserve a credential-store deletion failure reported by Twitch.
            connect_->setText(ui_text("Connect Twitch"));
            save_settings();
            return;
        }
        if (!valid_client_id(client_id_)) {
            open_setup();
            return;
        }
        if (normal_channel(channel_->text()).isEmpty()) {
            channel_->setFocus();
            connection_->setText(ui_text("Enter a Twitch channel name."));
            return;
        }
        desired_ = true;
        connect_->setText(ui_text("Disconnect"));
        save_settings();
        if (model_ready_)
            begin_connection();
        else
            prepare_model(false);
    });
}

void Panel::restore_connection() {
    if (!preview_) {
        // Verify/load only when the owner starts this feature; a closed unused
        // dock does not allocate a translation model just because OBS launched.
        if (settings_.value("connected", false).toBool() && remember_->isChecked() &&
            valid_client_id(client_id_)) {
            desired_ = true;
            save_settings();
            QTimer::singleShot(500, this, [this] {
                if (desired_)
                    prepare_model(false);
            });
        }
    }
}

Panel::~Panel() {
    twitch_.disconnect_chat();
    worker_.reset();
}
void Panel::save_settings() {
    channel_->setReadOnly(desired_);
    setup_button_->setEnabled(!desired_);
    setup_button_->setToolTip(desired_ ? ui_text("Disconnect before changing Twitch setup.")
                                       : QString{});
    remember_->setEnabled(!desired_);
    connect_->setText(desired_ ? ui_text("Disconnect") : ui_text("Connect Twitch"));
    settings_.setValue("channel", normal_channel(channel_->text()));
    settings_.setValue("client_id", client_id_);
    settings_.setValue("remember", remember_->isChecked());
    settings_.setValue("originals", originals_->isChecked());
    settings_.setValue("run_on", run_on_->currentData().toString());
    settings_.setValue("model_id", model_id(selected_profile()));
    settings_.setValue("connected", desired_);
    settings_.sync();
}
RunOn Panel::selected_device() const {
    return run_on_->currentData().toString() == "gpu" ? RunOn::GPU : RunOn::CPU;
}
ModelProfile Panel::selected_profile() const {
    return model_profile(resource_usage_->currentData().toString()).value_or(ModelProfile::Light);
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
    if (model_loading_)
        return;
    model_loading_ = true;
    model_ready_ = false;
    run_on_->setEnabled(false);
    resource_usage_->setEnabled(false);
    download_->setEnabled(false);
    cancel_->setVisible(download);
    update_device_help();
    model_->setText(
        ui_text("Preparing local translation · %1…").arg(run_on_name(selected_device())));
    worker_->prepare(download, selected_device(), selected_profile());
}
void Panel::begin_connection() {
    if (preview_)
        return;
    clear_chat();
    save_settings();
    twitch_.start(client_id_, channel_->text(), remember_->isChecked());
}
void Panel::clear_chat() {
    worker_->clear();
    rows_.clear();
    empty_->show();
}
void Panel::receive(Event event) {
    if (event.kind == EventKind::Clear) {
        clear_chat();
        return;
    }
    if (event.kind == EventKind::Delete) {
        rows_.remove([&](const Row &r) {
            return r.message.id == event.id;
        });
        return;
    }
    if (event.kind == EventKind::ClearUser) {
        rows_.remove([&](const Row &r) {
            return r.message.user_id == event.user_id;
        });
        return;
    }
    if (event.kind != EventKind::Message)
        return;
    const bool translate = model_ready_ && !pause_->isChecked();
    rows_.add(event.message, translate);
    empty_->hide();
    if (translate)
        worker_->offer(std::move(event.message));
}
} // namespace awl::chat
