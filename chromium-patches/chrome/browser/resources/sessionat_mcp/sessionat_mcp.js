(function () {
  'use strict';

  const CLIENT_IDS = [
    'claude_desktop', 'cursor', 'codex', 'claude_code', 'windsurf', 'vscode',
  ];

  const CLIENT_LABELS = {
    claude_desktop: 'Claude Desktop',
    cursor: 'Cursor',
    codex: 'Codex',
    claude_code: 'Claude Code',
    windsurf: 'Windsurf',
    vscode: 'VS Code',
    other: 'Other',
  };

  const DEFAULT_CONFIG_PATHS = {
    claude_desktop: '~/Library/Application Support/Claude/claude_desktop_config.json',
    cursor: '~/.cursor/mcp.json',
    codex: '~/.codex/config.toml',
    claude_code: '(project-scoped via `claude mcp` CLI)',
    windsurf: '~/.codeium/windsurf/mcp_config.json',
    vscode: 'User settings.json (mcp.servers)',
  };

  const SNIPPET_HINTS = {
    claude_desktop: 'Paste under mcpServers in claude_desktop_config.json, then restart Claude Desktop.',
    cursor: 'Paste under mcpServers in ~/.cursor/mcp.json, then restart Cursor.',
    codex: 'Append to ~/.codex/config.toml. Restart Codex.',
    claude_code: 'Run the command in your terminal once. Adds Sessionat to the current project or user config.',
    windsurf: 'Paste under mcpServers in ~/.codeium/windsurf/mcp_config.json, then restart Windsurf.',
    vscode: 'Add under "mcp.servers" in your User settings.json and reload the VS Code window.',
  };

  const OTHER_HINTS = {
    json_http: 'For Cline, Continue, Windsurf, and any client that supports streamable-HTTP MCP.',
    json_stdio: 'Requires `npm i -g mcp-remote`. Use this for clients that only speak stdio.',
    toml: 'Append to ~/.codex/config.toml or any TOML-based MCP client.',
    claude_mcp_add: "Run once in your terminal. Adds Sessionat to Claude Code's project or user config.",
    curl: 'Sanity-check the endpoint or build your own client.',
    python: 'Minimal MCP client. Drop the token into an env var for real code.',
  };

  let state = {
    running: false,
    port: 0,
    token: '',
    write_enabled: false,
    discovery_path: '',
    tools: [],
    clients: {},
  };
  let tokenVisible = false;
  let activeClientTab = 'claude_desktop';
  let activeOtherFormat = 'json_http';
  const inFlight = new Set();
  const autoHealedThisSession = new Set();

  try {
    const savedFmt = sessionStorage.getItem('sessionatMcpOtherFmt');
    if (savedFmt && OTHER_HINTS[savedFmt]) activeOtherFormat = savedFmt;
    const savedTab = sessionStorage.getItem('sessionatMcpActiveClient');
    if (savedTab && (savedTab === 'other' || CLIENT_IDS.includes(savedTab))) {
      activeClientTab = savedTab;
    }
  } catch (e) { /* sessionStorage may be unavailable */ }

  function el(tag, props, ...kids) {
    const n = document.createElement(tag);
    if (props) {
      for (const k in props) {
        if (k === 'class') n.className = props[k];
        else if (k === 'text') n.textContent = props[k];
        else if (k === 'html') n.innerHTML = props[k];
        else if (k.startsWith('on') && typeof props[k] === 'function') {
          n.addEventListener(k.slice(2).toLowerCase(), props[k]);
        } else if (props[k] !== null && props[k] !== undefined &&
                   props[k] !== false) {
          n.setAttribute(k, props[k] === true ? '' : props[k]);
        }
      }
    }
    for (const c of kids) {
      if (c == null || c === false) continue;
      n.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
    }
    return n;
  }

  function maskedToken(t) {
    if (!t) return '—';
    if (t.length <= 12) return '••••••';
    return t.slice(0, 6) + '…' + t.slice(-6);
  }

  function relTime(ms) {
    if (!ms || typeof ms !== 'number') return 'never';
    const diff = Math.max(0, Date.now() - ms);
    if (diff < 5000) return 'just now';
    const s = Math.floor(diff / 1000);
    if (s < 60) return s + 's ago';
    const m = Math.floor(s / 60);
    if (m < 60) return m + 'm ago';
    const h = Math.floor(m / 60);
    if (h < 24) return h + 'h ago';
    const d = Math.floor(h / 24);
    return d + 'd ago';
  }

  function label(name) { return CLIENT_LABELS[name] || name; }

  function portForSnippet() {
    return state.running && state.port ? state.port : 0;
  }

  function buildSnippet(name) {
    const port = portForSnippet();
    const token = state.running ? state.token : '(server-not-running)';
    const url = `http://127.0.0.1:${port}/mcp`;
    switch (name) {
      case 'claude_desktop':
      case 'cursor': {
        const cfg = {
          mcpServers: {
            sessionat: {
              command: 'mcp-remote',
              args: [url, '--header', `Authorization: Bearer ${token}`],
            },
          },
        };
        return JSON.stringify(cfg, null, 2);
      }
      case 'codex': {
        return [
          '[mcp_servers.sessionat]',
          'command = "mcp-remote"',
          `args = ["${url}", "--header", "Authorization: Bearer ${token}"]`,
        ].join('\n');
      }
      case 'claude_code': {
        return `claude mcp add sessionat -- mcp-remote ${url} \\\n  --header "Authorization: Bearer ${token}"`;
      }
      case 'windsurf': {
        const cfg = {
          mcpServers: {
            sessionat: {
              url: url,
              headers: { Authorization: `Bearer ${token}` },
            },
          },
        };
        return JSON.stringify(cfg, null, 2);
      }
      case 'vscode': {
        const cfg = {
          'mcp.servers': {
            sessionat: {
              url: url,
              headers: { Authorization: `Bearer ${token}` },
            },
          },
        };
        return JSON.stringify(cfg, null, 2);
      }
    }
    return '';
  }

  function buildOtherSnippet(fmt) {
    const port = portForSnippet();
    const token = state.running ? state.token : '(server-not-running)';
    const url = `http://127.0.0.1:${port}/mcp`;
    switch (fmt) {
      case 'json_http': {
        return JSON.stringify({
          mcpServers: {
            sessionat: { url: url, headers: { Authorization: `Bearer ${token}` } },
          },
        }, null, 2);
      }
      case 'json_stdio': {
        return JSON.stringify({
          mcpServers: {
            sessionat: {
              command: 'mcp-remote',
              args: [url, '--header', `Authorization: Bearer ${token}`],
            },
          },
        }, null, 2);
      }
      case 'toml': {
        return [
          '[mcp_servers.sessionat]',
          'command = "mcp-remote"',
          `args = ["${url}", "--header", "Authorization: Bearer ${token}"]`,
        ].join('\n');
      }
      case 'claude_mcp_add': {
        return `claude mcp add sessionat -- mcp-remote ${url} \\\n  --header "Authorization: Bearer ${token}"`;
      }
      case 'curl': {
        return [
          `curl -X POST ${url} \\`,
          `  -H "Authorization: Bearer ${token}" \\`,
          `  -H "Content-Type: application/json" \\`,
          `  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'`,
        ].join('\n');
      }
      case 'python': {
        return [
          '# pip install "mcp[cli]" httpx',
          'import asyncio, httpx',
          'from mcp.client.streamable_http import streamablehttp_client',
          'from mcp import ClientSession',
          '',
          `URL = "${url}"`,
          `HEADERS = {"Authorization": "Bearer ${token}"}`,
          '',
          'async def main():',
          '    async with streamablehttp_client(URL, headers=HEADERS) as (r, w, _):',
          '        async with ClientSession(r, w) as s:',
          '            await s.initialize()',
          '            print(await s.list_tools())',
          '',
          'asyncio.run(main())',
        ].join('\n');
      }
    }
    return '';
  }

  // ===================== Toasts =====================
  function toast(message, kind) {
    const host = document.getElementById('toastHost');
    if (!host) return;
    const k = kind || 'ok';
    const t = el('div', { class: 'toast toast--' + k, role: 'status' });
    t.textContent = message;
    t.addEventListener('click', () => { if (t.parentNode) t.parentNode.removeChild(t); });
    host.appendChild(t);
    const ttl = (k === 'err') ? 5000 : 3500;
    setTimeout(() => { if (t.parentNode) t.parentNode.removeChild(t); }, ttl);
  }

  // ===================== Copy infrastructure =====================
  function copyText(text) {
    try {
      navigator.clipboard.writeText(text);
      return true;
    } catch (e) { return false; }
  }

  function resolveCopyValue(btn) {
    const literal = btn.getAttribute('data-copy-literal');
    if (literal != null) return literal;
    const fromState = btn.getAttribute('data-copy-from-state');
    if (fromState) {
      const parts = fromState.split('.');
      let cur = state;
      for (const p of parts) {
        if (cur == null) return '';
        cur = cur[p];
      }
      return cur == null ? '' : String(cur);
    }
    const target = btn.getAttribute('data-target');
    if (!target) return '';
    if (target === 'authToken') return state.token || '';
    const node = document.getElementById(target);
    return node ? node.textContent : '';
  }

  function handleCopyClick(btn) {
    const value = resolveCopyValue(btn);
    if (!value || value === '—') return;
    if (!copyText(value)) {
      toast('Could not copy — check clipboard permissions', 'err');
      return;
    }
    btn.classList.add('copied');
    const original = btn.getAttribute('data-original-text') || btn.textContent;
    btn.setAttribute('data-original-text', original);
    btn.textContent = 'Copied!';
    setTimeout(() => {
      btn.classList.remove('copied');
      btn.textContent = original;
    }, 1200);
  }

  // ===================== Status card =====================
  function renderStatusCard() {
    const s = state;
    const running = !!s.running;
    const dot = document.getElementById('statusDot');
    dot.className = 'status-dot ' + (running ? 'running' : 'stopped');
    document.getElementById('statusLine').textContent =
      running ? 'Running' : 'Stopped';
    document.getElementById('statusSub').textContent =
      running
        ? 'Listening on 127.0.0.1 — accepts JSON-RPC POST at /mcp.'
        : 'The MCP server is disabled or could not bind a loopback port.';
  }

  // ===================== Connection card =====================
  function renderConnectionCard() {
    const s = state;
    const running = !!s.running;
    document.getElementById('endpointUrl').textContent =
      running ? `http://127.0.0.1:${s.port}/mcp` : '—';
    const tokenEl = document.getElementById('authToken');
    tokenEl.textContent = !running ? '—'
      : (tokenVisible ? s.token : maskedToken(s.token));
    document.getElementById('discoveryPath').textContent = s.discovery_path || '—';
  }

  // ===================== Write card =====================
  function renderWriteCard() {
    const tgl = document.getElementById('writeToggle');
    if (tgl) tgl.checked = !!state.write_enabled;
    const warn = document.getElementById('writeWarn');
    if (warn) warn.style.display = state.write_enabled ? '' : 'none';
    const wPath = document.getElementById('writeWarnPath');
    if (wPath) wPath.textContent = state.discovery_path || 'mcp.json';
  }

  // ===================== Tools card =====================
  function renderTools() {
    const list = document.getElementById('toolsList');
    const sub = document.getElementById('toolsSub');
    const tools = state.tools || [];
    sub.textContent = tools.length ? '· ' + tools.length + ' available' : '';
    if (tools.length === 0) {
      list.replaceChildren(el('p', { class: 'muted', text: 'No tools registered.' }));
      return;
    }
    list.replaceChildren(...tools.map((t) => {
      const props = t.inputSchema && t.inputSchema.properties
        ? t.inputSchema.properties : {};
      const required = new Set(t.inputSchema && t.inputSchema.required
        ? t.inputSchema.required : []);
      const args = Object.entries(props).map(([k, v]) =>
        el('span', {
          class: 'tool-arg' + (required.has(k) ? ' required' : ''),
          title: (v && v.description) || k,
          text: k + (required.has(k) ? '*' : ''),
        })
      );
      const argRow = args.length ? el('div', { class: 'tool-args' }, ...args) : null;
      const isWriteTool =
        (t.description || '').toUpperCase().startsWith('WRITE TOOL');
      return el('div', {
        class: 'tool-row' + (isWriteTool ? ' write-tool' : ''),
      },
        el('div', { class: 'tool-name', text: t.name }),
        el('div', { class: 'tool-desc', text: t.description || '' }),
        argRow,
      );
    }));
  }

  // ===================== Clients tabs =====================
  function statusModifierFor(name) {
    const c = state.clients[name];
    if (!state.running) return 'missing';
    if (!c) return 'missing';
    if (c.status === 'connected') return 'connected';
    if (c.status === 'stale') return 'stale';
    if (c.status === 'error') return 'error';
    return 'missing';
  }

  function renderClientsTabs() {
    const tabs = document.querySelectorAll('#clientsTabs .clients-tabs__tab');
    let connectedCount = 0;
    for (const name of CLIENT_IDS) {
      if (state.clients[name] && state.clients[name].status === 'connected') {
        connectedCount++;
      }
    }
    const sub = document.getElementById('clientsCountSub');
    if (sub) {
      sub.textContent = connectedCount
        ? `· ${connectedCount} connected`
        : '';
    }
    tabs.forEach((tab) => {
      const name = tab.getAttribute('data-client');
      tab.classList.toggle('clients-tabs__tab--active', name === activeClientTab);
      tab.setAttribute('aria-selected', name === activeClientTab ? 'true' : 'false');
      const dotForName = tab.querySelector('[data-status-for]');
      if (dotForName) {
        const mod = statusModifierFor(name);
        dotForName.className = 'status-dot status-dot--inline status-dot--' + mod;
      }
    });
  }

  // ===================== Client card =====================
  function statusTextFor(name, c) {
    if (!state.running) {
      return ["MCP server isn't running",
              'Enable it (top of this page) before connecting ' + label(name) + '.'];
    }
    if (!c) return ['Loading…', ''];
    switch (c.status) {
      case 'connected':
        return ['Connected',
                `Sessionat is in ${label(name)}'s config and pointed at the running server.`];
      case 'stale':
        return ['Stale — port/token rotated since last connect',
                'Click Re-connect to update the entry. Restart ' + label(name) + ' after.'];
      case 'installed_no_entry':
        return ['Not connected',
                label(name) + " is installed but Sessionat isn't in its config yet."];
      case 'not_installed':
        return [label(name) + ' not detected',
                'Install ' + label(name) + ' first, or use any MCP client manually with the endpoint + token above.'];
      case 'error':
        return ['Error',
                c.error || ('Could not read ' + label(name) + ' config.')];
    }
    return ['', ''];
  }

  function actionsForStatus(name, c) {
    const buttons = [];
    if (!state.running) {
      buttons.push(el('button', {
        class: 'btn-primary', type: 'button', disabled: true,
        title: 'Enable MCP server above',
        'data-action': 'connect', 'data-client': name,
      }, 'Connect'));
      return buttons;
    }
    if (!c) return buttons;
    const busy = inFlight.has(name);
    const reveal = () => el('button', {
      class: 'link-btn', type: 'button',
      'data-action': 'reveal', 'data-client': name,
    }, 'Reveal config in Finder');
    const disconnect = () => el('button', {
      class: 'link-btn danger', type: 'button',
      'data-action': 'disconnect', 'data-client': name,
      disabled: busy ? true : false,
    }, 'Disconnect');
    const connectPrimary = (text) => el('button', {
      class: 'btn-primary' + (busy ? ' is-loading' : ''), type: 'button',
      'data-action': 'connect', 'data-client': name,
      disabled: busy ? true : false,
    }, text);
    const reconnectLink = () => el('button', {
      class: 'link-btn', type: 'button',
      'data-action': 'connect', 'data-client': name,
      disabled: busy ? true : false,
    }, 'Re-connect');

    switch (c.status) {
      case 'installed_no_entry':
        buttons.push(connectPrimary('Connect'));
        if (c.config_path) buttons.push(reveal());
        break;
      case 'connected':
        buttons.push(reconnectLink());
        if (c.config_path) buttons.push(reveal());
        buttons.push(disconnect());
        break;
      case 'stale':
        buttons.push(connectPrimary('Re-connect'));
        if (c.config_path) buttons.push(reveal());
        buttons.push(disconnect());
        break;
      case 'not_installed':
        buttons.push(el('button', {
          class: 'link-btn', type: 'button',
          'data-action': 'install-instructions', 'data-client': name,
        }, 'Install instructions'));
        break;
      case 'error':
        buttons.push(el('button', {
          class: 'link-btn', type: 'button',
          'data-action': 'connect', 'data-client': name,
          disabled: busy ? true : false,
        }, 'Retry'));
        if (c.config_path) buttons.push(reveal());
        break;
    }
    return buttons;
  }

  function buildClientCard(name) {
    const c = state.clients[name] || {};
    const installed = !!(c.status && c.status !== 'not_installed');
    const mod = statusModifierFor(name);
    const [line1, line2] = statusTextFor(name, c);
    const snippetText = buildSnippet(name);
    const snippetHint = SNIPPET_HINTS[name] || '';
    const configPath = c.config_path || DEFAULT_CONFIG_PATHS[name] || '';

    const header = el('div', { class: 'client-card__header' },
      el('span', { class: 'client-icon', 'data-icon': name }),
      el('h3', { class: 'client-card__title', text: label(name) }),
      el('span', {
        class: 'client-card__installed-pill' +
          (installed ? ' client-card__installed-pill--ok' : ''),
        text: installed ? 'Installed' : 'Not detected',
      }),
    );

    const statusRow = el('div', { class: 'client-card__status-row' },
      el('span', { class: 'status-dot status-dot--' + mod }),
      el('div', { class: 'client-card__status-text' },
        el('strong', { text: line1 }),
        el('div', { class: 'muted', text: line2 }),
      ),
      el('div', { class: 'client-card__actions' }, ...actionsForStatus(name, c)),
    );

    const children = [header, statusRow];

    if (name === 'claude_code' && c.has_cli && c.status !== 'connected') {
      children.push(el('div', { class: 'client-card__cli-row' },
        el('button', {
          class: 'btn-primary', type: 'button',
          'data-action': 'run-claude-mcp-add',
        }, 'Run claude mcp add for me'),
        el('span', { class: 'muted',
          text: 'Runs the command in a hidden shell, then re-reads status.' }),
      ));
    }

    const snippetWrap = el('div', { class: 'code-block-wrap' },
      el('pre', { class: 'code-block', id: 'snippet-' + name, text: snippetText }),
      el('button', { class: 'copy-btn', 'data-target': 'snippet-' + name }, 'Copy'),
    );
    const snippetDetails = el('details', {},
      el('summary', { text: 'Show config snippet' }),
      snippetWrap,
      el('p', { class: 'muted', style: 'margin-top: 6px;', text: snippetHint }),
      configPath ? el('p', { class: 'muted', style: 'margin-top: 4px; font-size: 11px;',
                              text: 'Path: ' + configPath }) : null,
    );
    children.push(el('div', { class: 'client-card__snippet' }, snippetDetails));

    const grantChecked = !!c.has_write_grant;
    const grantSwitch = el('span', { class: 'switch' },
      el('input', {
        type: 'checkbox',
        'data-action': 'grant-write',
        'data-client': name,
        ...(grantChecked ? { checked: true } : {}),
      }),
      el('span', { class: 'switch-slider' }),
    );
    children.push(el('div', { class: 'grant-row' },
      el('label', { class: 'grant-row__label' },
        grantSwitch,
        el('span', { text: 'Allow write tools from ' + label(name) }),
      ),
      el('span', { class: 'grant-row__last-used',
                    text: 'Last used: ' + relTime(c.last_used_ms) }),
    ));

    return el('article', {
      class: 'client-card client-card--' + mod,
      'data-client': name,
    }, ...children);
  }

  function buildOtherCard() {
    const formats = [
      ['json_http', 'JSON (HTTP)'],
      ['json_stdio', 'JSON (stdio · mcp-remote)'],
      ['toml', 'TOML'],
      ['claude_mcp_add', 'claude mcp add'],
      ['curl', 'curl'],
      ['python', 'Python sample'],
    ];
    const switcher = el('div', {
      class: 'format-switcher', role: 'tablist', id: 'otherFormatSwitcher',
    }, ...formats.map(([fmt, lbl]) =>
      el('button', {
        class: 'format-switcher__tab' +
          (fmt === activeOtherFormat ? ' format-switcher__tab--active' : ''),
        type: 'button',
        'data-fmt': fmt,
      }, lbl),
    ));

    const snippetText = buildOtherSnippet(activeOtherFormat);
    const hintText = OTHER_HINTS[activeOtherFormat] || '';

    return el('article', {
      class: 'client-card client-card--missing',
      'data-client': 'other',
    },
      el('div', { class: 'client-card__header' },
        el('h3', { class: 'client-card__title', text: 'Other MCP clients' }),
        el('span', { class: 'muted',
          text: "Copy the snippet that matches your client's format." }),
      ),
      switcher,
      el('div', { class: 'code-block-wrap' },
        el('pre', { class: 'code-block', id: 'otherSnippet', text: snippetText }),
        el('button', { class: 'copy-btn', 'data-target': 'otherSnippet' }, 'Copy'),
      ),
      el('p', { class: 'muted', id: 'otherSnippetHint', text: hintText }),
    );
  }

  function renderClientsPanels() {
    const panels = document.getElementById('clientsPanels');
    if (!panels) return;
    if (activeClientTab === 'other') {
      panels.replaceChildren(buildOtherCard());
    } else {
      panels.replaceChildren(buildClientCard(activeClientTab));
    }
  }

  // ===================== Auto-heal =====================
  function maybeAutoHeal() {
    if (!state.running) return;
    for (const name of CLIENT_IDS) {
      const c = state.clients[name];
      if (!c || c.status !== 'stale') continue;
      if (autoHealedThisSession.has(name)) continue;
      autoHealedThisSession.add(name);
      inFlight.add(name);
      chrome.send('connectClient', [name]);
      toast(`Stale token for ${label(name)} — auto-healing`, 'warn');
    }
  }

  // ===================== Top-level render =====================
  function render() {
    renderStatusCard();
    renderConnectionCard();
    renderWriteCard();
    renderClientsTabs();
    renderClientsPanels();
    renderTools();
  }

  // ===================== chrome.send callback shims =====================
  window.sessionatMcpRenderStatus = function (payload) {
    state.running = !!payload.running;
    state.port = payload.port || 0;
    state.token = payload.master_token || payload.token || '';
    state.write_enabled = !!payload.master_has_write_grant;
    state.discovery_path = payload.discovery_path || '';
    state.tools = payload.tools || [];
    render();
  };

  window.sessionatMcpRenderAllClientStatuses = function (payload) {
    const list = (payload && payload.clients) || [];
    state.clients = {};
    for (const r of list) {
      const id = r.client_id;
      if (!id) continue;
      state.clients[id] = {
        status: r.status,
        config_path: r.config_path || '',
        error: r.error || '',
        has_write_grant: !!r.has_write_grant,
        requires_manual_snippet: !!r.requires_manual_snippet,
        manual_snippet: r.manual_snippet || '',
        last_used_ms: r.last_used_ms || 0,
        has_cli: !!r.has_cli,
        installed: r.status && r.status !== 'not_installed',
      };
    }
    render();
    maybeAutoHeal();
  };

  window.sessionatMcpRenderClientStatus = function (payload) {
    const id = payload && payload.client_id;
    if (!id) return;
    state.clients[id] = Object.assign({}, state.clients[id] || {}, {
      status: payload.status,
      config_path: payload.config_path || '',
      error: payload.error || '',
      has_write_grant: !!payload.has_write_grant,
      requires_manual_snippet: !!payload.requires_manual_snippet,
      manual_snippet: payload.manual_snippet || '',
    });
    render();
  };

  window.sessionatMcpRenderClientWriteResult = function (payload) {
    const id = payload && payload.client_id;
    if (id) inFlight.delete(id);
    if (payload && payload.ok) {
      if (id) toast(`Updated config — restart ${label(id)}`, 'ok');
    } else {
      toast((payload && payload.error) || 'Operation failed.', 'err');
    }
    chrome.send('getAllClientStatuses');
  };

  window.sessionatMcpRenderRevealResult = function (payload) {
    if (payload && !payload.ok) {
      toast(payload.error || 'Could not reveal config file.', 'err');
    }
  };

  window.sessionatMcpRenderGrantResult = function (payload) {
    const id = payload && payload.client_id;
    if (!id) return;
    if (!payload.ok) {
      toast(payload.error || 'Could not update write grant.', 'err');
      chrome.send('getAllClientStatuses');
      return;
    }
    if (state.clients[id]) {
      state.clients[id].has_write_grant = !!payload.granted;
      render();
    }
  };

  window.sessionatMcpRenderTestResult = function (payload) {
    if (!payload || !payload.ok) {
      toast((payload && payload.error) || 'Connection test failed.', 'err');
      return;
    }
    const url = `http://127.0.0.1:${payload.port}/mcp`;
    const started = performance.now();
    fetch(url, {
      method: 'POST',
      headers: {
        'Authorization': 'Bearer ' + payload.master_token,
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'tools/list' }),
    }).then((r) => {
      const ms = Math.round(performance.now() - started);
      if (r.ok) toast(`Connection OK (${ms} ms)`, 'ok');
      else toast(`Loopback returned HTTP ${r.status}`, 'err');
    }).catch((e) => {
      toast('Loopback request blocked: ' + (e && e.message ? e.message : e), 'err');
    });
  };

  window.sessionatMcpRenderRotateResult = function (payload) {
    if (!payload || !payload.ok) {
      toast((payload && payload.error) || 'Rotate failed.', 'err');
      return;
    }
    state.token = payload.new_master_token || state.token;
    autoHealedThisSession.clear();
    toast('Rotated token — all clients marked Stale, auto-healing', 'warn');
    chrome.send('getStatus');
    chrome.send('getAllClientStatuses');
  };

  // ===================== Event wiring =====================
  function onTabClick(name) {
    if (activeClientTab === name) return;
    activeClientTab = name;
    try { sessionStorage.setItem('sessionatMcpActiveClient', name); } catch (e) {}
    renderClientsTabs();
    renderClientsPanels();
  }

  function onConnectClick(name) {
    if (!state.running) {
      toast("Enable the MCP server first.", 'err');
      return;
    }
    inFlight.add(name);
    renderClientsPanels();
    chrome.send('connectClient', [name]);
  }

  function onDisconnectClick(name) {
    if (!window.confirm('Remove Sessionat from ' + label(name) + ' config?')) return;
    inFlight.add(name);
    renderClientsPanels();
    chrome.send('disconnectClient', [name]);
  }

  function onRevealClick(name) {
    chrome.send('revealClientConfig', [name]);
  }

  function onGrantChange(name, granted) {
    if (state.clients[name]) {
      state.clients[name].has_write_grant = granted;
    }
    chrome.send('setClientWriteGrant', [name, granted]);
  }

  function onFormatSwitch(fmt) {
    if (!OTHER_HINTS[fmt]) return;
    activeOtherFormat = fmt;
    try { sessionStorage.setItem('sessionatMcpOtherFmt', fmt); } catch (e) {}
    renderClientsPanels();
  }

  document.addEventListener('DOMContentLoaded', () => {
    // Delegated copy-button handler.
    document.body.addEventListener('click', (e) => {
      const btn = e.target.closest('.copy-btn');
      if (btn) {
        handleCopyClick(btn);
        return;
      }
    });

    // Tab strip click.
    const tabBar = document.getElementById('clientsTabs');
    if (tabBar) {
      tabBar.addEventListener('click', (e) => {
        const tab = e.target.closest('.clients-tabs__tab');
        if (!tab) return;
        const name = tab.getAttribute('data-client');
        if (name) onTabClick(name);
      });
    }

    // Delegated handlers inside the client panels.
    const panels = document.getElementById('clientsPanels');
    if (panels) {
      panels.addEventListener('click', (e) => {
        const fmtBtn = e.target.closest('.format-switcher__tab');
        if (fmtBtn) {
          const fmt = fmtBtn.getAttribute('data-fmt');
          if (fmt) onFormatSwitch(fmt);
          return;
        }
        const actionBtn = e.target.closest('[data-action]');
        if (!actionBtn) return;
        const action = actionBtn.getAttribute('data-action');
        const name = actionBtn.getAttribute('data-client');
        if (action === 'connect') onConnectClick(name);
        else if (action === 'disconnect') onDisconnectClick(name);
        else if (action === 'reveal') onRevealClick(name);
        else if (action === 'install-instructions') {
          toast('See docs at sessionat.com/docs/clients/' + name, 'ok');
        } else if (action === 'run-claude-mcp-add') {
          onConnectClick('claude_code');
        }
      });
      panels.addEventListener('change', (e) => {
        const input = e.target.closest('input[data-action="grant-write"]');
        if (!input) return;
        const name = input.getAttribute('data-client');
        onGrantChange(name, !!input.checked);
      });
    }

    // Token visibility toggle.
    const tt = document.getElementById('toggleToken');
    if (tt) tt.addEventListener('click', (e) => {
      tokenVisible = !tokenVisible;
      e.currentTarget.textContent = tokenVisible ? 'Hide' : 'Show';
      renderConnectionCard();
    });

    const tgl = document.getElementById('writeToggle');
    if (tgl) tgl.addEventListener('change', (e) => {
      chrome.send('setClientWriteGrant', ['_master', e.target.checked]);
      state.write_enabled = e.target.checked;
      renderWriteCard();
    });

    const rd = document.getElementById('revealDiscovery');
    if (rd) rd.addEventListener('click', () => {
      toast('Discovery file: ' + (state.discovery_path || '—'), 'ok');
    });

    // Test connection.
    const tc = document.getElementById('testConnBtn');
    if (tc) tc.addEventListener('click', () => {
      if (!state.running) {
        toast("MCP server isn't running", 'err');
        return;
      }
      chrome.send('testConnection');
    });

    // Rotate token.
    const rt = document.getElementById('rotateTokenBtn');
    if (rt) rt.addEventListener('click', () => {
      if (!window.confirm(
        'Rotate the master token? Every connected client will be marked Stale ' +
        'and auto-reconnected with the new token. You may need to restart each ' +
        'client app to pick it up.')) return;
      chrome.send('rotateToken');
    });

    chrome.send('getStatus');
    chrome.send('getAllClientStatuses');
  });

  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
      chrome.send('getStatus');
      chrome.send('getAllClientStatuses');
    }
  });
})();
