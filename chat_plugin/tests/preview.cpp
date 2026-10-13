// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel.hpp"
#include "ui_text.hpp"
#include <QApplication>
#include <QTimer>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QScrollBar>
using namespace awl::chat;
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    set_ui_locale(argc > 4 ? QString::fromUtf8(argv[4]) : "en-US");
    QTemporaryDir dir;
    QSettings settings(dir.filePath("preview.ini"), QSettings::IniFormat);
    settings.setValue("connected", true);
    settings.setValue("client_id", "previewclient");
    settings.setValue("channel", "example_channel");
    const QString mode = argc > 3 ? QString::fromUtf8(argv[3]) : QString{};
    if (mode.contains("heavy"))
        settings.setValue("model_id", model_id(ModelProfile::Heavy));
    settings.sync();
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#242629"));
    palette.setColor(QPalette::WindowText, QColor("#e4e6e8"));
    palette.setColor(QPalette::Base, QColor("#1d1f22"));
    palette.setColor(QPalette::Text, QColor("#e4e6e8"));
    palette.setColor(QPalette::Button, QColor("#36393e"));
    palette.setColor(QPalette::ButtonText, QColor("#e4e6e8"));
    palette.setColor(QPalette::PlaceholderText, QColor("#b3b7be"));
    app.setPalette(palette);
    Panel panel("", dir.path(), dir.filePath("preview.ini"), nullptr, true);
    panel.setWindowTitle(ui_text("Awl Interpretwitch"));
    panel.resize(argc > 2 ? QString::fromUtf8(argv[2]).toInt() : 380,
                 argc > 5 ? QString::fromUtf8(argv[5]).toInt() : 620);
    panel.show();
    if (mode.startsWith("settings"))
        panel.findChild<QToolButton *>("awlChatConnectionToggle")->setChecked(true);
    panel.findChild<QLabel *>("awlChatStatus")
        ->setText(ui_text("Reading #%1 · translating into Russian").arg("example_channel"));
    if (argc > 3 && QString::fromUtf8(argv[3]).startsWith("setup")) {
        panel.findChild<QPushButton *>("awlChatSetup")->click();
        auto setup = panel.findChild<TwitchSetup *>();
        setup->begin({});
    }
    auto model = panel.findChild<ChatView *>()->model();
    const QStringList english{"Hello everyone!", "Please move to the left and wait for the tank.",
                              "Do not pull the boss yet.", "Thank you @alice Kappa", "Привет!"};
    const QStringList russian{"Всем привет!", "Пожалуйста, отойдите влево и дождитесь танка.",
                              "Пока не атакуйте босса.", "Спасибо @alice Kappa", "Привет!"};
    for (int i = 0; i < english.size(); ++i) {
        Event e;
        e.kind = EventKind::Message;
        e.message = {QString::number(i), QString::number(i),
                     QStringList{"Hanna", "RaidLead", "Mika", "Alex", "Иван"}[i], english[i]};
        e.message.color = QStringList{"#32cd32", "#ff7f50", "#a970ff", "#1e90ff", "#ff69b4"}[i];
        panel.receive(e);
        model->translated(QString::number(i), {russian[i], i != 4});
    }
    if (mode == "pending") {
        model->add({"pending", "6", "Mika", "Please wait until everyone is ready."}, true);
        model->add({"queued", "7", "Hanna", "Thanks for the stream!"}, true);
    }
    QTimer::singleShot(400, [&] {
        if (argc > 3 && QString::fromUtf8(argv[3]) == "setup-fields") {
            const auto setup = panel.findChild<TwitchSetup *>();
            const auto scroll = setup->findChild<QScrollArea *>();
            const auto fields = setup->findChild<QLabel *>("awlSetupFields");
            scroll->verticalScrollBar()->setValue(fields->y() - 60);
        }
        if (argc > 3 && QString::fromUtf8(argv[3]) == "setup-bottom") {
            const auto scroll = panel.findChild<TwitchSetup *>()->findChild<QScrollArea *>();
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        }
        if (argc > 1)
            panel.grab().save(argv[1]);
        app.quit();
    });
    return app.exec();
}
