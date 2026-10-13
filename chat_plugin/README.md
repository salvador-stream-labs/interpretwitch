# Using Awl Interpretwitch

Version 0.4.0. A private OBS dock that translates incoming Twitch chat into Russian.
Messages are never posted back to Twitch. The dock is not a scene source, but will
be visible if you capture the OBS window or its display.

The interface follows **OBS → Settings → General → Language**: Russian for Russian
locales, English otherwise. Restart OBS after changing its language. The Russian
product name is **Awl Интерпретвич**. Interface language does not change the
translation target, which is currently Russian.

## Choose a model

| Resource usage | Download | CPU inference | Intended use |
| --- | --- | --- | --- |
| Light (default) | 83.4 MB | 2 threads | Small English → Russian model |
| Heavy | 496.8 MB | 1 thread | More languages → Russian, more RAM and slower translation |

Heavy uses quantized M2M100 418M; it means broader language coverage, not a promise
of better English translation. Russian, unsupported and uncertain messages remain
unchanged. Short chat and gaming slang can be mistranslated by either model.
Only the selected model is resident. Switching reuses verified cached files,
releases the previous helper first and preserves Twitch and displayed history.
Missing files download only after **Download model**, never just from changing
the dropdown. Existing Light files stay where they were; Heavy has its own cache.

Incoming chat can tolerate some delay, so Heavy uses one CPU inference thread to
limit parallel competition with a stream. This is not a percentage CPU cap; a
larger model still performs more work per message and stays in RAM while loaded.
Queues and deadlines remain bounded, with original text shown if work is skipped.

## Install the Mac development build

Build and stage the package first using [BUILD.md](../BUILD.md).

1. Quit OBS completely with Command-Q.
2. Run `Install Awl Interpretwitch.command` in the project root.
3. Open OBS and select **Docks → Awl Interpretwitch** (Russian: **Awl Интерпретвич**).
4. On first use, complete **Set up Twitch** as described below. If an ID is already
   saved, the dock opens normally. Setup can also be opened from **Settings**.
5. Choose **Resource usage**, then click **Download model**. Light downloads
   83.4 MB; Heavy downloads 496.8 MB. Models are not in the plugin package. The
   button verifies and repairs partial/damaged downloads for the selected model.
6. Enter the channel name and click **Connect Twitch**. Approve read-only chat
   access on Twitch's page.

This package targets Apple Silicon, macOS 14+, OBS **32.2.2** and its Qt **6.11.1**.
It is locally ad-hoc signed, not Developer ID signed/notarized. Older AWL Chat bundles
are backed up outside OBS's plugin search directory. `--check` only verifies the
package; it does not install or change settings.

## One-time Twitch application setup

For this development build, register an application once. This identifies the
plugin to Twitch; authorizing it lets the plugin in your local OBS receive remote
Twitch chat messages. It requests read-only chat access, cannot send messages or
start a stream, and translates on your computer. You do not need to write code or
run a server. This is independent of OBS's own streaming-account login.

All four steps are on one scrollable page, with **Save** and **Cancel** always
available. **I already have a Client ID** scrolls to the input on that same page.
Opening setup makes no network request. Only explicit browser-button clicks open
Twitch's developer console.

1. **Open the developer console.** Use the guide's button to open
   [Twitch's console](https://dev.twitch.tv/console/apps), sign in, then choose
   **Applications → Register Your Application**. Twitch requires a verified email
   and two-factor authentication.
2. **Register a Public application.** Fill in the following fields:

   | Field | Value |
   | --- | --- |
   | Name | Awl Interpretwitch followed by your channel name (must be unique) |
   | OAuth Redirect URLs | `https://localhost` |
   | Category | Chat Bot |
   | Client Type | Public |

   The guide provides **Copy redirect URL**. Click **Add** beside the URL and remove
   any empty URL rows, complete verification, then **Create**. The URL is a
   registration placeholder: device authorization needs no local callback server
   or certificate, and you do not open the URL.
3. **Copy your Client ID.** Use **Open your applications**, find the app, click
   **Manage**, then copy its **Client ID** into the guide. Only the public ID is
   needed, never a secret, token or password. The plugin checks its format; Twitch
   validates the actual registration when you connect. Existing apps and IDs still
   work after an update or rename.
4. **Authorize read-only chat access.** Choose **Save**, enter the channel to read,
   then **Connect Twitch** and approve chat access in the browser. Saving alone
   starts neither sign-in nor a model download. **Cancel** discards the draft.
   Editing registration is disabled while connected.

The guide's browser-field names follow [Twitch's registration documentation](https://dev.twitch.tv/docs/authentication/register-app/).
Russian instructions retain the field names shown in Twitch's English console so
users can find them. Twitch and OS/browser dialogs control their own language.

A distributable release can include the project's registered public Client ID,
so users only need Connect Twitch. This checkout does not invent a Client ID or
reuse OBS's stored Twitch credentials. Twitch may require two-factor authentication
on the developer account to register an application.

Device authorization requests only `user:read:chat`. The browser displays Twitch's
authorization page; the dock also shows the activation code and a button to reopen
it. Remember sign-in stores the access/refresh tokens in macOS Keychain or Windows
Credential Manager, separately from ordinary settings. Tokens are validated at
connection and at least hourly, rotated on refresh, and removed on Disconnect.
If saving credentials fails, the connection is session-only. Linux currently has
session-only credentials. No access tokens, refresh tokens or chat text are logged.

Protocol: https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/#device-code-grant-flow
and https://dev.twitch.tv/docs/eventsub/handling-websocket-events/

## Reading chat

- **Show original** toggles the original under a translation or waiting indicator.
- **Pause** stops new translation work; original messages continue.
- Scroll upward to read earlier messages. **Latest messages** returns to the end.
- Drag to select words or several messages. Copy with **Command-C** on Mac,
  **Ctrl-C** on Windows, or the text's right-click menu. Selection stops automatic
  scrolling. Selected messages retain their displayed wording until selection is
  cleared, so an arriving translation does not change what you are copying.
  Moderation deletions and the 200-message retention limit still apply immediately.
- **Clear** clears text and waiting translations immediately.
- Twitch message deletions, user purges and chat clears also remove local rows.
- **Settings** reveals channel, Disconnect, Resource usage, Run on and sign-in settings.
  The controls scroll within a short dock.
  Changing channels takes effect on the next connection. Disconnect first.
- **Run on** selects CPU or GPU (NVIDIA). The choice is saved. When a model is
  loaded, changing the device releases it before loading the replacement, without
  restarting OBS or reconnecting Twitch. Original messages continue to appear
  during loading; queued old-device translations are cancelled. Existing history
  stays visible. Before first use, the choice is only saved, with no model loaded.

The native text view uses a Twitch-style dark background and inline bold usernames.
It uses Twitch's supplied name colour, adjusted only when too dark to read; users
without a colour receive a stable fallback. Emotes remain text, with no extra image
downloads or embedded browser. Rendering updates individual message blocks and
retains at most 200; text is inserted literally, never interpreted as HTML.

Light translates confidently identified English and leaves mixed Cyrillic text
unchanged. Heavy also translates confidently detected languages supported by
M2M100. Both leave Russian, unsupported and ambiguous short messages unchanged.
Common English greetings are recognized explicitly. Twitch's structured emotes,
mentions and links stay untouched. A small list of common third-party emote names
is also preserved; arbitrary BTTV/7TV catalogues are not downloaded or guaranteed.
Short text portions separated by protected fragments are translated separately,
which can reduce grammatical fluency. Either model can translate gaming terms
too literally; the original remains available.

## Resource limits

Translation defaults to native CTranslate2 INT8 on **CPU**, with two inference
threads for Light, one for Heavy and one resident model. **GPU (NVIDIA)** uses CUDA device 0 and prefers
INT8 weights with FP16 operations where supported; CTranslate2 selects a supported
fallback precision on older cards. It uses the same downloaded model files.
Language detection, tokenization and the interface still use CPU.

GPU requires a CUDA-enabled CTranslate2 library, its matching CUDA runtime
dependencies and a compatible NVIDIA driver. NVIDIA execution and gameplay impact
have not been tested. A Windows package with those dependencies is still needed.
This engine has no Metal backend, so the GPU choice is disabled on Mac.

Before reporting GPU ready, the loader runs a tiny fixed-input decode to catch
lazily loaded CUDA-library failures. If GPU startup fails, the worker loads CPU
translation and the settings show both the actual CPU device and an explanation
(technical detail in the tooltip). The saved GPU preference is retained for the
next load. CPU fallback does not hide missing or invalid model files. This is
startup recovery; a later per-message inference error shows the original text.

There is no Python runtime, audio processing or Whisper model in this feature.
OBS's existing Qt interface libraries render the dock. GPU mode shares graphics
resources with games and encoding and is not guaranteed to be faster for chat.

Limits: 200 displayed messages, 24 waiting translations, a 15-second queue age and
generation deadline, 256 cached translations, 1,024 recent EventSub envelope IDs,
64 KiB incoming events, 256 input/output tokens per translated portion and 16
portions per message. Queue overload shows original text instead of a late
translation. The separate worker is terminated on plugin shutdown, cancellation
or a missed deadline. Cached models remain on disk. A force-stopped download can
leave an uncommitted temporary file; incomplete files are never loaded as models.
Ordinary inference crashes are isolated, but faults in the dock, OS or a graphics
driver can still affect OBS. This is process isolation, not a complete security sandbox.

All translation happens locally. Twitch chat/authentication and explicit model
downloads require the internet. Chat history is memory-only, bounded, and cleared
when the dock is destroyed. No telemetry, transcript files or background service.

## Development and review

See [build instructions](../BUILD.md), the [source map](ARCHITECTURE.md),
[process isolation](ISOLATION.md) and [dependency notices](THIRD_PARTY_NOTICES.md).
Windows packaging, NVIDIA execution and concurrent gameplay remain unvalidated.
