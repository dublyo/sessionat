// Sessionat — Workspaces Manager (chrome://sessionat-workspaces/)
//
// Pure DOM construction (no innerHTML) so the page works under the WebUI
// Trusted Types CSP. Render is invoked directly from C++ via
// CallJavascriptFunctionUnsafe("sessionatWorkspacesRender", list) — no
// cr.addWebUIListener dependency.

(function () {
  'use strict';

  /** All workspaces from C++. */
  let allWorkspaces = [];
  /** Current search filter (lowercase). */
  let filter = '';
  /** Set of workspace IDs whose items panel is expanded. */
  const expanded = new Set();
  /** Set to true once user clicks Expand all; auto-expanded by filter. */
  let expandAll = false;

  // ============ DOM helpers ============
  function el(tag, props, ...children) {
    const node = document.createElement(tag);
    if (props) {
      for (const key in props) {
        if (key === 'class') node.className = props[key];
        else if (key === 'style') {
          for (const k in props.style) node.style[k] = props.style[k];
        } else if (key.startsWith('on') && typeof props[key] === 'function') {
          node.addEventListener(key.slice(2).toLowerCase(), props[key]);
        } else if (key === 'text') {
          node.textContent = props[key];
        } else if (props[key] !== undefined && props[key] !== null) {
          node.setAttribute(key, props[key]);
        }
      }
    }
    for (const child of children) {
      if (child === null || child === undefined) continue;
      if (typeof child === 'string') node.appendChild(document.createTextNode(child));
      else node.appendChild(child);
    }
    return node;
  }

  function hostnameOf(url) {
    try { return new URL(url).hostname; } catch (e) { return url || ''; }
  }

  // chrome://favicon2 with allowGoogleServerFallback=1 does the Google
  // favicon CDN fetch inside the browser process (the WebUI URL loader
  // would otherwise reject cross-scheme requests from a chrome:// page
  // with a Mojo "Incorrect scheme" and kill the renderer). Adding
  // showFallbackMonogram=true makes the service draw a colored monogram
  // tile when no icon can be found at all — so we never need a JS fallback.
  function faviconUrl(url) {
    if (!url) return '';
    const u = new URL('chrome://favicon2/');
    u.searchParams.set('pageUrl', url);
    u.searchParams.set('size', '32');
    u.searchParams.set('allowGoogleServerFallback', '1');
    u.searchParams.set('showFallbackMonogram', 'true');
    return u.toString();
  }

  function createFavicon(url) {
    const host = hostnameOf(url);
    const img = document.createElement('img');
    img.className = 'ws-favicon';
    img.alt = '';
    img.src = faviconUrl(url);
    // Final JS fallback only if the favicon service itself errors out
    // (e.g. invalid pageUrl). Use a same-origin data: URI for the monogram
    // tile so we don't trigger another cross-scheme load.
    img.addEventListener('error', () => {
      const letter = (host || '?').charAt(0);
      const fb = document.createElement('div');
      fb.className = 'ws-favicon-fallback';
      fb.textContent = letter;
      img.replaceWith(fb);
    });
    return img;
  }

  // ============ Renderers ============
  function renderEmptyState() {
    const list = document.getElementById('workspacesList');
    list.replaceChildren(
      el('div', { class: 'empty-state' },
        el('h2', { text: 'No workspaces yet' }),
        el('p', { text: 'Click "+ New workspace" to make your first one. Then right-click any webpage and choose "Add page to workspace…" to save sites here.' }),
        el('button', {
          class: 'btn-primary',
          onClick: () => promptCreate(),
        }, '+ New workspace')
      )
    );
  }

  function renderItem(item) {
    const url = item.url || '';
    const title = item.title || url || '(untitled)';
    const host = hostnameOf(url);

    return el('div', {
      class: 'ws-item',
      title: url,
      onClick: () => { if (url) window.location.href = url; },
    },
      createFavicon(url),
      el('div', { class: 'ws-item-text' },
        el('div', { class: 'ws-item-title', text: title }),
        el('div', { class: 'ws-item-url', text: host })
      )
    );
  }

  function renderCard(ws) {
    const itemsFiltered = (ws.items || []).filter((it) => {
      if (!filter) return true;
      const t = (it.title || '').toLowerCase();
      const u = (it.url || '').toLowerCase();
      return t.includes(filter) || u.includes(filter);
    });

    // Auto-expand if user is actively filtering (so they see matches), or
    // if user hit Expand all, or if they've manually expanded this card.
    const isOpen = !!filter || expandAll || expanded.has(ws.id);

    const card = el('div', {
      class: 'ws-card' + (ws.is_active ? ' active' : ''),
    });

    const chev = el('span', {
      class: 'ws-chev',
      text: isOpen ? '▼' : '▶',
      title: isOpen ? 'Collapse' : 'Expand',
    });

    const head = el('div', {
      class: 'ws-head',
      onClick: (e) => {
        // Clicks on action buttons shouldn't toggle expand.
        if (e.target.closest('.ws-actions')) return;
        if (expanded.has(ws.id)) expanded.delete(ws.id);
        else expanded.add(ws.id);
        render(allWorkspaces);
      },
    },
      chev,
      el('div', {
        class: 'ws-icon',
        style: { background: ws.color || '#f97316' },
        text: ws.icon || '·',
      }),
      el('div', { class: 'ws-title' },
        el('div', { class: 'ws-name' },
          document.createTextNode(ws.name || 'Untitled'),
          ws.is_active
            ? el('span', { class: 'ws-active-pill', text: 'Active' })
            : null
        ),
        el('div', {
          class: 'ws-meta',
          text: (ws.items ? ws.items.length : 0) + ' saved '
                + ((ws.items && ws.items.length === 1) ? 'page' : 'pages'),
        })
      ),
      el('div', { class: 'ws-actions' },
        el('button', {
          class: 'primary-btn',
          title: 'Open every saved page in new background tabs',
          onClick: (e) => { e.stopPropagation();
                            chrome.send('switchWorkspace', [ws.id]); },
        }, 'Open tabs'),
        el('button', {
          title: 'Rename this workspace',
          onClick: (e) => { e.stopPropagation(); promptRename(ws); },
        }, 'Rename'),
        el('button', {
          class: 'danger',
          title: 'Delete this workspace',
          onClick: (e) => { e.stopPropagation(); confirmDelete(ws); },
        }, 'Delete')
      )
    );
    card.appendChild(head);

    if (isOpen) {
      const items = el('div', { class: 'ws-items' });
      if (itemsFiltered.length === 0) {
        items.appendChild(el('div', {
          class: 'ws-empty',
          text: filter
            ? 'No matches in this workspace.'
            : 'No saved pages yet. Right-click any webpage → Add page to workspace…',
        }));
      } else {
        itemsFiltered.forEach((it) => items.appendChild(renderItem(it)));
      }
      card.appendChild(items);
    }

    return card;
  }

  function render(workspaces) {
    allWorkspaces = workspaces || [];
    const list = document.getElementById('workspacesList');
    if (!list) return;

    if (allWorkspaces.length === 0) {
      renderEmptyState();
      return;
    }

    const cards = allWorkspaces
      .filter((ws) => {
        if (!filter) return true;
        if ((ws.name || '').toLowerCase().includes(filter)) return true;
        return (ws.items || []).some((it) =>
          (it.title || '').toLowerCase().includes(filter) ||
          (it.url || '').toLowerCase().includes(filter)
        );
      })
      .map(renderCard);

    if (cards.length === 0) {
      list.replaceChildren(el('div', { class: 'empty-state' },
        el('h2', { text: 'No matches' }),
        el('p', { text: 'No workspace or saved page matches "' + filter + '".' })
      ));
      return;
    }

    list.replaceChildren(...cards);
  }

  // Expose for C++ callback.
  window.sessionatWorkspacesRender = render;

  // ============ Actions ============
  function promptCreate() {
    const name = window.prompt('Workspace name', 'New');
    if (name && name.trim()) chrome.send('createWorkspace', [name.trim()]);
  }
  function promptRename(ws) {
    const nn = window.prompt('Rename workspace', ws.name);
    if (nn && nn !== ws.name) chrome.send('renameWorkspace', [ws.id, nn]);
  }
  function confirmDelete(ws) {
    if (window.confirm('Delete workspace "' + ws.name + '"?\nThis cannot be undone.')) {
      chrome.send('deleteWorkspace', [ws.id]);
    }
  }

  // ============ Wiring ============
  document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('newWorkspaceBtn')
      .addEventListener('click', promptCreate);

    const search = document.getElementById('searchInput');
    search.addEventListener('input', (e) => {
      filter = e.target.value.trim().toLowerCase();
      render(allWorkspaces);
    });

    const expandBtn = document.getElementById('expandAllBtn');
    if (expandBtn) expandBtn.addEventListener('click', () => {
      expandAll = true;
      expanded.clear();
      render(allWorkspaces);
    });
    const collapseBtn = document.getElementById('collapseAllBtn');
    if (collapseBtn) collapseBtn.addEventListener('click', () => {
      expandAll = false;
      expanded.clear();
      render(allWorkspaces);
    });

    chrome.send('getWorkspaces');
  });

  // Refresh on tab focus so external changes (NTP create, right-click add) show up.
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') chrome.send('getWorkspaces');
  });
})();
