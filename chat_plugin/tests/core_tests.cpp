// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_core.hpp"
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
        QString original = "Hello @bob Kappa https://example.test";
        QString rebuilt, protected_text;
        for (const auto &part : protect_text(original)) {
            rebuilt += part.text;
            if (part.protected_text)
                protected_text += part.text;
        }
        check(rebuilt == original && protected_text.contains("@bob") &&
                  protected_text.contains("Kappa") &&
                  protected_text.contains("https://example.test"),
              "Protected text changed");
        check(repetitive("hello hello hello hello hello hello") && !repetitive("Go go go!"),
              "Runaway repetition guard");
        JobQueue queue;
        QStringList discarded;
        const auto discard = [&](const Job &job) {
            discarded << job.message.id;
        };
        size_t drops = 0;
        for (int i = 0; i < 30; ++i)
            drops += queue.offer({Message{QString::number(i)}, 1, 1000}, discard);
        check(queue.size() == 24 && drops == 6 && discarded.size() == 6 &&
                  discarded.front() == "0" && discarded.back() == "5",
              "Queue memory bound");
        size_t expired = 0;
        check(!queue.take(17000, expired, discard) && expired == 24 && discarded.size() == 30 &&
                  discarded.back() == "29",
              "Stale translations left pending indicators");
        queue.offer({Message{"fresh"}, 1, 18000});
        check(queue.take(18001, expired)->message.id == "fresh", "Fresh job");
        queue.offer({Message{"cancelled"}, 1, 18000});
        queue.clear(discard);
        check(discarded.back() == "cancelled" && queue.size() == 0,
              "Queue clear left pending indicators");
        check(model_profile(model_id(ModelProfile::Light)) == ModelProfile::Light &&
                  model_profile(model_id(ModelProfile::Heavy)) == ModelProfile::Heavy &&
                  !model_profile("untrusted-model"),
              "Model identity allowlist");
        check(profile_manifest("/data/models.lock.json", ModelProfile::Light) ==
                      "/data/models.lock.json" &&
                  profile_manifest("/data/models.lock.json", ModelProfile::Heavy) ==
                      "/data/models-heavy.lock.json" &&
                  profile_directory("/cache", ModelProfile::Light) == "/cache" &&
                  profile_directory("/cache", ModelProfile::Heavy) == "/cache/m2m100-418m-int8",
              "Model cache separation");
        check(target_languages(ModelProfile::Heavy).size() == 100 &&
                  target_languages(ModelProfile::Light).size() == 5 &&
                  supports_target(ModelProfile::Light, "fr") &&
                  !supports_target(ModelProfile::Light, "en") &&
                  !supports_target(ModelProfile::Heavy, "../../bad"),
              "Target allowlist");
        check(profile_directory("/cache", ModelProfile::Heavy, "en") ==
                      profile_directory("/cache", ModelProfile::Heavy, "ru") &&
                  profile_directory("/cache", ModelProfile::Light, "fr") ==
                      "/cache/opus-mt-en-fr-int8" &&
                  profile_manifest("/data/models.lock.json", ModelProfile::Light, "fr") ==
                      "/data/models-en-fr.lock.json",
              "Heavy must reuse its model; Light pairs must be separate");
        check(
            translation_language("Русское сообщение", "ru", .99f, ModelProfile::Heavy, 0, "en") ==
                    "ru" &&
                translation_language("Hello everyone", "en", .99f, ModelProfile::Heavy, 0, "en")
                    .isEmpty() &&
                translation_language("hello", "en", .99f, ModelProfile::Heavy, 0, "en").isEmpty() &&
                translation_language("Bonjour tout le monde", "fr", .99f, ModelProfile::Heavy, 0,
                                     "fr")
                    .isEmpty(),
            "Target language filtering");
        for (auto profile : {ModelProfile::Light, ModelProfile::Heavy}) {
            check(translation_language("hello!", "unknown", .1f, profile) == "en" &&
                      translation_language("Hello everyone", "en", .9f, profile) == "en",
                  "English greeting/phrase regression");
            check(translation_language("Русское сообщение", "ru", .99f, profile).isEmpty() &&
                      translation_language("uncertain words", "en", .1f, profile).isEmpty() &&
                      translation_language("unknown words", "xx", .99f, profile).isEmpty() &&
                      translation_language("KEKW", "en", .99f, profile).isEmpty(),
                  "Russian, uncertain, unsupported or emote text translated");
            // Scores measured with lid.176.ftz for these messages.
            check(translation_language("On the left side of the road", "en", .590237f, profile,
                                       .135431f) == "en" &&
                      translation_language("I the left side of the road", "en", .590237f, profile,
                                           .135431f) == "en" &&
                      translation_language("move to the left", "en", .568439f, profile,
                                           .0734135f) == "en",
                  "Clear English lead below .60 was skipped");
            check(translation_language("Every now and then I fall apart", "en", .477f, profile,
                                       .031f) == "en" &&
                      translation_language("omg so close", "en", .531f, profile, .038f) == "en",
                  "Decisive low-score English lead rejected");
            check(translation_language("close call words", "en", .45f, profile, .1f).isEmpty() &&
                      translation_language("split language vote", "en", .55f, profile, .25f)
                          .isEmpty() &&
                      translation_language("faint lead words", "en", .35f, profile, .001f)
                          .isEmpty() &&
                      translation_language("lets go team", "it", .402f, profile, .326f).isEmpty(),
                  "Narrow or weak detector lead translated");
        }
        check(translation_language("hola a todos", "pt", .54f, ModelProfile::Heavy, .43f).isEmpty(),
              "Ambiguous Portuguese/Spanish message translated");
        check(translation_language("Bonjour tout le monde", "fr", .99f, ModelProfile::Light)
                      .isEmpty() &&
                  translation_language("Bonjour tout le monde", "fr", .99f, ModelProfile::Heavy) ==
                      "fr" &&
                  translation_language("Зачекайте, будь ласка", "uk", .99f, ModelProfile::Heavy) ==
                      "uk" &&
                  translation_language("请等待所有玩家准备好", "zh", .99f, ModelProfile::Heavy) ==
                      "zh",
              "Multilingual routing rejected supported scripts");

        std::cout << "Text protection, queue bounds and language routing passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
