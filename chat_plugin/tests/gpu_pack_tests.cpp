// SPDX-License-Identifier: GPL-2.0-or-later
// Optional NVIDIA cuBLAS pack: catalogue validation, verified download and
// extraction, publication, reuse and cancellation. Uses a local fixture; no
// network, NVIDIA download or CUDA device is needed.
#include "gpu_runtime.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace awl::chat;
static void check(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
static QString sha256(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Cannot read extracted file");
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}
static QString error_of(const std::function<void()> &run) {
    try {
        run();
    } catch (const std::exception &error) {
        return QString::fromUtf8(error.what());
    }
    return {};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        check(argc == 2, "usage: awl-chat-gpu-tests FIXTURE_DIRECTORY");
        const QDir fixture(QString::fromLocal8Bit(argv[1]));
        const auto manifest = fixture.filePath("manifest.json");
        QFile index(manifest);
        check(index.open(QIODevice::ReadOnly), "Missing fixture catalogue");
        const auto catalogue = QJsonDocument::fromJson(index.readAll()).object();
        int fetches = 0;
        const Fetch local = [&](const std::string &url, const awl::DownloadSink &sink,
                                const std::atomic<bool> &) {
            ++fetches;
            check(QString::fromStdString(url) == catalogue["archive"].toObject()["url"].toString(),
                  "Fetched an address outside the catalogue");
            QFile archive(fixture.filePath("archive.zip"));
            check(archive.open(QIODevice::ReadOnly), "Missing fixture archive");
            while (!archive.atEnd()) {
                const auto chunk = archive.read(65536);
                sink(chunk.constData(), size_t(chunk.size()));
            }
        };
        const std::atomic<bool> running{false};
        QStringList statuses;
        const Status status = [&](QString text) {
            statuses << text;
        };
        // Variants are written next to a fresh root so failures cannot publish.
        QTemporaryDir work;
        const auto variant = [&](const std::function<void(QJsonObject &)> &edit) {
            auto copy = catalogue;
            edit(copy);
            const auto path = QDir(work.path()).filePath(QString("variant-%1.json").arg(fetches));
            QFile file(path);
            check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot write variant");
            file.write(QJsonDocument(copy).toJson());
            return path;
        };
        const auto edit_file = [](QJsonObject &copy, int index, const char *key, QJsonValue value) {
            auto files = copy["files"].toArray();
            auto file = files[index].toObject();
            file[key] = value;
            files[index] = file;
            copy["files"] = files;
        };

        QTemporaryDir root;
        check(installed_gpu_pack(manifest, root.path()).isEmpty(), "Empty root reported a pack");
        const auto directory = install_gpu_pack(manifest, root.path(), running, status, local);
        const QDir pack(directory);
        check(!directory.isEmpty() && fetches == 1 && directory.endsWith("cublas-test-1"),
              "Install did not publish the versioned pack");
        for (const auto &value : catalogue["files"].toArray()) {
            const auto file = value.toObject();
            check(sha256(pack.filePath(file["filename"].toString())) == file["sha256"].toString(),
                  "Extracted file does not match the catalogue");
        }
        check(!pack.exists("nvblas64_12.dll") && pack.exists("pack.json") &&
                  !QFile::exists(directory + ".download") && !QDir(directory + ".partial").exists(),
              "Unapproved or temporary files remain");
        check(statuses.filter("GPU support").size() >= 3, "Download/install progress missing");
        check(installed_gpu_pack(manifest, root.path()) == directory &&
                  install_gpu_pack(manifest, root.path(), running, status, local) == directory &&
                  fetches == 1,
              "Installed pack was downloaded again");
        {
            QFile truncated(pack.filePath("cublasLt64_12.dll"));
            check(truncated.open(QIODevice::ReadWrite) && truncated.resize(10),
                  "Cannot tamper fixture");
        }
        check(installed_gpu_pack(manifest, root.path()).isEmpty(),
              "Damaged pack still reported complete");

        const auto failure = [&](const QString &path, const char *expected, int fetched) {
            QTemporaryDir fresh;
            const int before = fetches;
            const auto message = error_of([&] {
                install_gpu_pack(path, fresh.path(), running, status, local);
            });
            check(message == expected, expected);
            check(fetches - before == fetched, "Unexpected download for an invalid catalogue");
            check(QDir(fresh.path())
                      .entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden)
                      .isEmpty(),
                  "Failed install left files behind");
        };
        const char *verification = "GPU support download failed verification. Try again.";
        const char *invalid = "Invalid GPU support catalogue.";
        failure(variant([](QJsonObject &c) {
                    auto archive = c["archive"].toObject();
                    archive["sha256"] = QString(64, 'a');
                    c["archive"] = archive;
                }),
                verification, 1);
        failure(variant([&](QJsonObject &c) {
                    edit_file(c, 0, "sha256", QString(64, 'b'));
                }),
                verification, 1);
        failure(variant([&](QJsonObject &c) {
                    edit_file(c, 1, "entry", "pkg/bin/missing.dll");
                }),
                verification, 1);
        failure(variant([](QJsonObject &c) {
                    auto archive = c["archive"].toObject();
                    archive["url"] = "http://developer.download.nvidia.com/x.zip";
                    c["archive"] = archive;
                }),
                invalid, 0);
        failure(variant([](QJsonObject &c) {
                    auto archive = c["archive"].toObject();
                    archive["url"] = "https://example.com/compute/cuda/x.zip";
                    c["archive"] = archive;
                }),
                invalid, 0);
        for (const char *name : {"..\\evil.dll", "C:evil.dll", "a/b.dll", "pack.json", ".hidden"})
            failure(variant([&](QJsonObject &c) {
                        edit_file(c, 0, "filename", name);
                    }),
                    invalid, 0);
        failure(variant([](QJsonObject &c) {
                    c["id"] = "../escape";
                }),
                invalid, 0);
        {
            QTemporaryDir fresh;
            std::atomic<bool> cancel{false};
            const Fetch cancelling = [&](const std::string &url, const awl::DownloadSink &sink,
                                         const std::atomic<bool> &flag) {
                local(
                    url,
                    [&](const char *data, size_t length) {
                        cancel = true;
                        sink(data, length);
                    },
                    flag);
            };
            const auto message = error_of([&] {
                check(
                    install_gpu_pack(manifest, fresh.path(), cancel, status, cancelling).isEmpty(),
                    "Cancelled install published a pack");
            });
            check((message.isEmpty() || message == "Download cancelled.") &&
                      installed_gpu_pack(manifest, fresh.path()).isEmpty() &&
                      QDir(fresh.path())
                          .entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden)
                          .isEmpty(),
                  "Cancelled install left a pack or partial files");
        }

        {
            QTemporaryDir fresh;
            const auto legacy = QDir(fresh.path()).filePath("cublas-test-1.download");
            QFile sentinel(legacy);
            check(sentinel.open(QIODevice::WriteOnly), "Cannot create concurrent-download fixture");
            sentinel.write("another attempt");
            sentinel.close();
            install_gpu_pack(manifest, fresh.path(), running, status, local);
            check(sentinel.open(QIODevice::ReadOnly) && sentinel.readAll() == "another attempt",
                  "Install overwrote another attempt's temporary file");
            check(QDir(fresh.path())
                      .entryList({".gpu-install-*"}, QDir::Dirs | QDir::Hidden)
                      .isEmpty(),
                  "Successful install left temporary files");
        }

        check(classify_gpu_error("CUDA failed with error out of memory") == GpuIssue::Memory &&
                  classify_gpu_error("CUBLAS_STATUS_ALLOC_FAILED") == GpuIssue::Memory &&
                  classify_gpu_error("Library cublas64_12.dll is not found or cannot be loaded") ==
                      GpuIssue::Libraries &&
                  classify_gpu_error("CUDA driver version is insufficient for CUDA runtime "
                                     "version") == GpuIssue::OldDriver &&
                  classify_gpu_error("No compatible NVIDIA GPU is available.") ==
                      GpuIssue::NoDevice &&
                  classify_gpu_error("something new") == GpuIssue::Unknown,
              "GPU error classification");
        for (int i = 0; i <= int(GpuIssue::Unknown); ++i) {
            const auto issue = GpuIssue(i);
            check(gpu_issue_code(issue).isEmpty() == (issue == GpuIssue::None) &&
                      gpu_issue_text(issue).isEmpty() == (issue == GpuIssue::None),
                  "GPU issue codes/text");
        }
        check(gpu_issue_text(GpuIssue::OldDriver, 11060).contains("11.6"),
              "GPU issue validation/driver version");
        std::cout << "GPU pack: verified download, approved-only extraction (incl. ZIP64), "
                     "reuse, damage, tampered/hostile catalogues, cancellation and error "
                     "classification passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
