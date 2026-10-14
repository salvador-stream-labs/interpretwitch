// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs.h>
#include <QApplication>
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
    obs_module_t *module = nullptr;
    const auto opened = obs_open_module(&module, argv[1], argv[2]);
    const bool loaded = opened == MODULE_SUCCESS && obs_init_module(module);
    const bool locale = loaded &&
                        QString::fromUtf8(obs_get_module_name(module)) ==
                            (argc == 5 && QString::fromUtf8(argv[4]) == "ru-RU"
                                 ? QString::fromUtf8("Интерпретвич")
                                 : QString::fromUtf8("Interpretwitch")) &&
                        QString::fromUtf8(obs_get_module_description(module)) ==
                            (argc == 5 && QString::fromUtf8(argv[4]) == "ru-RU"
                                 ? QString::fromUtf8("Локальный перевод чата Twitch для вас")
                                 : QString::fromUtf8("Private local translation for Twitch chat"));
    if (!locale)
        std::cerr << "Module did not use the OBS locale.\n";
    if (loaded)
        std::cout << "Interpretwitch loads with the installed libobs and Qt. Tools menu and live "
                     "browser require OBS.\n";
    obs_shutdown();
    return loaded && locale ? 0 : 1;
}
