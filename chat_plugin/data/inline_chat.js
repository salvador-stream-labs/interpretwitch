/* SPDX-License-Identifier: GPL-2.0-or-later */
function attachInterpretwitch(config) {
    "use strict";
    const supportedPage = () => location.origin === "https://www.twitch.tv" &&
        /^\/popout\/[a-zA-Z0-9_]{1,25}\/chat\/?$/.test(location.pathname);
    if (!supportedPage()) return;
    if (window.__awlInterpretwitch?.endpoint === config.endpoint) return;
    window.__awlInterpretwitch?.stop();

    const rows = new Map();
    const pending = new Map();
    let history = new WeakMap(), historyPending = true;
    const rowSelector = '[data-a-target="chat-line-message"], .chat-line__message';
    const fragmentSelector = '.text-fragment, .mention-fragment, .link-fragment, ' +
        '[data-a-target="chat-message-text"], [data-a-target="chat-message-mention"], img.chat-image';
    const authorSelector = '.chat-author__display-name, [data-a-target="chat-message-username"]';
    const injectedSelector = '[data-awl-translation], [data-awl-original], [data-awl-health]';
    let ready = false, stopped = false, scheduled = 0;
    let budgetStarted = 0, submitted = 0;
    let showOriginal = true, showLanguage = true, showBackground = true;
    let healthText = config.health.waiting, healthDetail = '', lastReply = '';
    const health = document.createElement('span');
    health.dataset.awlHealth = '';
    const style = document.createElement('style');
    style.dataset.awlStyle = '';
    style.textContent = `
        [data-awl-group] { background-color:color-mix(in srgb,currentColor 4%,transparent); border-radius:6px;
            box-shadow:inset 0 0 0 1px color-mix(in srgb,currentColor 7%,transparent); }
        [data-awl-translation] { display:block; font:inherit; font-size:.94em; margin-top:.3em; white-space:pre-wrap; }
        [data-awl-translation][data-awl-primary] { display:inline; font-size:1em; margin:0; }
        [data-awl-source], [data-awl-original] { font-size:.82em; }
        [data-awl-original]::before { content:''; display:block; margin-top:.2em; }
        [data-awl-hide-original] [data-awl-source], [data-awl-hide-original] [data-awl-original] { display:none !important; }
        [data-awl-language] { display:inline-block; font:inherit; font-size:.8em; line-height:1.5; font-weight:400;
            padding:0 .4em; margin-inline-end:.6em; border-radius:999px; vertical-align:.08em;
            background:color-mix(in srgb,currentColor 5%,transparent); }
        [data-awl-language][hidden] { display:none; }
        [data-awl-translation] > [data-awl-language]:last-child { font-size:.66em; margin-inline-start:.6em; margin-inline-end:0; }
        [data-awl-health] { display:block; font:normal 11px/1.4 sans-serif; opacity:.65; white-space:normal; }
    `;
    document.head.appendChild(style);
    let targetLanguage = 'ru', languageLabels = {}, languageNames;
    const localeCode = code => code === 'ns' ? 'nso' : code;
    function setLanguageNames() {
        try { languageNames = new Intl.DisplayNames([localeCode(targetLanguage)], {type:'language'}); }
        catch { languageNames = undefined; }
    }
    setLanguageNames();
    const socket = new WebSocket(config.endpoint);
    function showHealth(failed = false) {
        // Use the existing header only. Never overlay the entry box or alter
        // Twitch controls if a future page has unfamiliar markup.
        if (!health.isConnected)
            document.querySelector('[data-test-selector="chat-room-header-label"], #chat-room-header-label, ' +
                '[data-a-target="chat-room-header-label"], .stream-chat-header h2, .stream-chat-header h3')?.append(health);
        const label = failed ? config.health.failed : pending.size ? config.health.busy : healthText;
        const text = config.health.name + ' · ' + label;
        const detail = (failed ? config.health.detail : healthDetail) +
            (lastReply ? '\n' + config.health.checked.replace('%1', lastReply) : '');
        if (health.textContent !== text) health.textContent = text;
        if (health.title !== detail) health.title = detail;
    }
    function visible(element) {
        if (!element.isConnected || !element.getClientRects().length || element.closest('[hidden], [aria-hidden="true"]')) return false;
        for (let node = element; node; node = node.parentElement) {
            const style = getComputedStyle(node);
            if (style.visibility !== 'visible' || Number(style.opacity) === 0 || style.filter.includes('blur(')) return false;
        }
        return true;
    }
    function content(row) {
        if (!visible(row) || row.matches('.chat-line__message--deleted, .chat-line__message--blocked') ||
            row.querySelector('[data-a-target="chat-deleted-message"], [data-test-selector="chat-deleted-message-placeholder"]')) return null;
        const fragments = [...row.querySelectorAll(fragmentSelector)].filter(element =>
            !element.closest(injectedSelector) && visible(element));
        const outer = fragments.filter(element => !fragments.some(other => other !== element && other.contains(element)));
        const parts = outer.map(element => ({text: element.tagName === 'IMG' ? element.alt : element.textContent,
                protected: element.tagName === 'IMG' || element.tagName === 'A' ||
                    element.matches('.mention-fragment, .link-fragment, [data-a-target="chat-message-mention"]')}))
            .filter(part => part.text);
        if (!parts.length || parts.length > 128 || parts.reduce((n, part) => n + part.text.length, 0) > 2000) return null;
        const author = row.querySelector(authorSelector)?.textContent || '';
        return {parts, fragments: outer, signature: JSON.stringify([location.pathname, row.getAttribute('data-id'), author, parts])};
    }
    function translationFirst(record, fragments) {
        // Insert only our nodes. Never move/replace React-owned message elements.
        let parent = fragments[0].parentElement;
        while (!fragments.every(node => parent.contains(node))) parent = parent.parentElement;
        const branches = [...new Set(fragments.map(node => {
            while (node.parentElement !== parent) node = node.parentElement;
            return node;
        }))];
        const first = branches[0], last = branches.at(-1);
        const siblings = [...parent.childNodes];
        const source = siblings.slice(siblings.indexOf(first), siblings.indexOf(last) + 1);
        // Unknown markup containing identity/controls must retain Twitch's layout.
        if (source.some(node => node.nodeType === Node.ELEMENT_NODE &&
            (node.matches(authorSelector + ', button') || node.querySelector(authorSelector + ', button')))) return;
        // Bare text cannot be hidden with CSS without moving Twitch-owned nodes.
        if (source.some(node => node.nodeType === Node.TEXT_NODE && node.textContent.trim())) return;
        record.source = source.filter(node => node.nodeType === Node.ELEMENT_NODE && node !== record.slot);
        const lead = document.createElement('span');
        lead.dataset.awlOriginal = '';
        if (record.badge) lead.append(record.badge);
        record.lead = lead;
        record.slot.dataset.awlPrimary = '';
        parent.insertBefore(record.slot, first);
        parent.insertBefore(lead, first);
        for (const node of record.source) node.setAttribute('data-awl-source', '');
    }
    function remove(row) {
        const record = rows.get(row);
        if (!record) return;
        pending.delete(record.id);
        clearTimeout(record.timeout);
        record.slot?.remove();
        record.lead?.remove();
        for (const node of record.source || []) node.removeAttribute('data-awl-source');
        row.removeAttribute('data-awl-group');
        row.removeAttribute('data-awl-hide-original');
        rows.delete(row);
    }
    function clear() { for (const row of rows.keys()) remove(row); }
    function suspend() {
        // Restore originals but remember handled rows. A recovering helper must
        // not replay the very message that crashed it, or old completed history.
        for (const [row, record] of [...rows]) {
            remove(row);
            rows.set(row, {id: record.id, signature: record.signature});
        }
    }
    function scrollAdjustment() {
        let scroller;
        for (let node = document.querySelector(rowSelector)?.parentElement;
            node && node !== document.body; node = node.parentElement) {
            if (node.clientHeight > 0 && /^(auto|scroll)$/.test(getComputedStyle(node).overflowY)) {
                scroller = node;
                break;
            }
        }
        const following = scroller && scroller.scrollHeight - scroller.clientHeight - scroller.scrollTop <= 2;
        return () => { if (following && scroller.isConnected) scroller.scrollTop = scroller.scrollHeight; };
    }
    function preserveChatScroll(update) {
        return (...args) => {
            if (stopped) return;
            // Twitch follows native arrivals before our later layout edits.
            // Measure once per batch, including when chat does not overflow yet.
            try {
                const restore = scrollAdjustment();
                update(...args);
                if (!stopped) showHealth();
                // No deferred scroll can overtake a later user action.
                restore();
            } catch (error) {
                // Page-layout errors must leave native chat usable. Report only
                // the error type/message, never row text.
                stop(true, 'layout-error', `${error?.name}: ${error?.message}`);
            }
        };
    }
    const scan = preserveChatScroll(() => {
        scheduled = 0;
        if (stopped) return;
        if (!supportedPage()) { stop(); return; }
        // Only inspect a bounded set of existing chat rows, never the account UI.
        const existing = [...document.querySelectorAll(rowSelector)];
        const candidates = existing.slice(-200);
        const contents = new Map();
        // Inspect native visibility, including Twitch's own hiding/moderation.
        // Lift our CSS once per synchronous batch, never once per row. Restoring
        // it before returning prevents a visible flash and avoids feedback from
        // our hidden originals. No native nodes or inline styles are changed.
        try {
            if (!showOriginal) style.sheet.disabled = true;
            for (const row of candidates) {
                if (!rows.has(row) && history.has(row) && history.get(row) === row.textContent) continue;
                contents.set(row, content(row));
            }
        } finally { style.sheet.disabled = false; }
        for (const [row, record] of rows) {
            const value = contents.get(row);
            if (!value || value.signature !== record.signature ||
                (record.translated && (!record.slot.isConnected || record.lead?.isConnected === false ||
                    record.source?.some(node => !node.isConnected)))) remove(row);
        }
        if (!ready || document.hidden || socket.readyState !== WebSocket.OPEN) return;
        if (historyPending && existing.length) {
            // Catch only the latest five when a page/model becomes ready, even
            // if Twitch loads its history after attachment. Weak keys do not
            // retain old DOM nodes; changed/recycled messages remain eligible.
            history = new WeakMap(existing.slice(0, -5).map(row => [row, row.textContent]));
            historyPending = false;
        }
        const now = performance.now();
        if (now - budgetStarted >= 1000) { budgetStarted = now; submitted = 0; }
        for (const row of candidates) {
            if (rows.has(row) || pending.size >= 24) continue;
            if (history.has(row)) {
                if (history.get(row) === row.textContent) continue;
                history.delete(row);
            }
            const value = contents.get(row);
            if (!value) continue;
            // Pace existing history too: fast language skips must not flood the bridge.
            if (submitted >= 24) { schedule(Math.max(50, 1000 - (now - budgetStarted))); break; }
            const id = crypto.randomUUID();
            const slot = document.createElement('span');
            slot.dataset.awlTranslation = id;
            slot.lang = localeCode(targetLanguage);
            slot.dir = 'auto';
            slot.textContent = config.waiting;
            slot.title = 'Interpretwitch';
            const record = {id, signature: value.signature, slot, timeout: 0};
            rows.set(row, record);
            pending.set(id, row);
            row.appendChild(slot);
            // Existing request deadline doubles as a health check. No idle ping.
            record.timeout = setTimeout(() => stop(true, 'timeout'), 20000);
            socket.send(JSON.stringify({type: 'translate', id, parts: value.parts}));
            submitted++;
        }
    });
    function schedule(delay = 50) {
        if (!stopped && !scheduled) scheduled = setTimeout(scan, typeof delay === 'number' ? delay : 50);
    }
    function appearance() {
        for (const [row, record] of rows) {
            row.toggleAttribute('data-awl-group', showBackground && !!record.translated);
            row.toggleAttribute('data-awl-hide-original', !showOriginal && !!record.lead);
            if (record.badge) {
                record.badge.hidden = !showLanguage;
                const parent = showOriginal && record.lead ? record.lead : record.slot;
                if (record.badge.parentElement !== parent) parent.append(record.badge);
                if (showOriginal && !record.lead && parent.firstElementChild !== record.badge) parent.prepend(record.badge);
                else if (!showOriginal && parent.lastElementChild !== record.badge) parent.append(record.badge);
            }
        }
    }
    const observer = new MutationObserver(mutations => {
        // Ignore our own text/slot edits to avoid a translation feedback loop.
        const external = mutations.some(m => {
            const target = m.target.nodeType === Node.ELEMENT_NODE ? m.target : m.target.parentElement;
            if (target?.closest(injectedSelector)) return false;
            if (m.type === 'childList') {
                const nodes = [...m.addedNodes, ...m.removedNodes];
                if (nodes.length && nodes.every(n => n.nodeType === Node.ELEMENT_NODE && n.matches(injectedSelector))) return false;
            }
            return true;
        });
        if (external) schedule();
    });
    function stop(failed = false, reason = '', detail = '') {
        if (!stopped) {
            stopped = true;
            clearTimeout(scheduled);
            observer.disconnect();
            document.removeEventListener('visibilitychange', schedule);
            window.removeEventListener('pagehide', pagehide);
            socket.onclose = socket.onerror = null;
            // Tell the plugin why, for its log. No chat content is included.
            if (failed && reason && socket.readyState === WebSocket.OPEN) {
                try {
                    socket.send(JSON.stringify({type: 'stopping', reason, pending: pending.size,
                        detail: String(detail).slice(0, 200)}));
                } catch {}
            }
            socket.close();
            const restore = scrollAdjustment();
            clear();
            restore();
        }
        if (failed) {
            showHealth(true);
            if (window.__awlInterpretwitch?.stop === stop) window.__awlInterpretwitch.endpoint = '';
        }
        else {
            health.remove();
            style.remove();
            if (window.__awlInterpretwitch?.stop === stop) delete window.__awlInterpretwitch;
        }
    }
    const pagehide = () => stop();
    socket.onmessage = preserveChatScroll(event => {
        if (stopped || typeof event.data !== 'string' || event.data.length > 65536) return;
        let value;
        try { value = JSON.parse(event.data); } catch { stop(true, 'bad-reply'); return; }
        if (value.type === 'appearance') {
            showOriginal = value.show_original !== false;
            showLanguage = value.language_label === true;
            showBackground = value.background === true;
            appearance();
            return;
        }
        if (value.type === 'state') {
            if (typeof value.target_language === 'string' && /^[a-z]{2,3}$/.test(value.target_language)) {
                if (targetLanguage !== value.target_language) { clear(); historyPending = true; }
                targetLanguage = value.target_language;
                languageLabels = value.language_labels || {};
                setLanguageNames();
            }
            ready = value.ready === true;
            healthText = typeof value.health === 'string' ? value.health : config.health.waiting;
            healthDetail = typeof value.detail === 'string' ? value.detail : '';
            if (!ready) { suspend(); historyPending = true; }
            schedule();
            return;
        }
        if (value.type !== 'result') return;
        const row = pending.get(value.id);
        const record = rows.get(row);
        if (!record) return;
        pending.delete(value.id);
        clearTimeout(record.timeout);
        lastReply = new Date().toLocaleTimeString(config.locale);
        const current = content(row);
        if (!current || current.signature !== record.signature || !record.slot.isConnected || !ready) {
            remove(row);
        } else if (typeof value.text === 'string' && value.text.length <= 8192 && value.text) {
            const translation = document.createElement('span');
            translation.textContent = value.text;
            record.slot.replaceChildren(translation);
            record.translated = true;
            if (typeof value.source_language === 'string' && /^[a-z]{2,3}$/.test(value.source_language)) {
                const badge = document.createElement('span');
                badge.dataset.awlLanguage = value.source_language;
                badge.lang = localeCode(targetLanguage);
                const code = value.source_language;
                const name = languageNames?.of(localeCode(code)) || code;
                // Russian has reviewed, distinct 2–3 letter UI abbreviations.
                // Other targets use localized short names; English uses ISO codes.
                badge.textContent = languageLabels[code] || (targetLanguage === 'en' || name === code
                    ? code.toUpperCase() : [...name].slice(0, 3).join('').toLocaleUpperCase(localeCode(targetLanguage)));
                badge.title = (config.detected || 'Detected source language') + ': ' + name;
                badge.setAttribute('aria-label', name);
                record.slot.prepend(badge);
                record.badge = badge;
            }
            translationFirst(record, current.fragments);
            appearance();
        } else {
            record.slot.remove();
        }
        schedule();
    });
    socket.onclose = socket.onerror = () => stop(true);
    observer.observe(document.documentElement, {subtree: true, childList: true, characterData: true,
        attributes: true, attributeFilter: ['class', 'style', 'hidden', 'aria-hidden', 'data-id']});
    document.addEventListener('visibilitychange', schedule);
    window.addEventListener('pagehide', pagehide);
    window.__awlInterpretwitch = {endpoint: config.endpoint, stop};
}
