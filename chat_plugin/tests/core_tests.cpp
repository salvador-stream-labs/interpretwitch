// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_core.hpp"
#include "model_profile.hpp"
#include "twitch.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>
#include <iostream>
#include <stdexcept>
using namespace awl::chat;
static void check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
static QByteArray notification(QString envelope, QString type, QJsonObject event) {
    return QJsonDocument(QJsonObject{{"metadata", QJsonObject{{"message_type", "notification"},
                                                              {"message_id", envelope},
                                                              {"subscription_type", type}}},
                                     {"payload", QJsonObject{{"event", event}}}})
        .toJson();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        check(normal_channel(" #Example_1 ") == "example_1", "Channel normalization");
        check(normal_channel("a?login=b").isEmpty(), "Channel query injection");
        check(safe_reconnect_url("wss://eventsub.wss.twitch.tv/ws?ticket=abc"), "Valid reconnect");
        check(!safe_reconnect_url("wss://eventsub.wss.twitch.tv.evil.test/ws") &&
                  !safe_reconnect_url("ws://eventsub.wss.twitch.tv/ws") &&
                  !safe_reconnect_url("wss://user@eventsub.wss.twitch.tv/ws"),
              "Reconnect credentials leak");
        Protocol protocol;
        QJsonObject payload{
            {"broadcaster_user_id", "123"},
            {"message_id", "chat-1"},
            {"chatter_user_id", "user-1"},
            {"chatter_user_name", "<b>Alice</b>"},
            {"color", "#1E90FF"},
            {"message",
             QJsonObject{
                 {"text", "Hello Kappa @bob https://example.test"},
                 {"fragments", QJsonArray{QJsonObject{{"type", "text"}, {"text", "Hello "}},
                                          QJsonObject{{"type", "emote"}, {"text", "Kappa"}},
                                          QJsonObject{{"type", "text"},
                                                      {"text", " @bob https://example.test"}}}}}}};
        auto encoded = notification("e-1", "channel.chat.message", payload);
        check(protocol.accept(encoded, "wrong").kind == EventKind::Ignore, "Wrong-channel message");
        auto event = protocol.accept(encoded, "123");
        check(event.kind == EventKind::Message && event.message.parts.size() >= 4,
              "Fragment decoding");
        check(event.message.color == "#1E90FF", "Twitch username colour missing");
        auto bad_color = payload;
        bad_color["color"] = "red; background:url(https://example.test)";
        check(protocol.accept(notification("color-test", "channel.chat.message", bad_color), "123")
                  .message.color.isEmpty(),
              "Unvalidated username colour");
        QString protected_text;
        for (const auto &part : event.message.parts)
            if (part.protected_text)
                protected_text += part.text;
        check(protected_text.contains("Kappa") && protected_text.contains("@bob") &&
                  protected_text.contains("https://example.test"),
              "Protected fragments");
        check(protocol.accept(encoded, "123").kind == EventKind::Ignore, "Duplicate redelivery");
        check(protocol.accept(QByteArray(65537, 'x'), "123").kind == EventKind::Ignore,
              "Payload bound");
        auto removed = protocol.accept(
            notification("e-2", "channel.chat.message_delete",
                         {{"broadcaster_user_id", "123"}, {"message_id", "chat-1"}}),
            "123");
        check(removed.kind == EventKind::Delete && removed.id == "chat-1", "Moderation delete");
        check(protocol.accept(notification(
                                  "e-3", "channel.chat.clear_user_messages",
                                  {{"broadcaster_user_id", "123"}, {"target_user_id", "user-1"}}),
                              "123")
                      .kind == EventKind::ClearUser,
              "Moderation user");
        check(protocol.accept(notification("e-4", "channel.chat.clear",
                                           {{"broadcaster_user_id", "123"}}),
                              "123")
                      .kind == EventKind::Clear,
              "Moderation clear");
        check(repetitive("hello hello hello hello hello hello") && !repetitive("Go go go!"),
              "Runaway repetition guard");
        JobQueue queue;
        QStringList discarded;
        const auto discard = [&](const Job &job) {
            discarded << job.message.id;
        };
        size_t drops = 0;
        for (int i = 0; i < 30; ++i)
            drops += queue.offer({Message{QString::number(i)}, 1, 1000}, discard);
        check(queue.size() == 24 && drops == 6 && discarded.size() == 6 &&
                  discarded.front() == "0" && discarded.back() == "5",
              "Queue memory bound");
        size_t expired = 0;
        check(!queue.take(17000, expired, discard) && expired == 24 && discarded.size() == 30 &&
                  discarded.back() == "29",
              "Stale translations left pending indicators");
        queue.offer({Message{"fresh"}, 1, 18000});
        check(queue.take(18001, expired)->message.id == "fresh", "Fresh job");
        queue.offer({Message{"cancelled"}, 1, 18000});
        queue.clear(discard);
        check(discarded.back() == "cancelled" && queue.size() == 0,
              "Queue clear left pending indicators");
        check(model_profile(model_id(ModelProfile::Light)) == ModelProfile::Light &&
                  model_profile(model_id(ModelProfile::Heavy)) == ModelProfile::Heavy &&
                  !model_profile("untrusted-model"),
              "Model identity allowlist");
        check(profile_manifest("/data/models.lock.json", ModelProfile::Light) ==
                      "/data/models.lock.json" &&
                  profile_manifest("/data/models.lock.json", ModelProfile::Heavy) ==
                      "/data/models-heavy.lock.json" &&
                  profile_directory("/cache", ModelProfile::Light) == "/cache" &&
                  profile_directory("/cache", ModelProfile::Heavy) == "/cache/m2m100-418m-int8",
              "Model cache separation");
        for (auto profile : {ModelProfile::Light, ModelProfile::Heavy}) {
            check(translation_language("hello!", "unknown", .1f, profile) == "en" &&
                      translation_language("Hello everyone", "en", .9f, profile) == "en",
                  "English greeting/phrase regression");
            check(translation_language("Русское сообщение", "ru", .99f, profile).isEmpty() &&
                      translation_language("uncertain words", "en", .1f, profile).isEmpty() &&
                      translation_language("unknown words", "xx", .99f, profile).isEmpty() &&
                      translation_language("KEKW", "en", .99f, profile).isEmpty(),
                  "Russian, uncertain, unsupported or emote text translated");
            // Scores measured with lid.176.ftz for these messages.
            check(translation_language("Every now and then I fall apart", "en", .477f, profile,
                                       .031f) == "en" &&
                      translation_language("omg so close", "en", .531f, profile, .038f) == "en",
                  "Decisive low-score English lead rejected");
            check(translation_language("close call words", "en", .45f, profile, .1f).isEmpty() &&
                      translation_language("faint lead words", "en", .35f, profile, .001f)
                          .isEmpty() &&
                      translation_language("lets go team", "it", .402f, profile, .326f).isEmpty(),
                  "Narrow or weak detector lead translated");
        }
        check(translation_language("hola a todos", "pt", .54f, ModelProfile::Heavy, .43f).isEmpty(),
              "Ambiguous Portuguese/Spanish message translated");
        check(translation_language("Bonjour tout le monde", "fr", .99f, ModelProfile::Light)
                      .isEmpty() &&
                  translation_language("Bonjour tout le monde", "fr", .99f, ModelProfile::Heavy) ==
                      "fr" &&
                  translation_language("Зачекайте, будь ласка", "uk", .99f, ModelProfile::Heavy) ==
                      "uk" &&
                  translation_language("请等待所有玩家准备好", "zh", .99f, ModelProfile::Heavy) ==
                      "zh",
              "Multilingual routing rejected supported scripts");

        // Entire device flow uses fake HTTP and a fake keychain. Never touches
        // the owner's Twitch account, credential store or network.
        TwitchClient client;
        QString saved;
        int polls = 0, authorizations = 0, validations = 0;
        HttpDone stale;
        client.load = [] {
            return QString{};
        };
        client.save = [&](QString value) {
            saved = value;
            return true;
        };
        client.erase = [&] {
            saved.clear();
            return true;
        };
        client.authorize = [&](QString url, QString code) {
            check(url.startsWith("https://www.twitch.tv/activate"), "Activation origin");
            check(code == "ABCDEFGH", "Device code");
            ++authorizations;
        };
        client.http_hook = [&](QString method, QString url, QByteArray body, QByteArray auth,
                               HttpDone done) {
            if (url.endsWith("/device")) {
                check(method == "POST" &&
                          QUrlQuery(QString::fromUtf8(body))
                                  .queryItemValue("scopes", QUrl::FullyDecoded) ==
                              "user:read:chat" &&
                          !body.contains("write"),
                      "Only read scope requested");
                done({200,
                      {{"device_code", "private-device-code"},
                       {"user_code", "ABCDEFGH"},
                       {"verification_uri", "https://www.twitch.tv/activate"},
                       {"expires_in", 1800},
                       {"interval", 5}}});
            } else if (url.endsWith("/token")) {
                ++polls;
                if (polls == 1)
                    done({400, {{"message", "authorization_pending"}}});
                else
                    done({200,
                          {{"access_token", "private-test-token"},
                           {"refresh_token", "private-test-refresh"},
                           {"expires_in", 14400}}});
            } else if (url.endsWith("/validate")) {
                ++validations;
                check(auth == "private-test-token", "Validate wrong token");
                done({200,
                      {{"client_id", "testclient1234"},
                       {"user_id", "me"},
                       {"scopes", QJsonArray{"user:read:chat"}},
                       {"expires_in", 14400}}});
            } else {
                check(url.contains("/users?login=channel"), "Unexpected Twitch route");
                stale = std::move(done);
            }
        };
        client.start("testclient1234", "channel", true);
        check(authorizations == 1, "Device flow not started");
        client.poll_authorization();
        check(client.active(), "Pending authorization treated as failure");
        client.poll_authorization();
        check(validations == 1 && saved.contains("private-test-refresh"),
              "Token validation/persistence");
        client.disconnect_chat(true);
        check(!client.active() && saved.isEmpty(), "Sign-out retained credentials");
        stale({200, {{"data", QJsonArray{QJsonObject{{"id", "channel-id"}}}}}});
        check(!client.active(), "Stale HTTP resurrected sign-in");
        // Saved sign-in, refresh rotation, four read subscriptions, handover,
        // duplicate delivery, and revocation, all through the production client.
        int subscriptions = 0, received = 0, opened = 0, refreshed = 0, validations2 = 0;
        bool handover = false;
        QString latest_url;
        saved =
            R"({"client_id":"testclient1234","access_token":"expired","refresh_token":"refresh+old&=value%"})";
        client.load = [&] {
            return saved;
        };
        client.event = [&](Event e) {
            if (e.kind == EventKind::Message)
                ++received;
        };
        client.socket_hook = [&](QString url, bool replacement) {
            ++opened;
            handover = replacement;
            latest_url = url;
        };
        client.http_hook = [&](QString, QString url, QByteArray body, QByteArray, HttpDone done) {
            if (url.endsWith("/validate")) {
                if (++validations2 == 1)
                    done({401, {}});
                else
                    done({200,
                          {{"client_id", "testclient1234"},
                           {"user_id", "me"},
                           {"scopes", QJsonArray{"user:read:chat"}},
                           {"expires_in", 14400}}});
            } else if (url.endsWith("/token")) {
                ++refreshed;
                check(body.contains("refresh_token=refresh%2Bold%26%3Dvalue%25") &&
                          !body.contains("client_secret"),
                      "Refresh token was not safely form-encoded");
                done({200,
                      {{"access_token", "access-new"},
                       {"refresh_token", "refresh-new"},
                       {"expires_in", 14400}}});
            } else if (url.endsWith("/subscriptions")) {
                const auto subscription = QJsonDocument::fromJson(body).object();
                check(subscription["type"].toString().startsWith("channel.chat."),
                      "Unexpected event subscription");
                check(subscription["transport"].toObject()["session_id"].toString() == "session1",
                      "Session subscription mismatch");
                ++subscriptions;
                done({202, {}});
            } else
                done({200, {{"data", QJsonArray{QJsonObject{{"id", "123"}}}}}});
        };
        client.start("testclient1234", "channel", true);
        check(refreshed == 1 && saved.contains("refresh-new") &&
                  !saved.contains("refresh+old&=value%"),
              "Refresh rotation failed");
        check(opened == 1 && !handover, "Initial socket connection failed");
        auto welcome = QString::fromUtf8(
            QJsonDocument(
                QJsonObject{
                    {"metadata", QJsonObject{{"message_type", "session_welcome"}}},
                    {"payload",
                     QJsonObject{{"session", QJsonObject{{"id", "session1"},
                                                         {"keepalive_timeout_seconds", 30}}}}}})
                .toJson());
        client.ingest(welcome);
        check(subscriptions == 4, "Missing chat/moderation subscriptions");
        client.ingest(QString::fromUtf8(encoded));
        client.ingest(QString::fromUtf8(encoded));
        check(received == 1, "Client redelivery duplicate");
        client.ingest(
            R"({"metadata":{"message_type":"session_reconnect"},"payload":{"session":{"reconnect_url":"wss://eventsub.wss.twitch.tv/ws?ticket=123"}}})");
        check(opened == 2 && handover && latest_url.endsWith("ticket=123"),
              "Reconnect ticket changed/lost");
        client.ingest(welcome, true);
        check(subscriptions == 4, "Handover resubscribed existing subscriptions");
        client.ingest(QString::fromUtf8(encoded));
        check(received == 1, "Handover lost duplicate history");
        client.ingest(
            R"({"metadata":{"message_type":"revocation"},"payload":{"subscription":{"status":"authorization_revoked"}}})");
        check(!client.active() && saved.isEmpty(), "Revoked authorization retained");
        // A transport failure is not evidence that the app is Confidential.
        // Preserve actionable local diagnostics without exposing server bodies.
        QString sign_in_status;
        client.status = [&](QString text) {
            sign_in_status = text;
        };
        client.authorize = [&](QString, QString) {
            throw std::runtime_error("Failed sign-in opened an authorization page");
        };
        const auto failed_sign_in = [&](HttpResult response, QString expected,
                                        bool application_error) {
            client.http_hook = [response](QString, QString, QByteArray, QByteArray, HttpDone done) {
                done(response);
            };
            client.start("testclient1234", "channel", false);
            check(!client.active(), "Failed device request remained active");
            check(sign_in_status.contains(expected), "Sign-in failure lost its cause");
            check(sign_in_status.contains("Public") == application_error,
                  "Network failure blamed application registration");
            check(!sign_in_status.contains("private-response-value"),
                  "Server response leaked into status");
        };
        failed_sign_in({0, {}, "Secure connections are unavailable. Reinstall Awl Interpretwitch."},
                       "Reinstall Awl Interpretwitch", false);
        failed_sign_in({0, {}, "Cannot find Twitch's server. Check DNS."}, "DNS", false);
        failed_sign_in({0, {}}, "Cannot reach Twitch", false);
        failed_sign_in({400, {{"message", "private-response-value"}}}, "HTTP 400", true);
        failed_sign_in({401, {}}, "HTTP 401", true);
        failed_sign_in({403, {}}, "HTTP 403", false);
        failed_sign_in({429, {}}, "Wait a minute", false);
        failed_sign_in({503, {}}, "temporarily unavailable", false);
        const QJsonObject valid_device{{"device_code", "private-device-code"},
                                       {"user_code", "ABCDEFGH"},
                                       {"verification_uri", "https://www.twitch.tv/activate"}};
        for (const auto &url : QStringList{
                 "http://www.twitch.tv/activate", "https://www.twitch.tv.evil.test/activate",
                 "https://user@www.twitch.tv/activate", "https://www.twitch.tv:444/activate",
                 "https://www.twitch.tv:invalid/activate"}) {
            auto invalid = valid_device;
            invalid["verification_uri"] = url;
            failed_sign_in({200, invalid}, "invalid sign-in response", false);
        }
        for (const auto &field : QStringList{"device_code", "user_code"}) {
            auto invalid = valid_device;
            invalid[field] = QString(2049, 'x');
            failed_sign_in({200, invalid}, "invalid sign-in response", false);
        }
        client.erase = [] {
            return false;
        };
        client.disconnect_chat(true);
        check(sign_in_status.contains("could not be removed"),
              "Credential deletion failure was hidden");
        // Exercise the real preflight with no SDK/backend present, without
        // making an HTTP request or reading the owner's credential store.
        QCoreApplication::setLibraryPaths({});
        client.http_hook = {};
        client.start("testclient1234", "channel", false);
        check(!client.active() && sign_in_status.contains("Secure connections are unavailable") &&
                  !sign_in_status.contains("Public"),
              "Missing packaged TLS was not caught before sign-in");
        std::cout << "Protocol, bounded queues, emotes/links, deletion, read-only device "
                     "authorization, validation and sign-out passed.\n";
        std::cout << "Sign-in errors distinguish missing TLS, DNS/network failures, rejected "
                     "applications, rate limits and server outages.\n";
        std::cout << "Unsafe activation URLs, oversized codes and failed credential deletion "
                     "handled explicitly.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
