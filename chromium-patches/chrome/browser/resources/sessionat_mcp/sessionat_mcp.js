// chrome://sessionat-mcp/ status page. Reads status from the embedded
// McpService via chrome.send and renders it.

(function () {
  'use strict';

  let state = null;
  let tokenVisible = false;

  function el(tag, props, ...kids) {
    const n = document.createElement(tag);
    if (props) {
      for (const k in props) {
        if (k === 'class') n.className = props[k];
        else if (k === 'text') n.textContent = props[k];
        else if (k.startsWith('on') && typeof props[k] === 'function') {
          n.addEventListener(k.slice(2).toLowerCase(), props[k]);
        } else if (props[k] !== null && props[k] !== undefined) {
          n.setAttribute(k, props[k]);
        }
      }
    }
    for (const c of kids) {
      if (c == null) continue;
      n.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
    }
    return n;
  }

  function maskedToken(t) {
    if (!t) return '—';
    return t.slice(0, 6) + '…' + t.slice(-6);
  }

  function render() {
    const s = state || {};
    const running = !!s.running;
    const writeOn = !!s.write_enabled;
    const dot = document.getElementById('statusDot');
    dot.className = 'status-dot ' + (running ? 'running' : 'stopped');
    document.getElementById('statusLine').textContent =
      running ? 'Running' : 'Stopped';
    document.getElementById('statusSub').textContent =
      running
        ? 'Listening on 127.0.0.1 — accepts JSON-RPC POST at /mcp.'
        : 'The MCP server is disabled or could not bind a loopback port.';

    const tgl = document.getElementById('writeToggle');
    if (tgl) tgl.checked = writeOn;
    const warn = document.getElementById('writeWarn');
    if (warn) warn.style.display = writeOn ? '' : 'none';
    const wPath = document.getElementById('writeWarnPath');
    if (wPath) wPath.textContent = s.discovery_path || 'mcp.json';

    document.getElementById('endpointUrl').textContent =
      running ? `http://127.0.0.1:${s.port}/mcp` : '—';
    const tokenEl = document.getElementById('authToken');
    tokenEl.textContent = !running ? '—'
        : (tokenVisible ? s.token : maskedToken(s.token));
    tokenEl.classList.toggle('masked', false);  // we mask via slice() above
    document.getElementById('discoveryPath').textContent = s.discovery_path || '—';

    // Claude Desktop config snippet.
    const cfg = {
      mcpServers: {
        sessionat: {
          command: 'mcp-remote',
          args: running
              ? [`http://127.0.0.1:${s.port}/mcp`,
                 '--header', `Authorization: Bearer ${s.token}`]
              : ['http://127.0.0.1:0/mcp', '--header', 'Authorization: Bearer (server-not-running)'],
        },
      },
    };
    document.getElementById('claudeConfig').textContent =
        JSON.stringify(cfg, null, 2);

    // Tools list.
    const list = document.getElementById('toolsList');
    const sub = document.getElementById('toolsSub');
    const tools = s.tools || [];
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
      const argRow = args.length
          ? el('div', { class: 'tool-args' }, ...args)
          : null;
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

  function copyText(text) {
    try {
      navigator.clipboard.writeText(text);
      return true;
    } catch (e) { return false; }
  }

  window.sessionatMcpRenderStatus = function (payload) {
    state = payload;
    render();
  };

  document.addEventListener('DOMContentLoaded', () => {
    document.querySelectorAll('.copy-btn[data-target]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const target = btn.getAttribute('data-target');
        let value;
        if (target === 'authToken') {
          value = state && state.token;  // copy the real token regardless of mask
        } else {
          value = document.getElementById(target).textContent;
        }
        if (!value || value === '—') return;
        copyText(value);
        btn.classList.add('copied');
        const original = btn.textContent;
        btn.textContent = 'Copied!';
        setTimeout(() => {
          btn.classList.remove('copied');
          btn.textContent = original;
        }, 1200);
      });
    });

    document.getElementById('toggleToken').addEventListener('click', (e) => {
      tokenVisible = !tokenVisible;
      e.currentTarget.textContent = tokenVisible ? 'Hide' : 'Show';
      render();
    });

    const tgl = document.getElementById('writeToggle');
    if (tgl) tgl.addEventListener('change', (e) => {
      chrome.send('setWriteEnabled', [e.target.checked]);
    });

    document.getElementById('claudeConnect').addEventListener('click', () => {
      chrome.send('connectClaude');
    });
    document.getElementById('claudeReconnect').addEventListener('click', () => {
      chrome.send('connectClaude');
    });
    document.getElementById('claudeDisconnect').addEventListener('click', () => {
      if (window.confirm('Remove Sessionat from Claude Desktop config?')) {
        chrome.send('disconnectClaude');
      }
    });

    chrome.send('getStatus');
    chrome.send('getClaudeStatus');
  });

  // ============ Claude Desktop status renderers ============
  window.sessionatMcpRenderClaudeStatus = function (payload) {
    const dot = document.getElementById('claudeDot');
    const line1 = document.getElementById('claudeLine1');
    const line2 = document.getElementById('claudeLine2');
    const connect = document.getElementById('claudeConnect');
    const reconnect = document.getElementById('claudeReconnect');
    const disconnect = document.getElementById('claudeDisconnect');
    const pathEl = document.getElementById('claudeConfigPath');
    if (pathEl && payload.config_path) pathEl.textContent = payload.config_path;
    [connect, reconnect, disconnect].forEach((b) => { b.style.display = 'none'; });

    const s = payload.status;
    if (s === 'connected') {
      dot.className = 'claude-status-dot connected';
      line1.textContent = 'Connected';
      line2.textContent = 'Sessionat is in Claude Desktop\'s config and pointed at the running server.';
      disconnect.style.display = '';
    } else if (s === 'stale') {
      dot.className = 'claude-status-dot stale';
      line1.textContent = 'Stale — port/token rotated since last connect';
      line2.textContent = 'Click Re-connect to update the entry. Restart Claude Desktop after.';
      reconnect.style.display = '';
      disconnect.style.display = '';
    } else if (s === 'installed_no_entry') {
      dot.className = 'claude-status-dot';
      line1.textContent = 'Not connected';
      line2.textContent = 'Claude Desktop is installed but Sessionat isn\'t in its config yet.';
      connect.style.display = '';
    } else if (s === 'not_installed') {
      dot.className = 'claude-status-dot';
      line1.textContent = 'Claude Desktop not detected';
      line2.textContent = 'Install Claude Desktop first, or use any MCP client manually with the endpoint + token above.';
    } else if (s === 'server_not_running') {
      dot.className = 'claude-status-dot error';
      line1.textContent = "MCP server isn't running";
      line2.textContent = 'Enable it (top of this page) before connecting Claude Desktop.';
    } else {
      dot.className = 'claude-status-dot error';
      line1.textContent = 'Error';
      line2.textContent = payload.error || 'Could not read Claude Desktop config.';
    }
  };

  window.sessionatMcpRenderClaudeWriteResult = function (payload) {
    if (payload.ok) return;  // status refresh handles success UX
    window.alert(payload.error || 'Operation failed.');
  };

  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
      chrome.send('getStatus');
      chrome.send('getClaudeStatus');
    }
  });
})();
