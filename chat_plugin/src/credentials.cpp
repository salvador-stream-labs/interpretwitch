// SPDX-License-Identifier: GPL-2.0-or-later
#include "credentials.hpp"
#ifdef __APPLE__
#include <Security/Security.h>
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <wincred.h>
#endif
namespace awl::chat {
#ifdef __APPLE__
static CFMutableDictionaryRef query() {
    auto q = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                       &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, CFSTR("org.awl.chat.twitch"));
    CFDictionarySetValue(q, kSecAttrAccount, CFSTR("read-only-chat"));
    return q;
}
QString read_credentials() {
    auto q = query();
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFTypeRef data = nullptr;
    const auto status = SecItemCopyMatching(q, &data);
    CFRelease(q);
    if (status != errSecSuccess || !data)
        return {};
    auto bytes = static_cast<CFDataRef>(data);
    const auto value = QString::fromUtf8(reinterpret_cast<const char *>(CFDataGetBytePtr(bytes)),
                                         CFDataGetLength(bytes));
    CFRelease(data);
    return value;
}
bool write_credentials(const QString &json) {
    auto q = query();
    const auto bytes = json.toUtf8();
    auto data =
        CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(bytes.constData()), bytes.size());
    auto values = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(values, kSecValueData, data);
    auto status = SecItemUpdate(q, values);
    if (status == errSecItemNotFound) {
        CFDictionarySetValue(q, kSecValueData, data);
        CFDictionarySetValue(q, kSecAttrAccessible,
                             kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
        status = SecItemAdd(q, nullptr);
    }
    CFRelease(values);
    CFRelease(data);
    CFRelease(q);
    return status == errSecSuccess;
}
bool clear_credentials() {
    auto q = query();
    const auto s = SecItemDelete(q);
    CFRelease(q);
    return s == errSecSuccess || s == errSecItemNotFound;
}
#elif defined(_WIN32)
static constexpr auto target = L"AWL/Chat/Twitch";
QString read_credentials() {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target, CRED_TYPE_GENERIC, 0, &credential))
        return {};
    const auto json = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
                                        credential->CredentialBlobSize);
    CredFree(credential);
    return json;
}
bool write_credentials(const QString &json) {
    const auto bytes = json.toUtf8();
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = const_cast<LPWSTR>(target);
    c.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(bytes.constData()));
    c.CredentialBlobSize = DWORD(bytes.size());
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&c, 0);
}
bool clear_credentials() {
    return CredDeleteW(target, CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
}
#else
QString read_credentials() {
    return {};
}
bool write_credentials(const QString &) {
    return false;
}
bool clear_credentials() {
    return true;
}
#endif
} // namespace awl::chat
