// SPDX-License-Identifier: GPL-2.0-or-later
// Test-only child executable. Never installed or linked into the real helper.
#include "ipc.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
using namespace awl::chat;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    QString mode;
    int starts = 0;
    for (std::string line; std::getline(std::cin, line);) {
        const auto command = ipc::decode(QByteArray::fromStdString(line));
        const auto type = command["type"].toString(), id = command["id"].toString();
        auto reply = [&](QJsonObject value) {
            value["id"] = id;
            if (type == "prepare") {
                value["target_language"] =
                    mode == "wrong-target" ? QJsonValue("xx") : command["target_language"];
                value["model_id"] =
                    mode == "wrong-model" ? QJsonValue("wrong") : command["model_id"];
            }
            const auto bytes = ipc::encode(value);
            std::cout.write(bytes.data(), bytes.size());
            std::cout.flush();
        };
        if (type == "prepare") {
            mode = QFileInfo(command["manifest"].toString()).fileName();
            QFile count(QDir(command["directory"].toString()).filePath("starts"));
            if (count.open(QIODevice::ReadOnly)) {
                starts = count.readAll().toInt();
                count.close();
            }
            if (!count.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return 2;
            count.write(QByteArray::number(++starts));
            count.close();
            if (mode == "load-hang")
                for (;;)
                    std::this_thread::sleep_for(std::chrono::seconds(30));
            if (mode == "stderr") {
                std::cerr << std::string(1024 * 1024, 'x');
                std::cerr.flush();
            }
            if (mode == "status-flood")
                for (;;)
                    reply({{"type", "status"}, {"text", "Still loading"}});
            QJsonObject ready{{"type", "ready"},
                              {"ok", mode != "unavailable"},
                              {"device", command["device"]},
                              {"fallback", ""}};
            // GPU requested without cuBLAS: CPU with a "libraries" issue until the
            // supervisor asks for the GPU pack download.
            if (mode == "gpu-libraries" && command["device"] == "gpu") {
                if (command["gpu_download"].toBool()) {
                    QFile marker(QDir(command["directory"].toString()).filePath("gpu-download"));
                    if (!marker.open(QIODevice::WriteOnly))
                        return 2;
                } else {
                    ready["device"] = "cpu";
                    ready["gpu_issue"] = "libraries";
                    ready["fallback"] = "GPU support needs a one-time download.";
                }
            }
            reply(ready);
        } else if (type == "clear")
            reply({{"type", "cleared"}});
        else if (type == "cancel")
            continue;
        else if (type == "translate") {
            if (mode == "crash-always" || (mode == "crash-once" && starts == 1)) {
                std::raise(SIGSEGV);
                return 3;
            }
            if (mode == "hang")
                for (;;)
                    std::this_thread::sleep_for(std::chrono::seconds(30));
            if (mode == "oversize") {
                std::cout << std::string(131072, 'x');
                std::cout.flush();
                continue;
            }
            if (mode == "bad-json") {
                std::cout << "garbage\n" << std::flush;
                continue;
            }
            if (mode == "wrong-id") {
                std::cout << ipc::encode({{"type", "result"}, {"id", "wrong"}}).constData()
                          << std::flush;
                continue;
            }
            if (mode == "late")
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            reply({{"type", "result"},
                   {"text", "Перевод"},
                   {"translated", true},
                   {"source_language", mode == "bad-language" ? "<script>" : "en"}});
        }
    }
}
