// SPDX-License-Identifier: GPL-2.0-or-later
#include "short_replies.hpp"
#include "model_profile.hpp"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
using namespace awl::chat;
static void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        check(short_reply("Yes", "ru") == "Да" && short_reply("No", "ru") == "Нет" &&
                  short_reply("Bye", "ru") == "Пока" && short_reply("Thanks", "ru") == "Спасибо",
              "Known model errors were not corrected");
        check(short_reply("YES!", "fr") == "Oui!" && short_reply("no?", "de") == "Nein?" &&
                  short_reply("Yes", "es") == "Sí" && short_reply("Bye", "uk") == "Бувай",
              "Target language or punctuation changed");
        check(short_reply(" \tYeP?!…\n", "ru") == " \tДа?!…\n" &&
                  short_reply("thank you...", "ru") == "Спасибо...",
              "Case, whitespace or punctuation handling failed");
        for (const auto &target : target_languages(ModelProfile::Light))
            for (const auto &text :
                 {"yes", "yeah", "yep", "no", "nope", "hello", "hi", "hey", "bye", "thanks",
                  "thank you", "ok", "okay", "sorry", "please"}) {
                check(!short_reply(text, target).isEmpty(), "Missing common reply translation");
                for (auto profile : {ModelProfile::Light, ModelProfile::Heavy})
                    check(translation_language(text, "", 0.f, profile, 0.f, target) == "en",
                          "Reply correction is unreachable through language routing");
            }
        for (const auto &text : {"", " ", "yes but wait", "not yes", "yesterday", "no way",
                                 "thank you everyone", "right", "gift", "chat", "Yes 🙂",
                                 "Yes @alice", "Kappa", "Да", "oui", "да yes", "yes\nno"})
            check(short_reply(text, "ru").isEmpty(), "Changed more than a whole English reply");
        for (const auto &target : {"en", "ja", "unknown", ""})
            check(short_reply("Yes", target).isEmpty(),
                  "Unsupported target must use normal routing");
        std::cout << "Short replies, language routing and sentence boundaries passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
