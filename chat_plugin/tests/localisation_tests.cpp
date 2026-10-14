// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel.hpp"
#include "ui_text.hpp"
#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
using namespace awl::chat;
static void check(bool condition, const char *why) {
    if (!condition)
        throw std::runtime_error(why);
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        for (auto locale : {"en-US", "ru-RU"}) {
            set_ui_locale(locale);
            QTemporaryDir dir;
            Panel panel("missing-catalogue", dir.path(), dir.filePath("settings.ini"), nullptr,
                        true);
            panel.resize(400, 600);
            panel.show();
            app.processEvents();
            check(panel.windowTitle() ==
                      (ui_locale() == "ru-RU" ? "Интерпретвич" : "Interpretwitch"),
                  "Product name");
            check(panel.findChild<QCheckBox *>("interpretwitchEnabled")->text() ==
                      (ui_locale() == "ru-RU" ? "Включить перевод" : "Enable translation"),
                  "Enable control locale");
            check(panel.findChild<QScrollArea *>()->horizontalScrollBar()->maximum() == 0,
                  "Settings overflow a narrow window");
            for (auto label : panel.findChildren<QLabel *>())
                check(!label->text().contains("Awl") && !label->text().contains("Client ID"),
                      "Legacy branding/setup remains");
        }
        set_ui_locale("ru_RU");
        check(ui_error("/private/library/error", "GPU initialization failed. Using CPU.") ==
                  "Не удалось инициализировать GPU. Используется CPU.",
              "Native detail leaked");
        std::cout << "English/Russian settings, product naming and narrow layout passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
