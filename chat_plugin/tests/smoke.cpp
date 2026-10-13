// SPDX-License-Identifier: GPL-2.0-or-later
#include "model.hpp"
#include "model_files.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <iostream>
#ifndef _WIN32
#include <sys/resource.h>
#endif
using namespace awl::chat;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: awl-chat-smoke MANIFEST MODEL_DIRECTORY [MODEL_ID]\n";
        return 2;
    }
    try {
        const auto profile = argc == 4 ? model_profile(argv[3]) : ModelProfile::Light;
        if (!profile)
            throw std::runtime_error("Unknown model ID");
        std::atomic<bool> cancel{false};
        if (!prepare_models(argv[1], argv[2], false, cancel, [](auto) {}, {}, *profile))
            throw std::runtime_error("Model verification failed");
        QElapsedTimer timer;
        timer.start();
        Translator translator(argv[2], RunOn::CPU, *profile);
        QJsonObject report{{"device", "CPU INT8"},
                           {"model_id", model_id(*profile)},
                           {"threads", *profile == ModelProfile::Heavy ? 1 : 2},
                           {"load_ms", double(timer.elapsed())}};
        QJsonArray rows;
        for (const auto &text : QStringList{
                 "Hello everyone!", "Please move to the left and wait for the tank.",
                 "Do not pull the boss yet.", "We have 15 seconds before the next phase.",
                 "Thank you @alice Kappa", "Привет, как дела?", "Bonjour tout le monde",
                 "hola amigos como estan", "KEKW", "Visit https://example.com after the stream",
                 "hello", "lol", "Bitte wartet auf den Tank, bevor ihr den Boss angreift.",
                 "Почекайте, будь ласка, поки всі гравці будуть готові.",
                 "请等所有玩家准备好后再开始战斗。", "شكرا جزيلا لكم على مشاهدة البث"}) {
            Message m;
            m.original = text;
            m.parts = protect_text(text);
            timer.restart();
#ifndef _WIN32
            struct rusage before{};
            getrusage(RUSAGE_SELF, &before);
#endif
            auto value = translator.translate(m, [] {
                return false;
            });
            QJsonObject row{{"input", text},
                            {"output", value.text},
                            {"translated", value.translated},
                            {"uncertain", value.uncertain},
                            {"ms", double(timer.nsecsElapsed()) / 1e6}};
#ifndef _WIN32
            struct rusage after{};
            getrusage(RUSAGE_SELF, &after);
            const auto ms = [](timeval t) {
                return double(t.tv_sec) * 1000 + t.tv_usec / 1000.;
            };
            row["cpu_ms"] =
                ms(after.ru_utime) + ms(after.ru_stime) - ms(before.ru_utime) - ms(before.ru_stime);
#endif
            rows.append(row);
            if (text == "Hello everyone!" && !value.translated)
                throw std::runtime_error("English translation failed");
            if (*profile == ModelProfile::Heavy &&
                (text.startsWith("Bonjour") || text.startsWith("Bitte")) && !value.translated)
                throw std::runtime_error("Multilingual translation failed");
            if (text.startsWith("Привет") && value.translated)
                throw std::runtime_error("Russian was retranslated");
            if (text == "KEKW" && value.text != text)
                throw std::runtime_error("Emote was changed");
            if (text.contains("@alice") &&
                (!value.text.contains("@alice") || !value.text.contains("Kappa")))
                throw std::runtime_error("Protected text lost");
        }
        report["messages"] = rows;
#ifndef _WIN32
        struct rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        report["peak_rss_native_units"] = double(usage.ru_maxrss);
#ifdef __APPLE__
        report["peak_rss_unit"] = "bytes";
#else
        report["peak_rss_unit"] = "KiB";
#endif
#endif
        report["limitation"] =
            "Standalone CPU text translation, not total OBS/gameplay memory or live Twitch "
            "latency. Synthetic examples, no native-speaker accuracy assessment.";
        std::cout << QJsonDocument(report).toJson().constData();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
