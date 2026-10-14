// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs.h>
#include <QApplication>
#include <QSslSocket>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif
int main(int argc, char **argv) {
#ifdef _WIN32
    // OBS loads modules with its own directory in the default DLL search path,
    // which provides obs-frontend-api.dll. Emulate that with the installed OBS.
    wchar_t obs[MAX_PATH];
    const DWORD length = GetModuleFileNameW(GetModuleHandleW(L"obs.dll"), obs, MAX_PATH);
    if (!length || length == MAX_PATH)
        return 2;
    *wcsrchr(obs, L'\\') = 0;
    if (!AddDllDirectory(obs))
        return 2;
#endif
    QApplication app(argc, argv);
    if ((argc != 4 && argc != 5) || !obs_startup(argc == 5 ? argv[4] : "en-US", argv[3], nullptr))
        return 2;
    // Do not let the developer SDK silently supply plugins absent from OBS.
    QCoreApplication::setLibraryPaths({});
    if (!QSslSocket::availableBackends().isEmpty()) {
        std::cerr << "TLS test is contaminated by an external backend.\n";
        obs_shutdown();
        return 2;
    }
    obs_module_t *module = nullptr;
    const auto opened = obs_open_module(&module, argv[1], argv[2]);
    const bool loaded = opened == MODULE_SUCCESS && obs_init_module(module);
    const bool locale =
        loaded &&
        QString::fromUtf8(obs_get_module_name(module)) ==
            (argc == 5 && QString::fromUtf8(argv[4]) == "ru-RU"
                 ? QString::fromUtf8("Awl Интерпретвич")
                 : QString::fromUtf8("Awl Interpretwitch")) &&
        QString::fromUtf8(obs_get_module_description(module)) ==
            (argc == 5 && QString::fromUtf8(argv[4]) == "ru-RU"
                 ? QString::fromUtf8("Личный перевод чата Twitch на русский")
                 : QString::fromUtf8("Private Twitch chat translation into Russian"));
    if (!locale)
        std::cerr << "Module did not use the OBS locale.\n";
#ifdef _WIN32
    constexpr auto backend = "schannel";
#else
    constexpr auto backend = "securetransport";
#endif
    const bool tls = loaded && QSslSocket::supportsSsl() &&
                     QSslSocket::availableBackends().contains(backend) &&
                     QSslSocket::isClassImplemented(QSsl::ImplementedClass::Socket) &&
                     QSslSocket::isClassImplemented(QSsl::ImplementedClass::Certificate) &&
                     QSslSocket::isProtocolSupported(QSsl::TlsV1_2);
    // SupportedFeature::CertificateVerification refers to the standalone
    // QSslCertificate::verify API, not certificate checks during a handshake.
    // SecureTransport and Schannel use the system trust evaluation during a handshake.
    QSslSocket socket;
    const bool verify_peer = socket.peerVerifyMode() == QSslSocket::AutoVerifyPeer ||
                             socket.peerVerifyMode() == QSslSocket::VerifyPeer;
    if (loaded)
        std::cout
            << "Awl Interpretwitch loads in installed libobs with Qt. Frontend dock placement "
               "requires actual OBS.\n";
    if (tls && verify_peer)
        std::cout << "Packaged TLS backend supports encrypted sockets with SDK plugin paths "
                     "removed; peer verification is enabled.\n";
    else
        std::cerr << "Packaged TLS backend is missing or unusable.\n";
    obs_shutdown();
    return loaded && locale && tls && verify_peer ? 0 : 1;
}
