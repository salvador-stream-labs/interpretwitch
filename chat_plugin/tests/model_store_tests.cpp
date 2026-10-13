// SPDX-License-Identifier: GPL-2.0-or-later
#include "model_files.hpp"
#include "worker.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace awl::chat;
static void check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        const QByteArray bytes("verified model fixture");
        QJsonArray files;
        const QStringList names{"config.json", "lid.176.ftz", "model.bin", "shared_vocabulary.json",
                                "source.spm",  "target.spm"};
        for (const auto &name : names)
            files.append(QJsonObject{
                {"filename", name},
                {"size", bytes.size()},
                {"sha256",
                 QString::fromLatin1(
                     QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
                {"url", "https://huggingface.co/example/model"}});
        const auto manifest = temp.filePath("models.json"), cache = temp.filePath("cache");
        QFile catalogue(manifest);
        check(catalogue.open(QIODevice::WriteOnly), "Fixture manifest open");
        catalogue.write(QJsonDocument(QJsonObject{{"schema", 1}, {"files", files}}).toJson());
        catalogue.close();
        std::atomic<bool> cancel{false};
        int calls = 0;
        Fetch fetch = [&](const auto &, const auto &sink, const auto &) {
            ++calls;
            sink(bytes.data(), bytes.size());
        };
        check(!prepare_models(
                  manifest, cache, false, cancel, [](auto) {}, fetch) &&
                  calls == 0,
              "Download without consent");
        check(prepare_models(
                  manifest, cache, true, cancel, [](auto) {}, fetch) &&
                  calls == 6,
              "Verified initial download");
        check(prepare_models(
                  manifest, cache, true, cancel, [](auto) {}, fetch) &&
                  calls == 6,
              "Valid cache redownloaded");
        QFile broken(QDir(cache).filePath("model.bin"));
        check(broken.open(QIODevice::WriteOnly), "Fixture corruption open");
        broken.write(QByteArray(bytes.size(), 'x'));
        broken.close();
        check(!prepare_models(
                  manifest, cache, false, cancel, [](auto) {}, fetch),
              "Same-size corruption accepted");
        check(prepare_models(
                  manifest, cache, true, cancel, [](auto) {}, fetch) &&
                  calls == 7,
              "Corrupt cache not repaired");
        QFile::remove(QDir(cache).filePath("model.bin"));
        bool failed = false;
        try {
            prepare_models(
                manifest, cache, true, cancel, [](auto) {},
                [&](const auto &, const auto &sink, const auto &) {
                    const QByteArray excess(1000, 'x');
                    sink(excess.data(), excess.size());
                });
        } catch (...) {
            failed = true;
        }
        check(failed && !QFile::exists(QDir(cache).filePath("model.bin")),
              "Oversized download promoted");
        failed = false;
        try {
            prepare_models(
                manifest, cache, true, cancel, [](auto) {},
                [&](const auto &, const auto &sink, const auto &) {
                    const QByteArray wrong(bytes.size(), 'x');
                    sink(wrong.data(), wrong.size());
                });
        } catch (...) {
            failed = true;
        }
        check(failed && !QFile::exists(QDir(cache).filePath("model.bin")), "Bad hash promoted");
        failed = false;
        try {
            prepare_models(
                manifest, cache, true, cancel, [](auto) {},
                [&](const auto &, const auto &sink, const auto &) {
                    sink(bytes.data(), 4);
                    cancel = true;
                    sink(bytes.data() + 4, bytes.size() - 4);
                });
        } catch (...) {
            failed = true;
        }
        check(failed && !QFile::exists(QDir(cache).filePath("model.bin")),
              "Cancelled partial download promoted");
        check(!prepare_models(
                  manifest, cache, true, cancel, [](auto) {}, fetch),
              "Cancellation ignored");
        check(QDir(cache).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size() == 5,
              "Failed downloads left partial files");
        cancel = false;
        const auto reject = [&](const QString &field, QJsonValue value) {
            auto invalid = files;
            auto last = invalid.last().toObject();
            last[field] = value;
            invalid[5] = last;
            check(catalogue.open(QIODevice::WriteOnly | QIODevice::Truncate),
                  "Invalid catalogue open");
            catalogue.write(QJsonDocument(QJsonObject{{"schema", 1}, {"files", invalid}}).toJson());
            catalogue.close();
            int fetched = 0;
            bool rejected = false;
            const auto untouched = temp.filePath("untouched");
            try {
                prepare_models(
                    manifest, untouched, true, cancel, [](auto) {},
                    [&](const auto &, const auto &, const auto &) {
                        ++fetched;
                    });
            } catch (...) {
                rejected = true;
            }
            check(rejected && !fetched && !QDir(untouched).exists(),
                  "Invalid catalogue caused filesystem or network side effects");
        };
        for (const auto &name : QStringList{"../escape", "C:escape", "model.bin:stream",
                                            "config.json", "unexpected.dll"})
            reject("filename", name);
        reject("size", 0);
        reject("size", -1);
        reject("size", 1.5);
        reject("size", 300 * 1024 * 1024);
        reject("sha256", "not-a-checksum");
        reject("url", "http://huggingface.co/model");
        reject("url", "https://evil.test/model");
        reject("url", "https://token@huggingface.co/model");
        reject("url", "https://huggingface.co:444/model");
        check(catalogue.open(QIODevice::WriteOnly | QIODevice::Truncate), "Restore catalogue open");
        catalogue.write(QJsonDocument(QJsonObject{{"schema", 1}, {"files", files}}).toJson());
        catalogue.close();
#ifndef _WIN32
        const auto outside = temp.filePath("outside-model");
        QFile external(outside);
        check(external.open(QIODevice::WriteOnly), "External fixture open");
        external.write(bytes);
        external.close();
        check(QFile::link(outside, QDir(cache).filePath("model.bin")), "Symlink fixture creation");
        failed = false;
        try {
            prepare_models(manifest, cache, true, cancel, [](auto) {}, fetch);
        } catch (...) {
            failed = true;
        }
        check(failed && external.open(QIODevice::ReadOnly) && external.readAll() == bytes,
              "Linked model accepted or external file changed");
#endif
        // Heavy has a distinct exact file set, identity and bounded cache.
        auto heavy_files = files;
        heavy_files.removeLast();
        auto tokenizer = heavy_files[4].toObject();
        tokenizer["filename"] = "sentencepiece.bpe.model";
        heavy_files[4] = tokenizer;
        const auto heavy_manifest = temp.filePath("models-heavy.lock.json");
        const auto heavy_cache = profile_directory(cache, ModelProfile::Heavy);
        auto write_heavy = [&](QJsonArray assets, QString id = "m2m100-418m-int8") {
            QFile file(heavy_manifest);
            check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Heavy catalogue open");
            file.write(
                QJsonDocument(QJsonObject{{"schema", 1}, {"model_id", id}, {"files", assets}})
                    .toJson());
        };
        write_heavy(heavy_files);
        const auto before_heavy = calls;
        check(!prepare_models(
                  heavy_manifest, heavy_cache, false, cancel, [](auto) {}, fetch,
                  ModelProfile::Heavy) &&
                  calls == before_heavy,
              "Heavy model downloaded without explicit request");
        check(prepare_models(
                  heavy_manifest, heavy_cache, true, cancel, [](auto) {}, fetch,
                  ModelProfile::Heavy) &&
                  calls == before_heavy + 5,
              "Heavy model did not load its five verified files");
        check(prepare_models(
                  heavy_manifest, heavy_cache, true, cancel, [](auto) {}, fetch,
                  ModelProfile::Heavy) &&
                  calls == before_heavy + 5,
              "Heavy cache redownloaded");
        auto reject_heavy = [&](QJsonArray assets, QString id = "m2m100-418m-int8") {
            write_heavy(assets, id);
            bool rejected = false;
            int fetched = 0;
            const auto untouched = temp.filePath("heavy-untouched");
            try {
                prepare_models(
                    heavy_manifest, untouched, true, cancel, [](auto) {},
                    [&](const auto &, const auto &, const auto &) {
                        ++fetched;
                    },
                    ModelProfile::Heavy);
            } catch (...) {
                rejected = true;
            }
            check(rejected && !fetched && !QDir(untouched).exists(),
                  "Invalid Heavy catalogue had side effects");
        };
        reject_heavy(heavy_files, "opus-mt-en-ru-int8");
        reject_heavy(files);
        auto oversized = heavy_files;
        auto weight = oversized[2].toObject();
        weight["size"] = 512 * 1024 * 1024; // Total exceeds the cap even though this file fits.
        oversized[2] = weight;
        reject_heavy(oversized);
        weight["size"] = 513 * 1024 * 1024;
        oversized[2] = weight;
        reject_heavy(oversized);
        // Exercise the idle wait/shutdown boundary without model or network I/O.
        for (int i = 0; i < 500; ++i) {
            Worker idle("unused", "unused");
            QThread::yieldCurrentThread();
        }
        std::cout << "Model store: explicit download, cache/hash verification, atomic repair, size "
                     "limits and cancellation passed.\n";
        std::cout << "Invalid catalogues (paths, duplicate roles, size/hash/origin errors) "
                     "rejected before any writes or downloads.\n";
        std::cout << "Idle worker creation/shutdown stress passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
