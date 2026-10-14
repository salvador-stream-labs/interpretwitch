// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel.hpp"
#include "ui_text.hpp"
#include <QApplication>
#include <QTimer>
#include <QTemporaryDir>
using namespace awl::chat;
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    set_ui_locale(argc > 2 ? QString::fromUtf8(argv[2]) : "en-US");
    QTemporaryDir dir;
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#242629"));
    palette.setColor(QPalette::WindowText, QColor("#e4e6e8"));
    palette.setColor(QPalette::Base, QColor("#1d1f22"));
    palette.setColor(QPalette::Text, QColor("#e4e6e8"));
    palette.setColor(QPalette::Button, QColor("#36393e"));
    palette.setColor(QPalette::ButtonText, QColor("#e4e6e8"));
    app.setPalette(palette);
    Panel panel("", dir.path(), dir.filePath("preview.ini"), nullptr, true);
    panel.show();
    QTimer::singleShot(400, [&] {
        if (argc > 1)
            panel.grab().save(argv[1]);
        app.quit();
    });
    return app.exec();
}
