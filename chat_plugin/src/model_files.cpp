// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui_text.hpp"
#include "model_files.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <cmath>
#include <stdexcept>

namespace awl::chat {
bool prepare_models(const QString &manifest, const QString &directory, bool download,
                    const std::atomic<bool> &cancel, const Status &status, const Fetch &fetch,
                    ModelProfile profile) {
    QFile index(manifest);
    if (!index.open(QIODevice::ReadOnly))
        throw std::runtime_error("Model catalogue is missing. Reinstall Awl Interpretwitch.");
    const auto bytes = index.read(65537);
    const auto catalogue = QJsonDocument::fromJson(bytes).object();
    const auto files = catalogue["files"].toArray();
    QSet<QString> required{"config.json", "lid.176.ftz", "model.bin", "shared_vocabulary.json",
                           "source.spm",  "target.spm"};
    const bool heavy = profile == ModelProfile::Heavy;
    if (heavy)
        required = {"config.json", "lid.176.ftz", "model.bin", "shared_vocabulary.json",
                    "sentencepiece.bpe.model"};
    const qint64 size_limit = (heavy ? 512LL : 256LL) * 1024 * 1024;
    if (catalogue["model_id"].toString(model_id(ModelProfile::Light)) != model_id(profile))
        throw std::runtime_error("Invalid model catalogue.");
    if (bytes.size() > 65536 || catalogue["schema"].toInt() != 1 || files.size() != required.size())
        throw std::runtime_error("Invalid model catalogue.");
    static const QRegularExpression sha256("^[0-9a-f]{64}$");
    qint64 total_size = 0;
    // Validate the entire plan before making directories, writing or fetching.
    // Exact names also rule out Windows drive paths, ADS and reserved filenames.
    for (const auto &value : files) {
        const auto asset = value.toObject();
        const double size = asset["size"].toDouble();
        const QUrl url(asset["url"].toString(), QUrl::StrictMode);
        if (!required.remove(asset["filename"].toString()) || size < 1 || size > size_limit ||
            std::floor(size) != size || !sha256.match(asset["sha256"].toString()).hasMatch() ||
            !url.isValid() || url.scheme() != "https" || !url.userInfo().isEmpty() ||
            url.hasFragment() || (url.port() != -1 && url.port() != 443) ||
            (url.host() != "huggingface.co" && url.host() != "dl.fbaipublicfiles.com"))
            throw std::runtime_error("Invalid model catalogue.");
        total_size += qint64(size);
    }
    if (total_size > size_limit)
        throw std::runtime_error("Model catalogue exceeds the download limit.");
    if (cancel)
        return false;
    QDir dir(directory);
    if (QFileInfo(directory).isSymLink())
        throw std::runtime_error("Model cache must not be a symbolic link.");
    if (!dir.exists() && !QDir().mkpath(directory))
        throw std::runtime_error("Cannot create model cache.");
    for (const auto &value : files) {
        if (cancel)
            return false;
        const auto asset = value.toObject();
        const auto name = asset["filename"].toString();
        if (QFileInfo(dir.filePath(name)).isSymLink())
            throw std::runtime_error("Model files must not be symbolic links.");
        const auto size = qint64(asset["size"].toDouble());
        const auto hash = asset["sha256"].toString().toLatin1();
        auto valid = [&] {
            QFile file(dir.filePath(name));
            if (!file.open(QIODevice::ReadOnly) || file.size() != size)
                return false;
            QCryptographicHash digest(QCryptographicHash::Sha256);
            while (!file.atEnd()) {
                if (cancel)
                    return false;
                const auto data = file.read(65536);
                if (data.isEmpty() && file.error() != QFile::NoError)
                    return false;
                digest.addData(data);
            }
            return digest.result().toHex() == hash;
        };
        status(ui_text("Checking translation files…"));
        if (valid())
            continue;
        if (!download || cancel)
            return false;
        QSaveFile file(dir.filePath(name));
        if (!file.open(QIODevice::WriteOnly))
            throw std::runtime_error("Cannot write model cache.");
        QCryptographicHash digest(QCryptographicHash::Sha256);
        qint64 received = 0;
        int last = -1;
        const auto transfer = fetch ? fetch : Fetch(awl::fetch_https);
        transfer(
            asset["url"].toString().toStdString(),
            [&](const char *data, size_t length) {
                if (cancel)
                    throw std::runtime_error("Download cancelled.");
                received += qint64(length);
                if (received > size || file.write(data, qint64(length)) != qint64(length))
                    throw std::runtime_error("Model download could not be saved.");
                digest.addData(QByteArrayView(data, qsizetype(length)));
                const int percent = int(received * 100 / size);
                if (percent != last) {
                    last = percent;
                    status(ui_text("Downloading translation files · %1%").arg(percent));
                }
            },
            cancel);
        if (cancel)
            return false;
        if (received != size || digest.result().toHex() != hash)
            throw std::runtime_error("Model verification failed. Download again.");
        if (!file.commit())
            throw std::runtime_error("Cannot finish saving model.");
    }
    return !cancel;
}
} // namespace awl::chat
