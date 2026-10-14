// SPDX-License-Identifier: GPL-2.0-or-later
// Developer-only Chromium test; Playwright is not a plugin dependency.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');

(async () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'interpretwitch-dom-'));
    const scriptPath = path.join(directory, 'attach.js');
    const fixture = spawn(process.argv[2], ['--fixture', scriptPath], {
        env: {...process.env, QT_QPA_PLATFORM: 'offscreen'}, stdio: ['ignore', 'ignore', 'pipe']
    });
    let browser, page;
    const diagnostics = [];
    fixture.stderr.on('data', data => {
        if (diagnostics.length < 10) diagnostics.push(data.toString().slice(0,500));
    });
    try {
        const started = Date.now();
        while (!fs.existsSync(scriptPath)) {
            if (fixture.exitCode !== null || Date.now() - started > 5000) throw Error('Bridge fixture failed to start');
            await new Promise(resolve => setTimeout(resolve, 20));
        }
        browser = await chromium.launch({headless: true});
        const context = await browser.newContext({viewport: {width: 420, height: 680}, deviceScaleFactor: 2});
        await context.addInitScript(() => {
            const Socket = WebSocket;
            window.WebSocket = class extends Socket {
                constructor(...args) { super(...args); window.testSocket = this; }
                send(data) {
                    (window.sentRequests ||= []).push(JSON.parse(data));
                    return super.send(data);
                }
            };
        });
        // Current standalone Chromium asks for local-network permission. OBS
        // ships its own CEF version; this is a fixture permission, not a browser
        // security flag or a change to OBS/Twitch configuration.
        await context.grantPermissions(['local-network-access'], {origin: 'https://www.twitch.tv'});
        page = await context.newPage();
        page.setDefaultTimeout(8000);
        page.on('console', message => {
            if (message.type() === 'error' && diagnostics.length < 10)
                diagnostics.push(message.text().replace(/ws:\/\/127\.0\.0\.1:\d+\/[a-f0-9]+/g, 'ws://localhost/<temporary-token>'));
        });
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        // Twitch origin is simulated offline. All translation traffic uses the
        // real C++ loopback bridge. No Twitch login or network request is made.
        await page.route('https://www.twitch.tv/**', route => route.fulfill({contentType: 'text/html', body: `
            <html><head><style>body{background:#18181b;color:#efeff1;font:16px sans-serif;margin:0;height:100vh;display:flex;flex-direction:column}
            #scrollport{flex:1;min-height:0;overflow-y:auto;overflow-anchor:none}
            #composer{flex:none;height:84px;padding:12px;box-sizing:border-box;background:#242429}input{width:90%}
            header{height:50px;box-sizing:border-box;text-align:center;display:flex;align-items:center;justify-content:center;position:relative;flex:none}
            header button{position:absolute;right:10px;top:12px}h4{margin:0;font-size:18px}
            .chat-line__message{padding:8px 10px;margin:4px 0;line-height:1.5}.chat-author__display-name{color:#679fa7;margin-right:5px}.chat-line__message--deleted .text-fragment{display:none}</style></head>
            <body><header class="stream-chat-header"><div><h4 data-test-selector="chat-room-header-label" id="chat-room-header-label">Stream Chat</h4></div><button data-test-selector="chat-viewer-list">Viewers</button></header><div id="scrollport"><main id="chat"></main></div><footer id="composer"><input placeholder="Send a message"></footer></body></html>`}));
        await page.goto('https://www.twitch.tv/popout/fixture/chat');
        const script = fs.readFileSync(scriptPath, 'utf8');
        await page.evaluate(script);
        const add = async (id, text, cls = '') => page.evaluate(({id, text, cls}) => {
            // Twitch follows native arrivals, before an asynchronous translation
            // changes their height. Browser scroll anchoring is disabled above
            // so the fixture cannot hide a missing adapter adjustment.
            const port = document.querySelector('#scrollport');
            const following = port.scrollHeight - port.clientHeight - port.scrollTop <= 2;
            const row = document.createElement('div');
            row.className = 'chat-line__message ' + cls;
            row.id = id; row.dataset.id = id;
            const author = document.createElement('b');
            author.className = 'chat-author__display-name'; author.textContent = 'softkrtl';
            const message = document.createElement('span'); message.dataset.aTarget = 'chat-line-message-body';
            const body = document.createElement('span'); body.className = 'text-fragment'; body.textContent = text;
            message.append(body); row.append(author, ': ', message); document.querySelector('#chat').append(row);
            if (following) port.scrollTop = port.scrollHeight;
        }, {id, text, cls});
        const translated = async id => page.waitForFunction(id =>
            document.querySelector('#' + id + ' [data-awl-primary]'), id);
        const batch = async (prefix, count) => page.evaluate(({prefix, count}) => {
            const chat = document.querySelector('#chat');
            for (let i = 0; i < count; i++) {
                const row = document.createElement('div');
                row.className = 'chat-line__message'; row.id = prefix + i; row.dataset.id = row.id;
                const text = document.createElement('span');
                text.className = 'text-fragment'; text.textContent = prefix + ' message ' + i;
                row.append(text); chat.append(row);
            }
        }, {prefix, count});
        const setReady = async ready => page.evaluate(ready => {
            window.testSocket.dispatchEvent(new MessageEvent('message', {
                data: JSON.stringify({type:'state', ready, health:ready ? 'Ready' : 'Preparing'})
            }));
        }, ready);
        await add('first', 'Hello from chat');
        await translated('first');
        assert.equal(await page.locator('#first .text-fragment').textContent(), 'Hello from chat');
        assert.equal(await page.locator('#first [data-awl-translation]').count(), 1);
        assert.equal(await page.locator('#first [data-awl-language]').textContent(), 'АН');
        assert.match(await page.locator('#first [data-awl-language]').getAttribute('title'), /английский/);
        assert.equal(await page.locator('#first[data-awl-group]').count(), 1);
        assert.equal(await page.locator('[data-awl-health]').count(), 1, 'Current Twitch header must receive the status');
        assert.equal(await page.locator('[data-awl-health]').textContent(), 'Interpretwitch · Ready');
        const header = await page.locator('.stream-chat-header').boundingBox();
        const status = await page.locator('[data-awl-health]').boundingBox();
        const viewers = await page.locator('[data-test-selector="chat-viewer-list"]').boundingBox();
        assert.ok(status.y >= header.y && status.y + status.height <= header.y + header.height, 'Status must fit inside the fixed header');
        assert.ok(status.x + status.width <= viewers.x, 'Status must leave the viewer-list control clear');
        await page.locator('[data-test-selector="chat-viewer-list"]').evaluate(button => {
            button.onclick = () => { window.viewerClicks = (window.viewerClicks || 0) + 1; };
        });
        await page.locator('[data-test-selector="chat-viewer-list"]').click();
        assert.equal(await page.evaluate(() => window.viewerClicks), 1);
        assert.match(await page.locator('[data-awl-health]').getAttribute('title'), /Last reply:/);
        const idleRequests = await page.evaluate(() => window.sentRequests.length);
        const idleHealth = await page.locator('[data-awl-health]').getAttribute('title');
        await page.waitForTimeout(350);
        assert.equal(await page.evaluate(() => window.sentRequests.length), idleRequests, 'Idle health must not send probes');
        assert.equal(await page.locator('[data-awl-health]').getAttribute('title'), idleHealth, 'Health timestamp must not tick');
        const layout = async id => page.locator('#' + id).evaluate(row => {
            const author = row.querySelector('.chat-author__display-name');
            const primary = row.querySelector('[data-awl-primary]');
            const original = row.querySelector('.text-fragment');
            return {authorTop: author.getBoundingClientRect().top, primaryTop: primary.getBoundingClientRect().top,
                originalTop: original.getBoundingClientRect().top,
                primarySize: parseFloat(getComputedStyle(primary).fontSize),
                originalSize: parseFloat(getComputedStyle(original).fontSize)};
        });
        const firstLayout = await layout('first');
        assert.ok(Math.abs(firstLayout.authorTop - firstLayout.primaryTop) < 2, 'Translation must follow the username');
        assert.ok(firstLayout.originalTop > firstLayout.primaryTop + 10, 'Original must be on the next line');
        assert.ok(firstLayout.originalSize < firstLayout.primarySize, 'Original must be smaller');
        const appearance = async (label, background) => page.evaluate(({label, background}) => {
            window.testSocket.dispatchEvent(new MessageEvent('message', {data: JSON.stringify({
                type: 'appearance', language_label: label, background})}));
        }, {label, background});
        await appearance(false, false);
        assert.equal(await page.locator('#first [data-awl-language]').isVisible(), false);
        assert.equal(await page.locator('[data-awl-group]').count(), 0);
        assert.ok((await layout('first')).originalTop > firstLayout.primaryTop + 10, 'Hiding the pill must retain the original line');
        await appearance(true, true);
        assert.equal(await page.locator('#first [data-awl-language]').isVisible(), true);
        if (process.env.PREVIEW_DIRECTORY) {
            fs.mkdirSync(process.env.PREVIEW_DIRECTORY, {recursive: true});
            await add('preview', 'This is another message in English.');
            await translated('preview');
            await page.screenshot({path: path.join(process.env.PREVIEW_DIRECTORY, 'chat-dark.png')});
            await page.evaluate(() => { document.body.style.background = '#f7f7f8'; document.body.style.color = '#242429'; });
            await page.screenshot({path: path.join(process.env.PREVIEW_DIRECTORY, 'chat-light.png')});
            await page.evaluate(() => { document.body.style.background = ''; document.body.style.color = ''; });
        }
        await page.evaluate(script);
        await page.waitForTimeout(300);
        assert.equal(await page.locator('#first [data-awl-translation]').count(), 1, 'Duplicate injection');

        await add('french', 'Bonjour tout le monde');
        await translated('french');
        assert.equal(await page.locator('#french [data-awl-language]').textContent(), 'ФР');
        assert.equal(await page.locator('#french [data-awl-language]').getAttribute('data-awl-language'), 'fr');
        assert.match(await page.locator('#french [data-awl-language]').getAttribute('title'), /французский/);

        await add('late', 'Delete before translation');
        await page.waitForSelector('#late [data-awl-translation]');
        await page.locator('#late').evaluate(row => row.remove());
        await page.waitForTimeout(300);
        assert.equal(await page.locator('#chat').textContent().then(t => t.includes('Delete before')), false);
        await page.locator('#first').evaluate(row => row.classList.add('chat-line__message--deleted'));
        await page.waitForFunction(() => !document.querySelector('#first [data-awl-translation]'));
        assert.equal(await page.locator('#first[data-awl-group]').count(), 0, 'Deleted row retained group styling');
        assert.equal(await page.locator('#first [data-awl-source], #first [data-awl-original]').count(), 0);

        await add('recycle', 'Old message');
        await page.waitForSelector('#recycle [data-awl-translation]');
        await page.locator('#recycle .text-fragment').evaluate(node => { node.textContent = 'Replacement message'; });
        await page.waitForFunction(() => document.querySelector('#recycle [data-awl-translation] > span:last-child')?.textContent === 'Перевод: Replacement message');
        assert.equal((await page.locator('#recycle').textContent()).includes('Old message'), false);

        // Twitch can replace a message element without changing its text.
        await page.locator('#recycle .text-fragment').evaluate(node => node.replaceWith(node.cloneNode(true)));
        await page.waitForTimeout(400);
        assert.equal(await page.locator('#recycle [data-awl-primary]').count(), 1);
        assert.equal(await page.locator('#recycle [data-awl-original]').count(), 1);

        await add('rich', 'Hello ');
        await page.locator('#rich [data-a-target="chat-line-message-body"]').evaluate(body => {
            const mention = document.createElement('span'); mention.className = 'mention-fragment'; mention.textContent = '@friend ';
            const link = document.createElement('a'); link.className = 'link-fragment'; link.textContent = 'example.test'; link.href = '#link';
            link.onclick = event => { event.preventDefault(); window.linkClicks = (window.linkClicks || 0) + 1; };
            const emote = document.createElement('img'); emote.className = 'chat-image'; emote.alt = ' Kappa'; emote.width = 16; emote.height = 16;
            body.append(mention, link, emote);
            window.richNodes = [mention, link, emote];
        });
        await translated('rich');
        assert.equal(await page.evaluate(() => window.richNodes.every(node =>
            node.parentElement === document.querySelector('#rich [data-a-target="chat-line-message-body"]'))), true);
        await page.locator('#rich a').click();
        assert.equal(await page.evaluate(() => window.linkClicks), 1, 'Native original link handler was lost');

        // Unknown layouts with controls mixed between fragments retain the
        // original ordering rather than resizing/repositioning those controls.
        await add('controls', 'First fragment');
        await page.locator('#controls .text-fragment').evaluate(node => {
            const button = document.createElement('button'); button.textContent = 'Moderate';
            const tail = document.createElement('span'); tail.className = 'text-fragment'; tail.textContent = 'Second fragment';
            node.after(button, tail);
        });
        await page.waitForFunction(() => document.querySelector('#controls [data-awl-translation] > span'));
        assert.equal(await page.locator('#controls [data-awl-primary], #controls [data-awl-source]').count(), 0);

        await add('blocked', 'Hidden user', 'chat-line__message--blocked');
        await add('hidden', 'Hidden content');
        await page.locator('#hidden').evaluate(row => { row.hidden = true; });
        await add('blurred', 'Obscured content');
        await page.locator('#blurred').evaluate(row => { row.style.filter = 'blur(5px)'; });
        await page.waitForTimeout(300);
        assert.equal(await page.locator('#blocked [data-awl-translation], #hidden [data-awl-translation], #blurred [data-awl-translation]').count(), 0);

        await add('literal', '<img src=x onerror="window.bad=1">');
        await translated('literal');
        assert.equal(await page.locator('#literal [data-awl-translation] img').count(), 0);
        assert.equal(await page.evaluate(() => window.bad), undefined);
        await page.locator('#literal').evaluate(row => {
            const range = document.createRange(); range.selectNodeContents(row.querySelector('.text-fragment'));
            getSelection().removeAllRanges(); getSelection().addRange(range);
        });
        const selection = await page.evaluate(() => getSelection().toString());
        await add('newer', 'New message during selection');
        await translated('newer');
        assert.equal(await page.evaluate(() => getSelection().toString()), selection);

        const source = await page.locator('.text-fragment').allTextContents();
        await page.evaluate(() => window.__awlInterpretwitch.stop());
        assert.equal(await page.locator('[data-awl-translation]').count(), 0);
        assert.equal(await page.locator('[data-awl-group], [data-awl-source], [data-awl-original], style[data-awl-style]').count(), 0);
        assert.deepEqual(await page.locator('.text-fragment').allTextContents(), source);
        await page.evaluate(script);
        await translated('newer');
        await page.evaluate(() => document.querySelector('#chat').replaceChildren());
        await page.waitForTimeout(300);
        assert.equal(await page.locator('[data-awl-translation]').count(), 0);
        await batch('live', 200);
        await page.waitForFunction(() => [...document.querySelectorAll('[data-awl-translation]')]
            .filter(node => node.lastElementChild?.textContent.startsWith('Перевод:')).length === 200, null, {timeout: 15000});
        assert.equal(await page.evaluate(() => !!window.__awlInterpretwitch), true, 'Live burst disconnected the bridge');

        // Restarting with history catches up only the latest five. Even history
        // beyond the 200-row work window stays skipped if it later enters it.
        await page.evaluate(() => window.__awlInterpretwitch.stop());
        await page.evaluate(() => document.querySelector('#chat').replaceChildren());
        await batch('history', 250);
        let beforeHistory = await page.evaluate(() => window.sentRequests.length);
        await page.evaluate(script);
        await translated('history249');
        await page.waitForTimeout(250);
        assert.equal(await page.evaluate(() => window.sentRequests.length), beforeHistory + 5);
        assert.deepEqual(await page.locator('[data-awl-primary]').evaluateAll(nodes =>
            nodes.map(node => node.closest('.chat-line__message').id)),
            ['history245', 'history246', 'history247', 'history248', 'history249']);
        await add('liveAfterHistory', 'New chat after startup');
        await translated('liveAfterHistory');
        const afterNew = await page.evaluate(() => window.sentRequests.length);
        await page.evaluate(() => {
            for (const row of [...document.querySelectorAll('.chat-line__message')].slice(150)) row.remove();
        });
        await page.waitForTimeout(300);
        assert.equal(await page.evaluate(() => window.sentRequests.length), afterNew, 'Older history must stay skipped after newer rows disappear');
        await page.locator('#history149 .text-fragment').evaluate(node => { node.textContent = 'A recycled row now has a new message'; });
        await translated('history149');

        // Explicit reconnect and target changes obey the same catch-up limit.
        beforeHistory = await page.evaluate(() => window.sentRequests.length);
        await page.evaluate(() => window.__awlInterpretwitch.stop());
        await page.evaluate(script);
        await translated('history149');
        await page.waitForTimeout(250);
        assert.equal(await page.evaluate(() => window.sentRequests.length), beforeHistory + 5, 'Reconnect replayed history');
        beforeHistory = await page.evaluate(() => window.sentRequests.length);
        await page.evaluate(() => window.testSocket.dispatchEvent(new MessageEvent('message', {
            data: JSON.stringify({type:'state', ready:true, health:'Ready', target_language:'fr'})
        })));
        await page.waitForFunction(() => document.querySelector('#history149 [data-awl-primary]')?.lang === 'fr');
        await page.waitForTimeout(250);
        assert.equal(await page.evaluate(() => window.sentRequests.length), beforeHistory + 5, 'Target change replayed history');

        // Messages accumulating while the model is unavailable are bounded too;
        // already handled rows must still not replay when the helper recovers.
        await setReady(false);
        beforeHistory = await page.evaluate(() => window.sentRequests.length);
        await batch('duringLoad', 80);
        await page.waitForTimeout(200);
        assert.equal(await page.evaluate(() => window.sentRequests.length), beforeHistory);
        await setReady(true);
        await translated('duringLoad79');
        await page.waitForTimeout(250);
        assert.equal(await page.evaluate(() => window.sentRequests.length), beforeHistory + 5, 'Model recovery replayed a backlog');

        // Twitch may populate its first history batch after the bridge connects.
        await page.evaluate(() => window.__awlInterpretwitch.stop());
        await page.evaluate(() => document.querySelector('#chat').replaceChildren());
        await page.evaluate(script);
        await page.waitForTimeout(250);
        beforeHistory = await page.evaluate(() => window.sentRequests.length);
        await batch('delayedHistory', 200);
        await translated('delayedHistory199');
        await page.waitForTimeout(250);
        assert.equal(await page.evaluate(() => window.sentRequests.length), beforeHistory + 5, 'Late history load bypassed the startup limit');
        await add('liveAfterDelay', 'Live chat after delayed history');
        await translated('liveAfterDelay');
        await page.evaluate(() => window.__awlInterpretwitch.stop());
        await page.evaluate(() => document.querySelector('#chat').replaceChildren());
        await page.evaluate(script.replace('"locale":"en-US"', '"locale":"ru-RU"'));
        await add('russianui', 'Hello from another viewer');
        await translated('russianui');
        assert.equal(await page.locator('#russianui [data-awl-language]').textContent(), 'АН');
        assert.match(await page.locator('#russianui [data-awl-language]').getAttribute('title'), /английский/);
        // The same detected source is labelled for the chosen reader language.
        const changeTarget = async target => page.evaluate(target => {
            for (const ready of [false, true]) window.testSocket.dispatchEvent(new MessageEvent('message', {
                data: JSON.stringify({type:'state', ready, target_language:target, language_labels:{}})
            }));
        }, target);
        await changeTarget('en');
        await translated('russianui');
        assert.equal(await page.locator('#russianui [data-awl-language]').textContent(), 'EN');
        assert.equal(await page.locator('#russianui [data-awl-primary]').getAttribute('lang'), 'en');
        assert.match(await page.locator('#russianui [data-awl-language]').getAttribute('title'), /English/);
        await changeTarget('fr');
        await translated('russianui');
        assert.equal(await page.locator('#russianui [data-awl-language]').textContent(), 'ANG');
        assert.match(await page.locator('#russianui [data-awl-language]').getAttribute('title'), /anglais/);
        assert.equal(await page.locator('#russianui [data-awl-primary]').getAttribute('lang'), 'fr');

        // A fixed composer must not clip the original line after translation.
        await page.evaluate(() => document.querySelector('#chat').replaceChildren());
        for (let i = 0; i < 12; i++) await add('scroll' + i, 'A message to fill the chat history ' + i);
        await page.waitForFunction(() => document.querySelectorAll('[data-awl-primary]').length === 12);
        await page.evaluate(() => { const port = document.querySelector('#scrollport'); port.scrollTop = port.scrollHeight; });
        const bottomGap = () => page.locator('#scrollport').evaluate(port => port.scrollHeight - port.clientHeight - port.scrollTop);
        await add('following', 'This longer message wraps across several lines when its translation and original are both displayed.');
        await page.waitForSelector('#following [data-awl-translation]');
        assert.ok(await bottomGap() <= 2, 'Waiting indicator must stay above the composer');
        await translated('following');
        assert.ok(await bottomGap() <= 2, 'Translation growth must keep live chat at the bottom');
        const originalBottom = (await page.locator('#following .text-fragment').boundingBox());
        const composerTop = (await page.locator('#composer').boundingBox()).y;
        assert.ok(originalBottom.y + originalBottom.height <= composerTop, 'Original must remain fully above the chat input');

        // Scrolling away while a reply is pending must win over auto-follow.
        await add('reading', 'Another message with a delayed translation while the viewer scrolls back through history.');
        await page.waitForSelector('#reading [data-awl-translation]');
        await page.locator('#scrollport').evaluate(port => { port.scrollTop = 40; });
        await page.locator('#composer input').focus();
        await translated('reading');
        assert.equal(await page.locator('#scrollport').evaluate(port => port.scrollTop), 40, 'A delayed reply must not pull the reader back down');
        assert.equal(await page.locator('#composer input').evaluate(input => input === document.activeElement), true, 'Translation must not steal input focus');
        await appearance(false, false);
        assert.equal(await page.locator('#scrollport').evaluate(port => port.scrollTop), 40, 'Appearance edits must respect reading history');

        // Short, initially non-scrolling chat can first overflow on translation.
        await page.evaluate(() => {
            document.querySelector('#chat').replaceChildren();
            const port = document.querySelector('#scrollport'); port.style.flex = 'none'; port.style.height = '125px';
        });
        await add('firstoverflow', 'This longer message wraps across several lines when its translation and original are both displayed.');
        await translated('firstoverflow');
        assert.ok(await page.locator('#scrollport').evaluate(port => port.scrollHeight > port.clientHeight), 'Fixture must overflow');
        assert.ok(await bottomGap() <= 2, 'Follow the first translation that makes chat overflow');
        await page.evaluate(() => window.__awlInterpretwitch.stop());
        assert.equal(await page.locator('#scrollport').evaluate(port => port.scrollTop), 0, 'Cleanup must leave native short chat in view');

        // Recovery must not replay previously handled or failed rows.
        await page.evaluate(() => {
            document.querySelector('#chat').replaceChildren();
            const port = document.querySelector('#scrollport'); port.style.flex = ''; port.style.height = '';
        });
        await page.evaluate(script);
        await add('handled', 'Already handled message');
        await translated('handled');
        const handledRequests = await page.evaluate(() => window.sentRequests.length);
        await page.evaluate(() => {
            for (const ready of [false, true]) window.testSocket.dispatchEvent(new MessageEvent('message', {
                data: JSON.stringify({type:'state', ready, health:ready ? 'Ready' : 'Preparing'})
            }));
        });
        await page.waitForTimeout(300);
        assert.equal(await page.evaluate(() => window.sentRequests.length), handledRequests, 'Recovery replayed an old message');
        assert.equal(await page.locator('#handled [data-awl-translation]').count(), 0);
        await add('recovered', 'New message after recovery');
        await translated('recovered');

        // A page-layout exception rolls back additions without touching native
        // messages, input focus or subsequent Twitch arrivals.
        await page.evaluate(() => {
            const insert = Node.prototype.insertBefore;
            Node.prototype.insertBefore = function(node, before) {
                if (node.hasAttribute?.('data-awl-primary')) {
                    Node.prototype.insertBefore = insert;
                    throw Error('Simulated page-layout failure');
                }
                return insert.call(this, node, before);
            };
        });
        await add('layoutfault', 'Native message during a script failure');
        await page.waitForFunction(() => document.querySelector('[data-awl-health]')?.textContent.includes('Unavailable'));
        assert.equal(await page.locator('[data-awl-translation], [data-awl-source], [data-awl-original], [data-awl-group]').count(), 0);
        assert.equal(await page.locator('#layoutfault .text-fragment').textContent(), 'Native message during a script failure');
        await page.locator('#composer input').fill('Still able to type');
        await add('unaffected', 'Native chat keeps flowing');
        assert.equal(await page.locator('#unaffected .text-fragment').textContent(), 'Native chat keeps flowing');
        await page.evaluate(script);
        await translated('unaffected');

        // Losing the socket must immediately revoke health and restore originals.
        await page.evaluate(() => window.testSocket.close());
        await page.waitForFunction(() => document.querySelector('[data-awl-health]')?.textContent.includes('Unavailable'));
        assert.equal(await page.locator('[data-awl-translation], [data-awl-source], [data-awl-original], [data-awl-group]').count(), 0);
        await page.evaluate(() => window.__awlInterpretwitch.stop());
        assert.equal(await page.locator('[data-awl-health], style[data-awl-style]').count(), 0);

        // A silent bridge uses the existing request deadline, not a heartbeat.
        await page.evaluate(() => document.querySelector('#chat').replaceChildren());
        await page.clock.install();
        await page.evaluate(script);
        await add('silent', 'Hold for timeout');
        await page.waitForSelector('#silent [data-awl-translation]');
        await page.clock.runFor(20001);
        assert.match(await page.locator('[data-awl-health]').textContent(), /Unavailable/);
        assert.equal(await page.locator('#silent [data-awl-translation]').count(), 0);
        assert.equal(await page.locator('#silent .text-fragment').textContent(), 'Hold for timeout');
        await page.evaluate(() => window.__awlInterpretwitch.stop());
        assert.deepEqual(errors, []);
        console.log('Chromium + native bridge: translations, layout, scrolling, selection, moderation, five-message startup/reconnect/model catch-up, delayed history, 200-message live burst, passive health, script exceptions, socket loss, reply timeout, recovery without replay and native chat continuity passed.');
    } catch (error) {
        console.error(diagnostics.join('\n'));
        if (page) console.error(await page.evaluate(() => ({attached: !!window.__awlInterpretwitch,
            hidden: document.hidden, rows: document.querySelectorAll('.chat-line__message').length,
            slots: document.querySelectorAll('[data-awl-translation]').length})));
        throw error;
    } finally {
        await browser?.close();
        fixture.kill();
        fs.rmSync(directory, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
