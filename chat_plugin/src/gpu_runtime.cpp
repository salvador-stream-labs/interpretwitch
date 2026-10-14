// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu_runtime.hpp"
#include "ui_text.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <zlib.h>
#define NOMINMAX
#include <windows.h>

namespace awl::chat {
namespace {
constexpr const char *codes[] = {"", "no-driver", "no-device", "old-driver",
                                 "libraries", "memory", "unknown"};
constexpr qint64 archive_limit = 1LL << 30, file_limit = 1LL << 30, total_limit = 3LL << 29;

struct PackFile {
    QString entry, filename, sha256;
    qint64 size = 0;
};
struct Pack {
    QString id, url, sha256;
    qint64 size = 0;
    std::vector<PackFile> files;
};

[[noreturn]] void invalid() {
    throw std::runtime_error("Invalid GPU support catalogue.");
}
qint64 exact_size(const QJsonValue &value, qint64 limit) {
    const double size = value.toDouble();
    if (!value.isDouble() || size < 1 || size > double(limit) || std::floor(size) != size)
        invalid();
    return qint64(size);
}
Pack load_pack(const QString &manifest) {
    QFile index(manifest);
    if (!index.open(QIODevice::ReadOnly))
        throw std::runtime_error("GPU support catalogue is missing. Reinstall Interpretwitch.");
    const auto bytes = index.read(65537);
    const auto catalogue = QJsonDocument::fromJson(bytes).object();
    static const QRegularExpression id("^[a-z0-9][a-z0-9._-]{0,63}$"), sha("^[0-9a-f]{64}$"),
        filename("^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$");
    Pack pack;
    pack.id = catalogue["id"].toString();
    const auto archive = catalogue["archive"].toObject();
    pack.url = archive["url"].toString();
    pack.sha256 = archive["sha256"].toString();
    const QUrl url(pack.url, QUrl::StrictMode);
    // Only NVIDIA's own download host, over HTTPS, for a fixed archive.
    if (bytes.size() > 65536 || catalogue["schema"].toInt() != 1 || !id.match(pack.id).hasMatch() ||
        pack.id.contains("..") || !url.isValid() || url.scheme() != "https" ||
        url.host() != "developer.download.nvidia.com" || !url.userInfo().isEmpty() ||
        url.hasQuery() || url.hasFragment() || (url.port() != -1 && url.port() != 443) ||
        !sha.match(pack.sha256).hasMatch())
        invalid();
    pack.size = exact_size(archive["size"], archive_limit);
    const auto files = catalogue["files"].toArray();
    if (files.isEmpty() || files.size() > 8)
        invalid();
    qint64 total = 0;
    for (const auto &value : files) {
        const auto file = value.toObject();
        PackFile item{file["entry"].toString(), file["filename"].toString(),
                      file["sha256"].toString(), exact_size(file["size"], file_limit)};
        // Fixed output names: no separators, drive letters, ADS or dot files.
        if (item.entry.isEmpty() || item.entry.size() > 260 ||
            !filename.match(item.filename).hasMatch() || item.filename.contains("..") ||
            item.filename.compare("pack.json", Qt::CaseInsensitive) == 0 ||
            !sha.match(item.sha256).hasMatch() ||
            std::any_of(pack.files.begin(), pack.files.end(), [&](const PackFile &other) {
                return other.filename.compare(item.filename, Qt::CaseInsensitive) == 0 ||
                       other.entry == item.entry;
            }))
            invalid();
        total += item.size;
        pack.files.push_back(item);
    }
    if (total > total_limit)
        invalid();
    return pack;
}
QString pack_directory(const Pack &pack, const QString &root) {
    if (!QFileInfo(root).isAbsolute() || QFileInfo(root).isSymLink())
        throw std::runtime_error("GPU support could not be installed. Try again.");
    return QDir(root).filePath(pack.id);
}
bool complete(const Pack &pack, const QString &directory) {
    const QDir dir(directory);
    if (QFileInfo(directory).isSymLink() || !QFileInfo(directory).isDir())
        return false;
    QFile marker(dir.filePath("pack.json"));
    if (!marker.open(QIODevice::ReadOnly) ||
        QJsonDocument::fromJson(marker.read(65537)).object()["id"].toString() != pack.id)
        return false;
    return std::all_of(pack.files.begin(), pack.files.end(), [&](const PackFile &file) {
        const QFileInfo info(dir.filePath(file.filename));
        return info.isFile() && !info.isSymLink() && info.size() == file.size;
    });
}

// Minimal ZIP reader for the fixed archive: central directory, optional ZIP64
// fields, stored or deflated entries. Every entry is checked against its CRC-32
// and the catalogue's size/SHA-256 before the pack is published.
struct ZipEntry {
    quint16 flags = 0, method = 0;
    quint32 crc = 0;
    qint64 compressed = 0, size = 0, offset = 0;
};
quint16 u16(const char *p) {
    return quint16(quint8(p[0]) | quint8(p[1]) << 8);
}
quint32 u32(const char *p) {
    return quint32(u16(p)) | quint32(u16(p + 2)) << 16;
}
quint64 u64(const char *p) {
    return quint64(u32(p)) | quint64(u32(p + 4)) << 32;
}
[[noreturn]] void corrupt() {
    throw std::runtime_error("GPU support download failed verification. Try again.");
}
QByteArray read_at(QFile &file, qint64 offset, qint64 length) {
    if (offset < 0 || length < 0 || offset + length > file.size() || !file.seek(offset))
        corrupt();
    auto data = file.read(length);
    if (data.size() != length)
        corrupt();
    return data;
}
QHash<QString, ZipEntry> read_directory(QFile &zip) {
    const qint64 tail = std::min<qint64>(zip.size(), 65535 + 22);
    const auto end = read_at(zip, zip.size() - tail, tail);
    qsizetype at = -1;
    for (qsizetype i = end.size() - 22; i >= 0; --i)
        if (u32(end.constData() + i) == 0x06054b50) {
            at = i;
            break;
        }
    if (at < 0)
        corrupt();
    const char *eocd = end.constData() + at;
    quint64 count = u16(eocd + 10), length = u32(eocd + 12), start = u32(eocd + 16);
    if (count == 0xffff || length == 0xffffffff || start == 0xffffffff) {
        const qint64 locator = zip.size() - tail + at - 20;
        const auto zip64 = read_at(zip, locator, 20);
        if (u32(zip64.constData()) != 0x07064b50)
            corrupt();
        const auto record = read_at(zip, qint64(u64(zip64.constData() + 8)), 56);
        if (u32(record.constData()) != 0x06064b50)
            corrupt();
        count = u64(record.constData() + 32);
        length = u64(record.constData() + 40);
        start = u64(record.constData() + 48);
    }
    if (count > 10000 || length > 16 * 1024 * 1024)
        corrupt();
    const auto directory = read_at(zip, qint64(start), qint64(length));
    QHash<QString, ZipEntry> entries;
    qsizetype p = 0;
    for (quint64 i = 0; i < count; ++i) {
        if (p + 46 > directory.size() || u32(directory.constData() + p) != 0x02014b50)
            corrupt();
        const char *h = directory.constData() + p;
        ZipEntry entry{u16(h + 8), u16(h + 10), u32(h + 16), u32(h + 20), u32(h + 24), u32(h + 42)};
        const int name = u16(h + 28), extra = u16(h + 30), comment = u16(h + 32);
        if (p + 46 + name + extra + comment > directory.size())
            corrupt();
        const auto path = QString::fromUtf8(h + 46, name);
        // ZIP64 extended information: only the fields saturated in the record.
        for (qsizetype e = 0; e + 4 <= extra;) {
            const char *x = h + 46 + name + e;
            const int id = u16(x), size = u16(x + 2);
            if (e + 4 + size > extra)
                corrupt();
            if (id == 0x0001) {
                qsizetype f = 4;
                auto next = [&](qint64 &field) {
                    if (field != 0xffffffff)
                        return;
                    if (f + 8 > 4 + size)
                        corrupt();
                    field = qint64(u64(x + f));
                    f += 8;
                };
                next(entry.size);
                next(entry.compressed);
                next(entry.offset);
            }
            e += 4 + size;
        }
        entries.insert(path, entry);
        p += 46 + name + extra + comment;
    }
    return entries;
}
void extract(QFile &zip, const ZipEntry &entry, const PackFile &file, const QString &output,
             const std::atomic<bool> &cancel, const std::function<void(qint64)> &progress) {
    // Encrypted or unusual entries are rejected; sizes must match the catalogue.
    if ((entry.flags & 0x1) || (entry.method != 0 && entry.method != 8) || entry.size != file.size ||
        (entry.method == 0 && entry.compressed != entry.size) || entry.compressed > archive_limit)
        corrupt();
    const auto local = read_at(zip, entry.offset, 30);
    if (u32(local.constData()) != 0x04034b50)
        corrupt();
    qint64 position = entry.offset + 30 + u16(local.constData() + 26) + u16(local.constData() + 28);
    if (position + entry.compressed > zip.size() || !zip.seek(position))
        corrupt();
    QFile out(output);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw std::runtime_error("GPU support could not be installed. Try again.");
    QCryptographicHash digest(QCryptographicHash::Sha256);
    uLong crc = crc32(0, nullptr, 0);
    qint64 written = 0, remaining = entry.compressed;
    auto emit_data = [&](const char *data, qint64 length) {
        written += length;
        if (written > file.size || out.write(data, length) != length)
            corrupt();
        digest.addData(QByteArrayView(data, qsizetype(length)));
        crc = crc32(crc, reinterpret_cast<const Bytef *>(data), uInt(length));
        progress(length);
    };
    std::vector<char> input(256 * 1024), buffer(256 * 1024);
    z_stream stream{};
    if (entry.method == 8 && inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        throw std::runtime_error("GPU support could not be installed. Try again.");
    struct Cleanup {
        z_stream &s;
        bool active;
        ~Cleanup() {
            if (active)
                inflateEnd(&s);
        }
    } cleanup{stream, entry.method == 8};
    int status = Z_OK;
    while (remaining > 0 && status != Z_STREAM_END) {
        if (cancel)
            throw std::runtime_error("Download cancelled.");
        const auto chunk = zip.read(input.data(), std::min<qint64>(remaining, qint64(input.size())));
        if (chunk <= 0)
            corrupt();
        remaining -= chunk;
        if (entry.method == 0) {
            emit_data(input.data(), chunk);
            continue;
        }
        stream.next_in = reinterpret_cast<Bytef *>(input.data());
        stream.avail_in = uInt(chunk);
        while (stream.avail_in > 0 && status != Z_STREAM_END) {
            stream.next_out = reinterpret_cast<Bytef *>(buffer.data());
            stream.avail_out = uInt(buffer.size());
            status = inflate(&stream, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END)
                corrupt();
            emit_data(buffer.data(), qint64(buffer.size() - stream.avail_out));
        }
    }
    if ((entry.method == 8 && status != Z_STREAM_END) || written != file.size ||
        crc != entry.crc || digest.result().toHex() != file.sha256.toLatin1())
        corrupt();
    if (!out.flush())
        throw std::runtime_error("GPU support could not be installed. Try again.");
}
} // namespace

QString gpu_issue_code(GpuIssue issue) {
    return codes[int(issue)];
}
GpuIssue gpu_issue_from_code(const QString &code, bool *valid) {
    for (int i = 0; i <= int(GpuIssue::Unknown); ++i)
        if (code == codes[i]) {
            if (valid)
                *valid = true;
            return GpuIssue(i);
        }
    if (valid)
        *valid = false;
    return GpuIssue::Unknown;
}
GpuIssue classify_gpu_error(const std::string &message) {
    const auto text = QString::fromStdString(message).toLower();
    auto has = [&](const char *needle) {
        return text.contains(QLatin1String(needle));
    };
    if (has("out of memory") || has("cudaerrormemoryallocation") ||
        has("cublas_status_alloc_failed"))
        return GpuIssue::Memory;
    if (has("cublas64_") || has("cublaslt64_") || has("cannot be loaded"))
        return GpuIssue::Libraries;
    if (has("insufficient") && has("driver"))
        return GpuIssue::OldDriver;
    if (has("no compatible nvidia gpu") || has("no cuda-capable device"))
        return GpuIssue::NoDevice;
    return GpuIssue::Unknown;
}
QString gpu_issue_text(GpuIssue issue, int driver_cuda) {
    switch (issue) {
    case GpuIssue::None:
        return {};
    case GpuIssue::NoDriver:
        return ui_text("No NVIDIA graphics driver was found. Translation uses CPU.");
    case GpuIssue::NoDevice:
        return ui_text("No compatible NVIDIA GPU was found. Translation uses CPU.");
    case GpuIssue::OldDriver:
        return driver_cuda > 0
                   ? ui_text("GPU needs a newer NVIDIA driver: this one supports CUDA %1, and "
                             "CUDA 12 or later is required. Translation uses CPU.")
                         .arg(QString("%1.%2").arg(driver_cuda / 1000).arg(driver_cuda % 1000 / 10))
                   : ui_text("GPU needs a newer NVIDIA driver that supports CUDA 12 or later. "
                             "Translation uses CPU.");
    case GpuIssue::Libraries:
        return ui_text("GPU support needs a one-time download of approximately 550 MB. "
                       "Translation uses CPU until then.");
    case GpuIssue::Memory:
        return ui_text("Not enough free GPU memory for this model. Translation uses CPU. Close "
                       "other programs using the GPU, or choose a lighter model.");
    case GpuIssue::Unknown:
        break;
    }
    return ui_text("GPU could not start for an unknown reason. Translation uses CPU.");
}

GpuProbe probe_gpu() {
    GpuProbe probe;
    // The graphics driver installs nvcuda.dll in System32; never search elsewhere.
    const auto cuda = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!cuda) {
        probe.issue = GpuIssue::NoDriver;
        return probe;
    }
    using Init = int(__stdcall *)(unsigned);
    using Query = int(__stdcall *)(int *);
    const auto init = reinterpret_cast<Init>(GetProcAddress(cuda, "cuInit"));
    const auto version = reinterpret_cast<Query>(GetProcAddress(cuda, "cuDriverGetVersion"));
    const auto count = reinterpret_cast<Query>(GetProcAddress(cuda, "cuDeviceGetCount"));
    if (!init || !version || !count) {
        probe.issue = GpuIssue::NoDriver;
        return probe;
    }
    if (version(&probe.driver_cuda) != 0)
        probe.driver_cuda = 0;
    // A driver advertising CUDA 13 runs CUDA 12 libraries; only older drivers fail.
    if (probe.driver_cuda > 0 && probe.driver_cuda < 12000) {
        probe.issue = GpuIssue::OldDriver;
        return probe;
    }
    const int started = init(0);
    int devices = 0;
    if (started == 35) // CUDA_ERROR_INSUFFICIENT_DRIVER
        probe.issue = GpuIssue::OldDriver;
    else if (started == 100 || (started == 0 && (count(&devices) != 0 || devices < 1)))
        probe.issue = GpuIssue::NoDevice; // CUDA_ERROR_NO_DEVICE
    else if (started != 0)
        probe.issue = GpuIssue::Unknown;
    // cuBLAS 12 through the normal search order, which includes an installed pack.
    else if (!LoadLibraryExW(L"cublas64_12.dll", nullptr, 0))
        probe.issue = GpuIssue::Libraries;
    else
        probe.issue = GpuIssue::None;
    return probe;
}

QString installed_gpu_pack(const QString &manifest, const QString &root) {
    try {
        const auto pack = load_pack(manifest);
        const auto directory = pack_directory(pack, root);
        return complete(pack, directory) ? directory : QString{};
    } catch (...) {
        return {};
    }
}

QString install_gpu_pack(const QString &manifest, const QString &root,
                         const std::atomic<bool> &cancel, const Status &status,
                         const Fetch &fetch) {
    // Validate the whole plan before writing anything.
    const auto pack = load_pack(manifest);
    const auto directory = pack_directory(pack, root);
    if (complete(pack, directory))
        return directory;
    qint64 extracted = 0;
    for (const auto &file : pack.files)
        extracted += file.size;
    if (!QDir().mkpath(root))
        throw std::runtime_error("GPU support could not be installed. Try again.");
    const QStorageInfo storage(root);
    if (storage.isValid() && storage.bytesAvailable() < pack.size + extracted + (64LL << 20))
        throw std::runtime_error(
            "Not enough free disk space for GPU support (about 1.4 GB needed).");
    const auto archive_path = directory + ".download", staging = directory + ".partial";
    struct Remove {
        QString archive, staging;
        ~Remove() {
            QFile::remove(archive);
            QDir(staging).removeRecursively();
        }
    } remove{archive_path, staging};
    QDir(staging).removeRecursively();
    if (!QDir().mkpath(staging))
        throw std::runtime_error("GPU support could not be installed. Try again.");
    {
        QFile archive(archive_path);
        if (!archive.open(QIODevice::WriteOnly | QIODevice::Truncate))
            throw std::runtime_error("GPU support could not be installed. Try again.");
        QCryptographicHash digest(QCryptographicHash::Sha256);
        qint64 received = 0;
        int last = -1;
        const auto transfer = fetch ? fetch : Fetch(awl::fetch_https);
        transfer(
            pack.url.toStdString(),
            [&](const char *data, size_t length) {
                if (cancel)
                    throw std::runtime_error("Download cancelled.");
                received += qint64(length);
                if (received > pack.size || archive.write(data, qint64(length)) != qint64(length))
                    throw std::runtime_error("GPU support could not be installed. Try again.");
                digest.addData(QByteArrayView(data, qsizetype(length)));
                const int percent = int(received * 100 / pack.size);
                if (percent != last) {
                    last = percent;
                    status(ui_text("Downloading GPU support · %1%").arg(percent));
                }
            },
            cancel);
        if (cancel)
            return {};
        status(ui_text("Checking GPU support download…"));
        if (received != pack.size || digest.result().toHex() != pack.sha256.toLatin1())
            corrupt();
    }
    QFile zip(archive_path);
    if (!zip.open(QIODevice::ReadOnly))
        throw std::runtime_error("GPU support could not be installed. Try again.");
    const auto entries = read_directory(zip);
    qint64 done = 0;
    int last = -1;
    for (const auto &file : pack.files) {
        const auto entry = entries.constFind(file.entry);
        if (entry == entries.cend())
            corrupt();
        extract(zip, *entry, file, QDir(staging).filePath(file.filename), cancel,
                [&](qint64 length) {
                    done += length;
                    const int percent = int(done * 100 / extracted);
                    if (percent != last) {
                        last = percent;
                        status(ui_text("Installing GPU support · %1%").arg(percent));
                    }
                });
        if (cancel)
            return {};
    }
    zip.close();
    QFile marker(QDir(staging).filePath("pack.json"));
    QJsonArray files;
    for (const auto &file : pack.files)
        files.append(QJsonObject{{"filename", file.filename},
                                 {"size", double(file.size)},
                                 {"sha256", file.sha256}});
    if (!marker.open(QIODevice::WriteOnly) ||
        marker.write(QJsonDocument(QJsonObject{{"id", pack.id}, {"files", files}}).toJson()) < 0 ||
        !marker.flush())
        throw std::runtime_error("GPU support could not be installed. Try again.");
    marker.close();
    // Publish atomically: an interrupted install never looks complete.
    QDir(directory).removeRecursively();
    if (!QDir().rename(staging, directory) || !complete(pack, directory))
        throw std::runtime_error("GPU support could not be installed. Try again.");
    return directory;
}

bool use_gpu_pack(const QString &directory) {
    // Process-local: the helper's loader finds cuBLAS; OBS and the system are unchanged.
    std::vector<wchar_t> current(32767);
    const DWORD length = GetEnvironmentVariableW(L"PATH", current.data(), DWORD(current.size()));
    const auto path = QDir::toNativeSeparators(directory).toStdWString() + L";" +
                      std::wstring(current.data(), length < current.size() ? length : 0);
    return SetEnvironmentVariableW(L"PATH", path.c_str()) != 0;
}
} // namespace awl::chat
