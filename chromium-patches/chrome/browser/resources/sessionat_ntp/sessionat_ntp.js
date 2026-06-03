/**
 * Sessionat NTP - Using localStorage for Session History (stable version)
 */

// ============================================================================
// Trusted Types Policy
// ============================================================================

const trustedTypesPolicy = window.trustedTypes
  ? window.trustedTypes.createPolicy('sessionat-ntp', {
      createHTML: (html) => html,
    })
  : null;

function trustedHTML(html) {
  return trustedTypesPolicy ? trustedTypesPolicy.createHTML(html) : html;
}

// ============================================================================
// State Management
// ============================================================================

const state = {
  sessions: [],           // Recently closed (from TabRestoreService)
  savedSessions: [],      // User-saved sessions (localStorage)
  favorites: [
    { id: 1, name: 'Google', url: 'https://google.com' },
    { id: 2, name: 'GitHub', url: 'https://github.com' },
    { id: 3, name: 'YouTube', url: 'https://youtube.com' },
  ],
  settings: {
    autoSaveEnabled: true,
    minTabsThreshold: 5,
    autoSaveInterval: 10,
    saveOnClose: true,
  },
  autoSaveTimer: null,
};

// ============================================================================
// localStorage-based Session History
// ============================================================================

function loadSavedSessions() {
  try {
    const saved = localStorage.getItem('sessionat_saved_sessions');
    if (saved) {
      state.savedSessions = JSON.parse(saved);
    }
  } catch (e) {
    state.savedSessions = [];
  }
}

function saveSavedSessions() {
  try {
    localStorage.setItem('sessionat_saved_sessions', JSON.stringify(state.savedSessions));
  } catch (e) {
    // Silent fail
  }
}

function loadSettings() {
  try {
    const saved = localStorage.getItem('sessionat_settings');
    if (saved) {
      state.settings = { ...state.settings, ...JSON.parse(saved) };
    }
  } catch (e) {
    // Silent fail
  }
}

function saveSettings() {
  try {
    localStorage.setItem('sessionat_settings', JSON.stringify(state.settings));
  } catch (e) {
    // Silent fail
  }
}

function generateSessionId() {
  return 'sess_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
}

function generateSessionName() {
  const now = new Date();
  return now.toLocaleString('en-US', {
    month: 'short',
    day: 'numeric',
    hour: 'numeric',
    minute: '2-digit',
    hour12: true,
  });
}

// ============================================================================
// Utility Functions
// ============================================================================

function getDomain(url) {
  try {
    return new URL(url).hostname.replace('www.', '');
  } catch {
    return url;
  }
}

function formatRelativeTime(timestamp) {
  const now = Date.now();
  const diff = now - timestamp;
  const minutes = Math.floor(diff / (1000 * 60));
  const hours = Math.floor(diff / (1000 * 60 * 60));
  const days = Math.floor(diff / (1000 * 60 * 60 * 24));

  if (minutes < 1) return 'Just now';
  if (minutes < 60) return minutes + 'm ago';
  if (hours < 24) return hours + 'h ago';
  if (days < 7) return days + 'd ago';
  return new Date(timestamp).toLocaleDateString();
}

function formatDateTime(timestamp) {
  const date = new Date(timestamp);
  return date.toLocaleString('en-US', {
    month: 'short',
    day: 'numeric',
    hour: 'numeric',
    minute: '2-digit',
    hour12: true,
  });
}

// ============================================================================
// Render Functions
// ============================================================================

function renderFavorites() {
  const grid = document.getElementById('favoritesGrid');
  if (!grid) return;

  grid.innerHTML = trustedHTML(
    state.favorites
      .map(
        (fav) => `
      <a href="${fav.url}" class="favorite-item">
        <div class="favorite-icon">${fav.name.charAt(0)}</div>
        <span class="favorite-name">${fav.name}</span>
      </a>
    `
      )
      .join('')
  );
}

function renderSavedSessions() {
  const section = document.getElementById('savedSessionsSection');
  const list = document.getElementById('savedSessionsList');
  if (!section || !list) return;

  if (state.savedSessions.length === 0) {
    section.style.display = 'none';
    return;
  }

  section.style.display = 'block';

  list.innerHTML = trustedHTML(
    state.savedSessions
      .map(
        (session) => `
      <div class="saved-session-card" data-id="${session.id}">
        <div class="session-header" data-id="${session.id}">
          <div class="session-info">
            <div class="session-name">
              <svg class="expand-icon" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <polyline points="9 18 15 12 9 6"></polyline>
              </svg>
              ${session.customName || session.name}
              ${session.isAutoSave ? '<span class="auto-save-indicator">Auto</span>' : ''}
            </div>
            <div class="session-meta">
              <span>${session.tabs ? session.tabs.length : 0} tabs</span>
              <span>${formatDateTime(session.timestamp)}</span>
            </div>
          </div>
          <div class="session-actions">
            <button class="session-action-btn restore-btn" data-id="${session.id}">Restore All</button>
            <button class="session-action-btn delete" data-id="${session.id}">Delete</button>
          </div>
        </div>
        <div class="session-tabs-list" data-id="${session.id}" style="display: none;">
          ${(session.tabs || []).map((tab) => `
            <div class="session-tab-item" data-url="${tab.url}">
              <span class="tab-title">${tab.title || getDomain(tab.url)}</span>
              <span class="tab-domain">${getDomain(tab.url)}</span>
            </div>
          `).join('')}
        </div>
      </div>
    `
      )
      .join('')
  );

  // Attach event listeners
  list.querySelectorAll('.session-header').forEach((header) => {
    header.addEventListener('click', (e) => {
      // Don't toggle if clicking on buttons
      if (e.target.closest('.session-action-btn')) return;

      const sessionId = header.dataset.id;
      const tabsList = list.querySelector(`.session-tabs-list[data-id="${sessionId}"]`);
      const card = header.closest('.saved-session-card');

      if (tabsList) {
        const isExpanded = tabsList.style.display !== 'none';
        tabsList.style.display = isExpanded ? 'none' : 'block';
        card.classList.toggle('expanded', !isExpanded);
      }
    });
  });

  list.querySelectorAll('.restore-btn').forEach((btn) => {
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      restoreSavedSession(btn.dataset.id);
    });
  });

  list.querySelectorAll('.session-action-btn.delete').forEach((btn) => {
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      deleteSavedSession(btn.dataset.id);
    });
  });

  // Click on individual tab to open just that one
  list.querySelectorAll('.session-tab-item').forEach((tabItem) => {
    tabItem.addEventListener('click', (e) => {
      e.stopPropagation();
      const url = tabItem.dataset.url;
      if (url) {
        chrome.send('openUrl', [url]);
      }
    });
  });
}

function renderRecentSessions() {
  const grid = document.getElementById('sessionGrid');
  const emptyState = document.getElementById('emptyState');
  if (!grid) return;

  if (state.sessions.length === 0) {
    grid.style.display = 'none';
    if (emptyState) emptyState.style.display = 'flex';
    return;
  }

  grid.style.display = 'grid';
  if (emptyState) emptyState.style.display = 'none';

  grid.innerHTML = trustedHTML(
    state.sessions
      .map(
        (session) => `
      <div class="session-card" data-id="${session.id}" data-url="${session.url}">
        <div class="card-content">
          <div class="card-header">
            <span class="card-title">${session.title}</span>
          </div>
          <div class="card-meta">
            <span class="card-domain">${session.domain}</span>
            <span class="card-time">${formatRelativeTime(session.lastVisited)}</span>
          </div>
        </div>
      </div>
    `
      )
      .join('')
  );

  // Attach click handlers
  grid.querySelectorAll('.session-card').forEach((card) => {
    card.addEventListener('click', () => {
      const id = parseInt(card.dataset.id, 10);
      if (id) {
        chrome.send('restoreSession', [id]);
      }
    });
  });
}

// ============================================================================
// Session History Functions (localStorage-based)
// ============================================================================

function saveCurrentSession(customName, isAutoSave = false) {
  // Save from recently closed sessions
  const tabs = state.sessions.map((s) => ({
    url: s.url,
    title: s.title,
    favicon: '',
  }));

  if (tabs.length === 0) {
    return;
  }

  // If auto-save, update existing auto-save or create new
  if (isAutoSave) {
    const existingAutoSave = state.savedSessions.find((s) => s.isAutoSave);
    if (existingAutoSave) {
      existingAutoSave.tabs = tabs;
      existingAutoSave.timestamp = Date.now();
      saveSavedSessions();
      renderSavedSessions();
      return;
    }
  }

  const session = {
    id: generateSessionId(),
    name: generateSessionName(),
    customName: customName || '',
    timestamp: Date.now(),
    isAutoSave: isAutoSave,
    tabs: tabs,
  };

  // Manual save overwrites auto-save if exists
  if (!isAutoSave) {
    state.savedSessions = state.savedSessions.filter((s) => !s.isAutoSave);
  }

  state.savedSessions.unshift(session);

  // Keep only last 20 sessions
  if (state.savedSessions.length > 20) {
    state.savedSessions = state.savedSessions.slice(0, 20);
  }

  saveSavedSessions();
  renderSavedSessions();
}

function restoreSavedSession(sessionId) {
  const session = state.savedSessions.find((s) => s.id === sessionId);
  if (!session) return;

  // Open all tabs from the saved session
  session.tabs.forEach((tab) => {
    if (tab.url) {
      chrome.send('openUrl', [tab.url]);
    }
  });
}

function deleteSavedSession(sessionId) {
  state.savedSessions = state.savedSessions.filter((s) => s.id !== sessionId);
  saveSavedSessions();
  renderSavedSessions();
}

// ============================================================================
// Auto-Save Logic
// ============================================================================

function startAutoSave() {
  if (!state.settings.autoSaveEnabled) return;

  if (state.autoSaveTimer) {
    clearInterval(state.autoSaveTimer);
  }

  const intervalMs = state.settings.autoSaveInterval * 60 * 1000;

  state.autoSaveTimer = setInterval(() => {
    if (state.sessions.length >= state.settings.minTabsThreshold) {
      saveCurrentSession('', true);
    }
  }, intervalMs);
}

function stopAutoSave() {
  if (state.autoSaveTimer) {
    clearInterval(state.autoSaveTimer);
    state.autoSaveTimer = null;
  }
}

// ============================================================================
// Modal Functions
// ============================================================================

function showSaveModal() {
  const modal = document.getElementById('saveModal');
  if (modal) {
    modal.style.display = 'flex';
    const input = document.getElementById('sessionName');
    if (input) {
      input.value = '';
      input.focus();
    }
  }
}

function hideSaveModal() {
  const modal = document.getElementById('saveModal');
  if (modal) {
    modal.style.display = 'none';
  }
}

function showSettingsModal() {
  const modal = document.getElementById('settingsModal');
  if (modal) {
    document.getElementById('autoSaveEnabled').checked = state.settings.autoSaveEnabled;
    document.getElementById('minTabsThreshold').value = state.settings.minTabsThreshold;
    document.getElementById('autoSaveInterval').value = state.settings.autoSaveInterval;
    document.getElementById('saveOnClose').checked = state.settings.saveOnClose;
    modal.style.display = 'flex';
  }
}

function hideSettingsModal() {
  const modal = document.getElementById('settingsModal');
  if (modal) {
    modal.style.display = 'none';
  }
}

function applySettings() {
  state.settings.autoSaveEnabled = document.getElementById('autoSaveEnabled').checked;
  state.settings.minTabsThreshold = parseInt(document.getElementById('minTabsThreshold').value, 10) || 5;
  state.settings.autoSaveInterval = parseInt(document.getElementById('autoSaveInterval').value, 10) || 10;
  state.settings.saveOnClose = document.getElementById('saveOnClose').checked;

  saveSettings();

  if (state.settings.autoSaveEnabled) {
    startAutoSave();
  } else {
    stopAutoSave();
  }

  hideSettingsModal();
}

// ============================================================================
// WebUI Communication
// ============================================================================

window.cr = window.cr || {};
window.cr.webUIListenerCallback = function (event, data) {
  if (event === 'sessions-updated') {
    handleRecentSessionsUpdated(data);
  }
};

function handleRecentSessionsUpdated(rawSessions) {
  state.sessions = (rawSessions || [])
    .map((session) => {
      const firstTab = session.tabs && session.tabs.length > 0 ? session.tabs[0] : null;
      return {
        id: session.id,
        title: session.name || (firstTab ? firstTab.title : 'Untitled'),
        url: firstTab ? firstTab.url : '',
        domain: firstTab ? getDomain(firstTab.url) : '',
        lastVisited: session.timestamp || Date.now(),
      };
    })
    .filter((s) => s.url);

  renderRecentSessions();
}

// ============================================================================
// Event Listeners
// ============================================================================

function setupEventListeners() {
  // Save Session Button
  const saveBtn = document.getElementById('saveSessionBtn');
  if (saveBtn) {
    saveBtn.addEventListener('click', showSaveModal);
  }

  // Settings Button
  const settingsBtn = document.getElementById('settingsBtn');
  if (settingsBtn) {
    settingsBtn.addEventListener('click', showSettingsModal);
  }

  // Save Modal
  document.getElementById('saveModalClose')?.addEventListener('click', hideSaveModal);
  document.getElementById('saveModalCancel')?.addEventListener('click', hideSaveModal);
  document.getElementById('saveModalConfirm')?.addEventListener('click', () => {
    const name = document.getElementById('sessionName').value.trim();
    saveCurrentSession(name, false);
    hideSaveModal();
  });

  // Settings Modal
  document.getElementById('settingsModalClose')?.addEventListener('click', hideSettingsModal);
  document.getElementById('settingsModalCancel')?.addEventListener('click', hideSettingsModal);
  document.getElementById('settingsModalSave')?.addEventListener('click', applySettings);

  // Close modals on overlay click
  document.querySelectorAll('.modal-overlay').forEach((overlay) => {
    overlay.addEventListener('click', (e) => {
      if (e.target === overlay) {
        overlay.style.display = 'none';
      }
    });
  });

  // Restore All Button
  const restoreAllBtn = document.getElementById('restoreAllBtn');
  if (restoreAllBtn) {
    restoreAllBtn.addEventListener('click', () => {
      state.sessions.forEach((session) => {
        if (session.id) {
          chrome.send('restoreSession', [session.id]);
        }
      });
    });
  }

  // Search Input
  const searchInput = document.getElementById('searchInput');
  if (searchInput) {
    searchInput.addEventListener('keypress', (e) => {
      if (e.key === 'Enter') {
        const query = e.target.value.trim();
        if (!query) return;

        if (query.includes('.') && !query.includes(' ')) {
          const url = query.startsWith('http') ? query : 'https://' + query;
          chrome.send('openUrl', [url]);
        } else {
          chrome.send('openUrl', ['https://www.google.com/search?q=' + encodeURIComponent(query)]);
        }
      }
    });
  }

  // Keyboard shortcuts
  document.addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'l') {
      e.preventDefault();
      searchInput?.focus();
      searchInput?.select();
    }
    if (e.key === 'Escape') {
      hideSaveModal();
      hideSettingsModal();
    }
  });
}

// ============================================================================
// Initialization
// ============================================================================

function init() {
  // Load from localStorage
  loadSettings();
  loadSavedSessions();

  // Render static content
  renderFavorites();
  renderSavedSessions();

  // Set up event listeners
  setupEventListeners();

  // Request recently closed tabs from C++
  chrome.send('getRecentSessions');
  // Request workspaces from C++
  chrome.send('getWorkspaces');

  // Request top-sites-today widget data from C++ (VisitAnalyticsService).
  chrome.send('getTopSitesToday');

  // Start auto-save if enabled
  if (state.settings.autoSaveEnabled) {
    startAutoSave();
  }
}

// Render workspace cards. Backend pushes via FireWebUIListener('workspaces-updated', list).
function renderWorkspaces(workspaces) {
  const grid = document.getElementById('workspacesGrid');
  if (!grid) return;
  grid.innerHTML = '';
  // Cap NTP cards at 12 so the grid stays scannable — overflow lives in the
  // dedicated manager page + the Cmd+K spotlight.
  const NTP_CAP = 12;
  const total = (workspaces || []).length;
  const visible = (workspaces || []).slice(0, NTP_CAP);
  const moreLink = document.getElementById('workspacesMoreLink');
  if (moreLink) {
    if (total > NTP_CAP) {
      moreLink.style.display = '';
      const span = document.getElementById('workspacesTotal');
      if (span) span.textContent = String(total);
    } else {
      moreLink.style.display = 'none';
    }
  }
  visible.forEach((ws, idx) => {
    const card = document.createElement('div');
    card.className = 'workspace-card';
    if (ws.is_active) card.classList.add('active');
    if (ws.is_pinned) card.classList.add('pinned');
    const titleParts = [ws.name];
    if (ws.is_active) titleParts.push('(active)');
    if (ws.is_pinned) titleParts.push('(pinned)');
    if (idx < 9) titleParts.push('— Cmd+' + (idx + 1));
    card.title = titleParts.join(' ');
    card.addEventListener('click', (e) => {
      // Ignore clicks on action buttons.
      if (e.target.closest('.workspace-action')) return;
      chrome.send('switchWorkspace', [ws.id]);
    });

    const icon = document.createElement('div');
    icon.className = 'workspace-icon';
    icon.style.background = ws.color || '#f97316';
    icon.textContent = ws.icon || '·';

    const meta = document.createElement('div');
    meta.className = 'workspace-meta';
    const name = document.createElement('div');
    name.className = 'workspace-name';
    name.textContent = ws.name;
    if (ws.is_pinned) {
      const star = document.createElement('span');
      star.className = 'workspace-pin-indicator';
      star.textContent = ' ★';
      star.title = 'Pinned';
      name.appendChild(star);
    }
    const count = document.createElement('div');
    count.className = 'workspace-count';
    count.textContent = (ws.tab_count || 0) + ' tabs';
    if (idx < 9) {
      const kbd = document.createElement('span');
      kbd.className = 'workspace-kbd';
      kbd.textContent = '⌘' + (idx + 1);
      count.appendChild(document.createTextNode(' · '));
      count.appendChild(kbd);
    }
    meta.appendChild(name);
    meta.appendChild(count);

    // Pin toggle — appears on hover.
    const pin = document.createElement('button');
    pin.className = 'workspace-action workspace-pin';
    pin.textContent = ws.is_pinned ? '★' : '☆';
    pin.title = ws.is_pinned ? 'Unpin from top' : 'Pin to top';
    pin.addEventListener('click', (e) => {
      e.stopPropagation();
      chrome.send('togglePinWorkspace', [ws.id]);
    });

    // Delete (×) — appears only on hover via CSS; confirms before destroying.
    const del = document.createElement('button');
    del.className = 'workspace-action workspace-delete';
    del.textContent = '×';
    del.title = 'Delete workspace ' + ws.name;
    del.addEventListener('click', (e) => {
      e.stopPropagation();
      if (window.confirm('Delete workspace "' + ws.name + '"?')) {
        chrome.send('deleteWorkspace', [ws.id]);
      }
    });

    card.appendChild(icon);
    card.appendChild(meta);
    card.appendChild(pin);
    card.appendChild(del);
    grid.appendChild(card);
  });

  // "+ New workspace" tile — prompts for a name, calls the backend, then
  // refreshes the list.
  const newCard = document.createElement('div');
  newCard.className = 'workspace-card';
  newCard.style.borderStyle = 'dashed';
  newCard.style.color = 'var(--text-secondary)';
  newCard.title = 'Create a new Sessionat workspace';
  newCard.innerHTML =
      '<div class="workspace-icon" style="background: var(--bg-tertiary); color: var(--text-secondary);">+</div>' +
      '<div class="workspace-meta">' +
      '  <div class="workspace-name">New workspace</div>' +
      '  <div class="workspace-count">Click to add</div>' +
      '</div>';
  newCard.addEventListener('click', () => {
    const name = window.prompt('Workspace name', 'New');
    if (!name) return;
    chrome.send('createWorkspace', [name]);
  });
  grid.appendChild(newCard);
}

if (typeof cr !== 'undefined' && cr.addWebUIListener) {
  cr.addWebUIListener('workspaces-updated', renderWorkspaces);
}

// Top sites today widget — C++ pushes via direct CallJavascriptFunction
// rather than FireWebUIListener (no cr.js on this page).
function renderTopSitesToday(visits) {
  const section = document.getElementById('topSitesSection');
  const grid = document.getElementById('topSitesGrid');
  const empty = document.getElementById('topSitesEmpty');
  if (!section || !grid || !empty) return;
  if (!visits || visits.length === 0) {
    grid.hidden = true;
    grid.replaceChildren();
    empty.hidden = false;
    return;
  }
  empty.hidden = true;
  grid.hidden = false;
  grid.replaceChildren();
  visits.forEach((v) => {
    const card = document.createElement('a');
    card.className = 'top-site-card';
    card.href = v.url;

    const favUrl = new URL('chrome://favicon2/');
    favUrl.searchParams.set('pageUrl', v.url);
    favUrl.searchParams.set('size', '32');
    favUrl.searchParams.set('allowGoogleServerFallback', '1');
    favUrl.searchParams.set('showFallbackMonogram', 'true');
    const img = document.createElement('img');
    img.className = 'top-site-favicon';
    img.alt = '';
    img.src = favUrl.toString();
    card.appendChild(img);

    const meta = document.createElement('div');
    meta.className = 'top-site-meta';
    const name = document.createElement('div');
    name.className = 'top-site-name';
    name.textContent = v.title || v.host || v.url;
    const host = document.createElement('div');
    host.className = 'top-site-host';
    host.textContent = v.host || '';
    meta.appendChild(name);
    meta.appendChild(host);
    card.appendChild(meta);

    grid.appendChild(card);
  });
}
window.sessionatNtpRenderTopSitesToday = renderTopSitesToday;

// Refresh workspaces every time the NTP comes back into view so Cmd+1..9
// workspace switches reflect immediately (active card highlights, etc.).
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible') {
    chrome.send('getWorkspaces');
    chrome.send('getTopSitesToday');
  }
});

// Wire the header "+ New workspace" button (single source of truth for create).
document.addEventListener('DOMContentLoaded', () => {
  const btn = document.getElementById('newWorkspaceBtn');
  if (btn) {
    btn.addEventListener('click', () => {
      const name = window.prompt('Workspace name', 'New');
      if (!name) return;
      chrome.send('createWorkspace', [name]);
    });
  }
});

document.addEventListener('DOMContentLoaded', init);
