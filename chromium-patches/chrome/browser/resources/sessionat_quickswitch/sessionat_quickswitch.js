// Sessionat Quick Switcher (chrome://sessionat-quickswitch/)
//
// Spotlight-style picker triggered by Cmd+K. Builds a flat list of all
// workspaces and their saved pages, fuzzy-filters as you type, switches
// workspace or opens a page on Enter, then closes itself.

(function () {
  'use strict';

  /** Flat result set built from workspaces + their pages. */
  let allRows = [];
  /** Currently visible (filtered) rows. */
  let visibleRows = [];
  /** Index into visibleRows. */
  let selected = 0;

  // -------- DOM helpers --------
  function el(tag, props, ...children) {
    const n = document.createElement(tag);
    if (props) {
      for (const k in props) {
        if (k === 'class') n.className = props[k];
        else if (k === 'text') n.textContent = props[k];
        else if (k.startsWith('on') && typeof props[k] === 'function') {
          n.addEventListener(k.slice(2).toLowerCase(), props[k]);
        } else if (k === 'style') {
          for (const sk in props.style) n.style[sk] = props.style[sk];
        } else if (props[k] !== null && props[k] !== undefined) {
          n.setAttribute(k, props[k]);
        }
      }
    }
    for (const c of children) {
      if (c == null) continue;
      n.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
    }
    return n;
  }

  function hostnameOf(url) {
    try { return new URL(url).hostname; } catch (e) { return url || ''; }
  }

  // Use chrome://favicon2 with allowGoogleServerFallback=1 so the favicon
  // service fetches from Google inside the browser process when Chrome's
  // local favicon cache is empty. A direct https://www.google.com/... img
  // src would be killed by the WebUI URL loader factory (Mojo "Incorrect
  // scheme"). showFallbackMonogram=true lets the service render a colored
  // letter tile when no icon exists anywhere.
  function createFavicon(url) {
    const host = hostnameOf(url);
    const u = new URL('chrome://favicon2/');
    u.searchParams.set('pageUrl', url);
    u.searchParams.set('size', '32');
    u.searchParams.set('allowGoogleServerFallback', '1');
    u.searchParams.set('showFallbackMonogram', 'true');
    const img = document.createElement('img');
    img.className = 'qs-favicon';
    img.alt = '';
    img.src = u.toString();
    img.addEventListener('error', () => {
      const letter = (host || '?').charAt(0);
      const fb = document.createElement('div');
      fb.className = 'qs-favicon-fallback';
      fb.textContent = letter;
      img.replaceWith(fb);
    });
    return img;
  }

  // -------- Highlight matched characters --------
  function highlight(text, query) {
    if (!query) return [document.createTextNode(text || '')];
    const q = query.toLowerCase();
    const lc = (text || '').toLowerCase();
    const nodes = [];
    let qi = 0;
    let runStart = 0;
    let inMatch = false;
    for (let i = 0; i < text.length; i++) {
      const isMatch = qi < q.length && lc[i] === q[qi];
      if (isMatch !== inMatch) {
        if (i > runStart) {
          const seg = text.slice(runStart, i);
          if (inMatch) {
            nodes.push(el('span', { class: 'qs-match', text: seg }));
          } else {
            nodes.push(document.createTextNode(seg));
          }
        }
        runStart = i;
        inMatch = isMatch;
      }
      if (isMatch) qi++;
    }
    if (runStart < text.length) {
      const seg = text.slice(runStart);
      nodes.push(inMatch ? el('span', { class: 'qs-match', text: seg })
                         : document.createTextNode(seg));
    }
    return nodes;
  }

  // -------- Fuzzy score: returns -1 if no match, else lower=better --------
  function fuzzyScore(text, query) {
    if (!query) return 0;
    const t = (text || '').toLowerCase();
    const q = query.toLowerCase();
    let ti = 0;
    let qi = 0;
    let score = 0;
    let lastMatch = -1;
    while (qi < q.length && ti < t.length) {
      if (t[ti] === q[qi]) {
        if (lastMatch >= 0) score += (ti - lastMatch);
        else score += ti;
        lastMatch = ti;
        qi++;
      }
      ti++;
    }
    if (qi < q.length) return -1;
    return score;
  }

  // -------- Build allRows from workspaces --------
  function buildRows(workspaces) {
    const rows = [];
    (workspaces || []).forEach((ws, wsIdx) => {
      rows.push({
        kind: 'workspace',
        id: ws.id,
        name: ws.name || 'Untitled',
        color: ws.color || '#f97316',
        icon: ws.icon || '·',
        tabCount: (ws.items || []).length,
        index: wsIdx,
        isActive: !!ws.is_active,
        // Search blob.
        searchText: (ws.name || '') + ' workspace',
      });
      (ws.items || []).forEach((it) => {
        rows.push({
          kind: 'page',
          url: it.url || '',
          title: it.title || it.url || '(untitled)',
          host: hostnameOf(it.url || ''),
          workspaceName: ws.name || '',
          workspaceId: ws.id,
          searchText: (it.title || '') + ' ' + (it.url || '') + ' ' + (ws.name || ''),
        });
      });
    });
    return rows;
  }

  // -------- Render --------
  function render() {
    const list = document.getElementById('results');
    if (!list) return;
    list.replaceChildren();

    if (visibleRows.length === 0) {
      list.appendChild(el('div', {
        class: 'qs-empty',
        text: 'No workspaces or pages match.',
      }));
      return;
    }

    const query = (document.getElementById('q').value || '').trim();
    let lastKind = null;
    visibleRows.forEach((row, idx) => {
      // Section labels.
      if (row.kind !== lastKind) {
        list.appendChild(el('div', {
          class: 'qs-section-label',
          text: row.kind === 'workspace' ? 'Workspaces' : 'Saved pages',
        }));
        lastKind = row.kind;
      }

      const isSelected = idx === selected;
      const rowEl = el('div', {
        class: 'qs-row' + (isSelected ? ' selected' : ''),
        'data-idx': String(idx),
        onClick: () => activate(idx, false),
      });

      if (row.kind === 'workspace') {
        rowEl.appendChild(el('div', {
          class: 'qs-icon',
          style: { background: row.color },
          text: row.icon,
        }));
        const text = el('div', { class: 'qs-text' });
        const title = el('div', { class: 'qs-title' });
        highlight(row.name, query).forEach((n) => title.appendChild(n));
        if (row.isActive) {
          title.appendChild(el('span', {
            class: 'qs-match',
            style: { marginLeft: '8px', fontSize: '10px',
                     textTransform: 'uppercase', letterSpacing: '0.5px' },
            text: 'active',
          }));
        }
        text.appendChild(title);
        text.appendChild(el('div', {
          class: 'qs-meta',
          text: row.tabCount + ' ' + (row.tabCount === 1 ? 'tab' : 'tabs'),
        }));
        rowEl.appendChild(text);
        if (row.index < 9) {
          rowEl.appendChild(el('span', {
            class: 'qs-shortcut',
            text: '⌘' + (row.index === 8 ? '9' : String(row.index + 1)),
          }));
        }
      } else {
        // page row — use the shared favicon loader.
        rowEl.appendChild(createFavicon(row.url));
        const text = el('div', { class: 'qs-text' });
        const title = el('div', { class: 'qs-title' });
        highlight(row.title, query).forEach((n) => title.appendChild(n));
        text.appendChild(title);
        text.appendChild(el('div', {
          class: 'qs-meta',
          text: row.host + ' · in ' + row.workspaceName,
        }));
        rowEl.appendChild(text);
      }

      list.appendChild(rowEl);
    });

    // Ensure selected row is in view.
    const sel = list.querySelector('.qs-row.selected');
    if (sel) sel.scrollIntoView({ block: 'nearest' });
  }

  // -------- Filter --------
  function applyFilter() {
    const query = (document.getElementById('q').value || '').trim();
    if (!query) {
      visibleRows = allRows.slice();
    } else {
      visibleRows = allRows
        .map((r) => ({ row: r, score: fuzzyScore(r.searchText, query) }))
        .filter((x) => x.score >= 0)
        .sort((a, b) => a.score - b.score)
        .map((x) => x.row);
    }
    selected = 0;
    render();
  }

  // -------- Activate (Enter or click) --------
  function activate(idx, openInBackground) {
    const row = visibleRows[idx];
    if (!row) return;
    if (row.kind === 'workspace') {
      chrome.send('switchWorkspaceAndClose', [row.id]);
    } else {
      chrome.send('openPageAndClose', [row.url, openInBackground ? 1 : 0]);
    }
  }

  // -------- Keyboard --------
  function onKey(e) {
    if (e.key === 'Escape') {
      window.close();
      return;
    }
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (visibleRows.length === 0) return;
      selected = (selected + 1) % visibleRows.length;
      render();
      return;
    }
    if (e.key === 'ArrowUp') {
      e.preventDefault();
      if (visibleRows.length === 0) return;
      selected = (selected - 1 + visibleRows.length) % visibleRows.length;
      render();
      return;
    }
    if (e.key === 'Enter') {
      e.preventDefault();
      activate(selected, e.metaKey || e.ctrlKey);
      return;
    }
  }

  // -------- C++ entry point --------
  window.sessionatQuickSwitchRender = function (workspaces) {
    allRows = buildRows(workspaces);
    applyFilter();
  };

  document.addEventListener('DOMContentLoaded', () => {
    const input = document.getElementById('q');
    input.addEventListener('input', applyFilter);
    input.addEventListener('keydown', onKey);
    document.addEventListener('keydown', (e) => {
      // Capture Esc / Arrow keys even if input isn't focused.
      if (document.activeElement !== input) onKey(e);
    });
    chrome.send('getWorkspaces');
  });
})();
