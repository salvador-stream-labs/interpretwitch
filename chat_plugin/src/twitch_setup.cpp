// SPDX-License-Identifier: GPL-2.0-or-later
#include "twitch_setup.hpp"
#include "chat_core.hpp"
#include "text_menu.hpp"
#include "ui_text.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>

namespace awl::chat {
TwitchSetup::TwitchSetup(QWidget *parent) : QWidget(parent) {
    setObjectName("awlTwitchSetup");
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    auto title = new QLabel(ui_text("Twitch setup"), this);
    QFont heading = title->font();
    heading.setBold(true);
    heading.setPointSizeF(13);
    title->setFont(heading);
    layout->addWidget(title);
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    auto content = new QWidget(scroll_);
    content->setObjectName("awlSetupContent");
    auto instructions = new QVBoxLayout(content);
    instructions->setContentsMargins(0, 0, 6, 0);
    instructions->setSpacing(12);
    auto paragraph = [&](const QString &text, bool is_heading = false) {
        auto label = new QLabel(text, content);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        label->setContextMenuPolicy(Qt::NoContextMenu);
        if (is_heading)
            label->setFont(heading);
        instructions->addWidget(label);
        return label;
    };
    paragraph(ui_text("Registering an app identifies this plugin to Twitch. Authorizing it lets "
                      "Awl Interpretwitch in your local OBS receive Twitch chat messages. Access "
                      "is read-only: it cannot send messages or start a stream. Translation "
                      "happens on your computer."));
    auto existing = new QPushButton(ui_text("I already have a Client ID"), content);
    existing->setObjectName("awlSetupExisting");
    instructions->addWidget(existing);
    connect(existing, &QPushButton::clicked, this, [this] {
        client_->setFocus();
        scroll_->ensureWidgetVisible(client_);
    });
    auto browser_button = [&](const QString &text, const char *name) {
        auto button = new QPushButton(text, content);
        button->setObjectName(name);
        instructions->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this] {
            if (!QDesktopServices::openUrl(QUrl("https://dev.twitch.tv/console/apps"))) {
                error_->setText(
                    ui_text("Could not open your browser. Visit dev.twitch.tv/console/apps."));
                scroll_->ensureWidgetVisible(error_);
            }
        });
    };
    paragraph(ui_text("1. Open the developer console"), true);
    paragraph(ui_text("Sign in with your Twitch account. It needs a verified email and "
                      "two-factor authentication. In Applications, choose Register Your "
                      "Application."));
    browser_button(ui_text("Open Twitch Developer Console"), "awlSetupBrowser");

    paragraph(ui_text("2. Register a Public application"), true);
    paragraph(ui_text("Name: Awl Interpretwitch + your channel name\n"
                      "OAuth Redirect URLs: https://localhost\n"
                      "Category: Chat Bot\n"
                      "Client Type: Public"))->setObjectName("awlSetupFields");
    copy_url_ = new QPushButton(ui_text("Copy redirect URL"), content);
    copy_url_->setObjectName("awlSetupCopyUrl");
    instructions->addWidget(copy_url_);
    connect(copy_url_, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText("https://localhost");
        copy_url_->setText(ui_text("Copied"));
    });
    paragraph(ui_text("Choose a unique name. Click Add beside the redirect URL and remove any "
                      "empty URL rows. Complete verification, then click Create. The redirect "
                      "address is a registration placeholder; you do not need to open it."));

    paragraph(ui_text("3. Copy your Client ID"), true);
    paragraph(ui_text("In Applications, find your app and click Manage."));
    browser_button(ui_text("Open your applications"), "awlSetupApplications");
    auto client_label = paragraph(ui_text("Client ID: paste the value from Manage below."));
    client_ = new QLineEdit(content);
    client_->setObjectName("awlSetupClientId");
    client_->setMaxLength(80);
    client_->setPlaceholderText(ui_text("Paste the public Client ID"));
    client_->setAccessibleName(ui_text("Twitch Client ID"));
    client_label->setBuddy(client_);
    install_text_menu(client_);
    instructions->addWidget(client_);
    error_ = paragraph({});
    error_->setObjectName("awlSetupError");
    paragraph(ui_text("Only the public Client ID is needed. Do not paste a Client Secret, "
                      "access token or password. Your existing app and Client ID still work "
                      "after a plugin update or rename."));

    paragraph(ui_text("4. Authorize read-only chat access"), true);
    paragraph(ui_text("Click Save to return to chat settings.\n"
                      "Channel: the Twitch channel you want to read\n"
                      "Choose Connect Twitch, then approve chat access in your browser. "
                      "This connects the translator independently of OBS's streaming login."));
    instructions->addStretch();
    scroll_->setWidget(content);
    layout->addWidget(scroll_, 1);
    auto actions = new QHBoxLayout;
    auto cancel = new QPushButton(ui_text("Cancel"), this);
    cancel->setObjectName("awlSetupCancel");
    auto save = new QPushButton(ui_text("Save"), this);
    save->setObjectName("awlSetupSave");
    actions->addWidget(cancel);
    actions->addStretch();
    actions->addWidget(save);
    layout->addLayout(actions);
    connect(cancel, &QPushButton::clicked, this, [this] {
        if (cancelled)
            cancelled();
    });
    connect(save, &QPushButton::clicked, this, [this] {
        const auto id = client_->text().trimmed();
        if (!valid_client_id(id)) {
            error_->setText(ui_text(
                "Paste the Client ID from Manage. Use 10–80 letters or digits, without spaces."));
            client_->setFocus();
            scroll_->ensureWidgetVisible(error_);
            return;
        }
        if (saved)
            saved(id);
    });
    connect(client_, &QLineEdit::textChanged, this, [this] {
        error_->clear();
    });
    connect(client_, &QLineEdit::returnPressed, save, &QPushButton::click);
}
void TwitchSetup::begin(const QString &client_id) {
    client_->setText(client_id);
    error_->clear();
    copy_url_->setText(ui_text("Copy redirect URL"));
    scroll_->verticalScrollBar()->setValue(0);
}
} // namespace awl::chat
