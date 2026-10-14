// SPDX-License-Identifier: GPL-2.0-or-later
#include "limits.hpp"
#include "ipc.hpp"
#include "model.hpp"
#include "model_files.hpp"
#include "ui_text.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <sys/resource.h>
#endif

using namespace awl::chat;
namespace {
struct Command {
    QJsonObject value;
    quint64 generation;
};
struct Input {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Command> commands;
    std::atomic<quint64> generation{0};
};
void output(QJsonObject value) {
    const auto bytes = ipc::encode(value);
    if (std::fwrite(bytes.data(), 1, size_t(bytes.size()), stdout) != size_t(bytes.size()) ||
        std::fflush(stdout))
        std::_Exit(2);
}
void read_commands(Input &input) {
    QByteArray line;
    for (;;) {
        const int byte = std::getchar();
        // Parent death/closed stdin terminates even if inference is stuck. This
        // process owns no OBS state. Never leave an orphan decoding indefinitely.
        if (byte == EOF)
            std::_Exit(0);
        if (byte != '\n') {
            line += char(byte);
            if (line.size() >= ipc::frame_limit)
                std::_Exit(2);
            continue;
        }
        try {
            auto value = ipc::decode(line);
            line.clear();
            if (value["type"].toString() == "cancel") {
                ++input.generation;
                continue;
            }
            const auto id = value["id"].toString();
            if (id.isEmpty() || id.size() > 32)
                std::_Exit(2);
            {
                std::lock_guard lock(input.mutex);
                if (input.commands.size() >= 2)
                    std::_Exit(2);
                input.commands.push_back({std::move(value), input.generation.load()});
            }
            input.wake.notify_one();
        } catch (...) {
            std::_Exit(2);
        }
    }
}
bool constrain_process() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // The job is private to this helper, never the OBS process. Limit committed
    // host memory to 4 GiB and disallow child processes. This does not cap VRAM.
    // Loading Heavy on CPU peaks near 2 GiB, so a 2 GiB ceiling rejects it.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    limit.BasicLimitInformation.ActiveProcessLimit = 1;
    limit.ProcessMemoryLimit = SIZE_T(4) * 1024 * 1024 * 1024;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof(limit)) ||
        !AssignProcessToJobObject(job, GetCurrentProcess())) {
        CloseHandle(job);
        return false;
    }
    // Keep this handle until process exit; closing it terminates this job.
#else
    struct rlimit core{0, 0};
    if (setrlimit(RLIMIT_CORE, &core) != 0)
        return false;
#endif
    return true;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2 || QString::fromUtf8(argv[1]) != "--stdio" || !constrain_process())
        return 2;
    Input input;
    std::thread reader([&] {
        read_commands(input);
    });
    reader.detach();
    std::unique_ptr<Translator> translator;
    bool prepared = false;
    for (;;) {
        Command command;
        {
            std::unique_lock lock(input.mutex);
            input.wake.wait(lock, [&] {
                return !input.commands.empty();
            });
            command = std::move(input.commands.front());
            input.commands.pop_front();
        }
        const auto id = command.value["id"].toString(), type = command.value["type"].toString();
        auto say = [&](QString text) {
            output(
                {{"type", "status"}, {"id", id}, {"text", text.left(limits::status_characters)}});
        };
        try {
            if (type == "prepare" && !prepared) {
                const auto locale = command.value["locale"].toString("en-US");
                if (locale != "en-US" && locale != "ru-RU")
                    std::_Exit(2);
                set_ui_locale(locale);
                prepared = true;
                const auto manifest = command.value["manifest"].toString();
                const auto directory = command.value["directory"].toString();
                const auto device = command.value["device"].toString();
                const auto target = command.value["target_language"].toString("ru");
                const auto profile = model_profile(
                    command.value["model_id"].toString(model_id(ModelProfile::Light)));
                if (!QFileInfo(manifest).isAbsolute() || !QFileInfo(directory).isAbsolute() ||
                    manifest.size() > 32768 || directory.size() > 32768 ||
                    (device != "cpu" && device != "gpu") || !command.value["download"].isBool() ||
                    !profile || !supports_target(*profile, target) ||
                    command.value["model_id"].toString(model_id(ModelProfile::Light)) !=
                        model_id(*profile, target))
                    std::_Exit(2);
                RunOn actual = device == "gpu" ? RunOn::GPU : RunOn::CPU;
                QString fallback;
                try {
                    const std::atomic<bool> cancel{false};
                    if (prepare_models(manifest, directory, command.value["download"].toBool(),
                                       cancel, say, {}, *profile, target)) {
                        say(ui_text("Loading local translation · %1…").arg(run_on_name(actual)));
                        try {
                            translator =
                                std::make_unique<Translator>(directory, actual, *profile, target);
                        } catch (const std::exception &error) {
                            if (actual != RunOn::GPU)
                                throw;
                            fallback =
                                ui_error(error.what(), "GPU initialization failed. Using CPU.")
                                    .left(limits::status_characters);
                            say(ui_text("GPU could not start. Loading CPU translation…"));
                            translator = std::make_unique<Translator>(directory, RunOn::CPU,
                                                                      *profile, target);
                            actual = RunOn::CPU;
                        }
                    } else
                        say(ui_text("Download the selected translation model to get started."));
                } catch (const std::exception &error) {
                    say(ui_error(error.what(), "Could not prepare translation. Check your "
                                               "connection and model files, then try again."));
                }
                output({{"type", "ready"},
                        {"id", id},
                        {"ok", bool(translator)},
                        {"device", actual == RunOn::GPU ? "gpu" : "cpu"},
                        {"model_id", model_id(*profile, target)},
                        {"target_language", target},
                        {"fallback", fallback}});
            } else if (type == "translate" && translator) {
                const auto message = ipc::message(command.value["message"].toObject());
                const auto started = std::chrono::steady_clock::now();
                // Unexpected inference errors terminate this helper and reach the
                // parent's bounded recovery, rather than silently claiming health.
                const auto translation = translator->translate(message, [&] {
                    return command.generation != input.generation ||
                           std::chrono::steady_clock::now() - started >
                               std::chrono::milliseconds(limits::translation_age_ms);
                });
                QJsonObject reply{{"type", "result"},
                                  {"id", id},
                                  {"text", translation.text},
                                  {"translated", translation.translated},
                                  {"source_language", translation.source_language}};
                if (translation.text.size() > limits::translation_characters ||
                    QJsonDocument(reply).toJson(QJsonDocument::Compact).size() >
                        ipc::frame_limit - 32) {
                    reply["text"] = message.original;
                    reply["translated"] = false;
                }
                output(reply);
            } else if (type == "clear" && translator) {
                translator->clear_cache();
                output({{"type", "cleared"}, {"id", id}});
            } else
                std::_Exit(2);
        } catch (...) {
            std::_Exit(2);
        }
    }
}
