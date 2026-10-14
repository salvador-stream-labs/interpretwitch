# Twitch chat adapter

Interpretwitch 0.8.1 uses OBS’s built-in Twitch Chat dock as its only chat source.
The owner confirmed the preceding bridge in a signed-in Mac OBS session. Twitch
continues to own original messages, emotes, settings and moderation controls.

## Controls and presentation

Use **Tools → Interpretwitch** for models, CPU/GPU choice, enable/disable and
appearance. Translation defaults to enabled and respects the saved choice. The
separate Interpretwitch chat dock and developer registration flow are removed.
Twitch’s own webpage settings menu is not modified.

The translation follows the username at normal size. The original appears below
in smaller type with an optional compact source-language label (such as **АН**).
An optional faint background groups the pair. The tooltip gives the full language
name in the selected translation language. Russian uses reviewed, distinct 2–3
letter abbreviations (АН, ФР, НЕМ), English uses ISO codes, and other targets use
shortened localized names. These identify the detector/routing result, not a
guarantee of accuracy. They are display labels; internal model codes stay unchanged.
Unknown/skipped results receive no pill or container.

Original message nodes remain in their existing parents, with links, mentions and
emote handlers intact. The adapter inserts the translation and a line separator,
then adds removable sizing attributes to original content. If unfamiliar markup
mixes identity or button controls into that content, it keeps the original layout
and appends the translation below instead.

Before each batch of layout edits, the adapter checks the nearest scrollable
chat ancestor. If it was at the bottom (within 2 CSS pixels), it stays at the
bottom after waiting indicators, translations, appearance changes or cleanup
alter the content height. This also covers the first message that makes chat
overflow. When the viewer has scrolled back, the adapter does not write the
scroll position. Adjustments are synchronous, with no scroll listener, polling,
extra timer or deferred scroll that could override a later user action.

Appearance uses inherited fonts/colour and a faint `currentColor` mix for both
light and dark themes. Updating appearance does not run the model again. Added
text uses `textContent`, never HTML. Disabling removes the injected stylesheet,
row attributes and translations without replacing Twitch’s original elements.

## Passive status

A quiet status line is added to the recognized Stream Chat heading, never over
messages, controls or the chat-entry box. It follows OBS's English/Russian UI
language. It shows Connecting, Preparing, Ready, Translating, Recovering or
Unavailable. Its tooltip includes the last completed bridge reply time and the
current model/worker detail. This is observed state, not a certificate of model
accuracy or proof that OBS, its browser or a driver cannot fail.

The monitor uses existing socket, worker and result events plus the existing
20-second request deadline. There are no periodic health probes, new background
timers, idle model runs, animation or ticking timestamps. An idle model that
becomes unresponsive is detected when the next request misses its deadline.
If Twitch's header markup is unrecognized, no status element is inserted;
the native settings window still shows connection/model state.

An adapter callback exception, lost socket or unanswered request stops the page
adapter and restores original chat. A failed status remains until reconnect or
disable. During helper recovery, handled row signatures are retained so old
messages, including the triggering message, are not replayed. Changing target
explicitly clears them so the chosen language can be applied to recent history.

## Existing conversations

On the first nonempty scan after the page/model becomes ready, only the latest
five existing message rows are eligible for catch-up. This includes history that
Twitch populates after attachment. Older row text is remembered in a WeakMap so
later scans or removal of newer rows cannot start translating it. Weak keys do
not keep discarded DOM nodes alive; changed/recycled message content is eligible
again. New arrivals after that first batch use the normal bounded live queue.

Reconnect, model loading/recovery and target changes use the same five-message
limit. Already handled messages remain suppressed during recovery. There is no
added timer, model call, saved chat history or background catch-up job. The page
adapter cannot distinguish history from live messages in Twitch's first batch;
if that batch contains more than five live arrivals, only its latest five are
eligible. Subsequent live arrivals are unaffected.

## Boundaries

`twitch_dock.cpp` attaches only to OBS’s `twitchChat` browser dock with ABI 3. The
script checks the exact Twitch origin and standard `/popout/<channel>/chat` path.
Custom browser docks and other pages are not supported.

A MutationObserver coalesces changes; there is no perpetual DOM polling. At most
200 recent message rows, 24 submitted requests per one-second window and 24 pending
translations are tracked for live work, separately from the weak history snapshot.
Each input is limited to 2,000 UTF-16 characters and
128 fragments. Links, mentions and emotes are protected from translation.

A WebSocket listens on **127.0.0.1 only**, using a random free port and a fresh
unpersisted capability token. Only the exact `https://www.twitch.tv` origin,
correct token and one client are accepted. Frames/messages and outgoing buffering
are bounded; malformed, binary, excessive or oversized input disconnects.

The bridge accepts text translation requests, not filesystem paths or commands.
Scripts already running in the attached Twitch page share access to its temporary
capability, so this is not a full browser sandbox. No passwords, cookies, browser
storage, Twitch tokens or internal React/network hooks are read.

Target changes clear old rows/requests before new inference starts. The page receives
the chosen target for text language/direction and pill names. Results must match
a still-connected row with unchanged original content.
Deleted, hidden, blurred, changed and recycled rows lose additions; late replies
cannot create a new row. Disconnect clears pending IDs. At most 15 attachment
attempts are made per enable/reconnect, spaced two seconds apart while disconnected;
successful handshakes do not replenish this allowance. Use **Reconnect to chat** after opening a missing dock
or reconnecting OBS’s Twitch account. Model loading waits for the page connection.

## Limits and validation

This is a browser-page adapter, not a supported Twitch chat-event API. Twitch,
BetterTTV and FrankerFaceZ markup changes may require updates. Known moderation
states are tested; full parity with every filter or extension is not claimed.

The native tests cover origin/token rejection, one client, bounds, settings,
source-language routing, stale replies and shutdown. An offline Twitch-origin
Chromium fixture covers styling, live-chat scrolling above a fixed entry box,
reading history during delayed replies, input focus, selection, deletion,
hidden/recycled rows, five-message catch-up from 250 existing rows, delayed history,
reconnect/model recovery and a 200-message live burst. Browser scroll
anchoring is disabled in the fixture to expose missing adjustments.
It grants local-network permission only to
its temporary test context and changes no OBS/browser settings.

The 0.8.1 header selector matches Twitch's current `h4` and `data-test-selector`
markup. Its status layout was checked against the public Twitch header and the
offline fixture. Live Windows operation and the corrected health display in
signed-in OBS still need acceptance checks. The owner confirmed the preceding
layout and scrolling.
Native code can fail despite these tests; see
[ISOLATION.md](ISOLATION.md). There is no telemetry. OBS's log receives
state-only `[Interpretwitch]` diagnostics: bridge connections, rejections and
disconnects, the page's reason for stopping (`timeout`, `layout-error` or
`bad-reply`, with the JavaScript error type and message for layout errors), worker
lifecycle and model state. Chat text, translations, usernames and the capability
token are never logged.
