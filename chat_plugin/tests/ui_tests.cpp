// SPDX-License-Identifier: GPL-2.0-or-later
#include "panel.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QProcess>
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
        QTemporaryDir dir;
        const auto file = dir.filePath("settings.ini");
        QSettings saved(file, QSettings::IniFormat);
        saved.setValue("model_id", model_id(ModelProfile::Heavy));
        saved.setValue("client_id", "legacy-unused-id");
        saved.sync();
        {
            Panel panel("missing-catalogue", dir.path(), file, nullptr, true);
            check(panel.findChild<QComboBox *>("awlChatResourceUsage")->currentIndex() == 1,
                  "Upgrade lost the selected model");
            check(panel.findChild<QCheckBox *>("interpretwitchEnabled")->isChecked(),
                  "Not enabled by default");
            check(panel.findChild<QCheckBox *>("interpretwitchShowOriginal")->isChecked(),
                  "Original messages must remain visible by default after upgrade");
            check(!panel.findChild<QWidget *>("awlTwitchSetup") &&
                      !panel.findChild<QPushButton *>("awlChatConnect"),
                  "Standalone authentication remains");
            auto targets = panel.findChild<QComboBox *>("interpretwitchTargetLanguage");
            auto profiles = panel.findChild<QComboBox *>("awlChatResourceUsage");
            check(targets->currentData() == "ru" && targets->count() == 100,
                  "Russian must be the default target");
            targets->setCurrentIndex(targets->findData("fr"));
            profiles->setCurrentIndex(0);
            check(targets->currentData() == "fr" && targets->count() == 5 &&
                      targets->findData("en") < 0,
                  "Light target selection/availability");
            profiles->setCurrentIndex(1);
            targets->setCurrentIndex(targets->findData("ja"));
            profiles->setCurrentIndex(0);
            check(targets->currentData() == "fr",
                  "Unsupported Light target must restore the last valid Light choice");
            panel.findChild<QCheckBox *>("interpretwitchShowOriginal")->setChecked(false);
            panel.findChild<QCheckBox *>("interpretwitchLanguageLabel")->setChecked(false);
            panel.findChild<QCheckBox *>("interpretwitchBackground")->setChecked(false);
            panel.findChild<QCheckBox *>("interpretwitchEnabled")->setChecked(false);
            panel.show();
            panel.close();
            check(!panel.isVisible(), "Settings cannot close");
        }
        Panel restored("missing-catalogue", dir.path(), file, nullptr, true);
        check(restored.findChild<QComboBox *>("interpretwitchTargetLanguage")->currentData() ==
                  "fr",
              "Target language was not saved");
        for (auto name : {"interpretwitchEnabled", "interpretwitchShowOriginal",
                          "interpretwitchLanguageLabel", "interpretwitchBackground"})
            check(!restored.findChild<QCheckBox *>(name)->isChecked(), "Preference was not saved");
        std::cout << "Bridge defaults, legacy model settings, appearance and disable persistence "
                     "passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
