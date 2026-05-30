// Sessionat — Visit Analytics dashboard (chrome://sessionat-analytics/).
// Local-only data; pulls from VisitAnalyticsService via chrome.send.

(function () {
  'use strict';

  // ---- State ----
  let workspaces = [];          // Full list for the filter dropdown.
  let currentRange = 'today';   // 'today' | '7d' | '30d'.
  let currentWsFilter = '';     // '' = all workspaces.
  let lastPayload = null;       // Most recent getRangeData payload.

  const SVG_NS = 'http://www.w3.org/2000/svg';

  // ============ Helpers ============
  function faviconForUrl(url) {
    if (!url) return '';
    const u = new URL('chrome://favicon2/');
    u.searchParams.set('pageUrl', url);
    u.searchParams.set('size', '32');
    u.searchParams.set('allowGoogleServerFallback', '1');
    u.searchParams.set('showFallbackMonogram', 'true');
    return u.toString();
  }
  function faviconForHost(host) {
    return faviconForUrl('https://' + host + '/');
  }

  function el(tag, props, ...kids) {
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
    for (const c of kids) {
      if (c == null) continue;
      n.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
    }
    return n;
  }

  function svg(tag, attrs) {
    const n = document.createElementNS(SVG_NS, tag);
    for (const k in attrs) {
      if (attrs[k] != null) n.setAttribute(k, attrs[k]);
    }
    return n;
  }

  function timeAgo(ms) {
    const now = Date.now();
    const diff = Math.max(0, now - ms);
    const m = Math.floor(diff / 60000);
    if (m < 1) return 'just now';
    if (m < 60) return m + 'm ago';
    const h = Math.floor(m / 60);
    if (h < 24) return h + 'h ago';
    return Math.floor(h / 24) + 'd ago';
  }

  // "1h 23m" / "5m" / "47s" / "—"
  function formatDuration(ms) {
    if (!ms || ms <= 0) return '—';
    const s = Math.floor(ms / 1000);
    if (s < 60) return s + 's';
    const m = Math.floor(s / 60);
    if (m < 60) return m + 'm';
    const h = Math.floor(m / 60);
    const rem = m % 60;
    return rem === 0 ? h + 'h' : h + 'h ' + rem + 'm';
  }

  const CATEGORY_COLORS = {
    'Development': '#10b981',
    'Social': '#f59e0b',
    'News': '#6366f1',
    'Reference': '#0ea5e9',
    'Entertainment': '#ef4444',
    'Shopping': '#14b8a6',
    'Work': '#ec4899',
    'Finance': '#84cc16',
    'Email': '#8b5cf6',
    'Other': '#9ca3af',
  };
  function colorForCategory(cat) {
    return CATEGORY_COLORS[cat] || '#9ca3af';
  }

  function formatHourLabel(hour) {
    if (hour === 0) return '12a';
    if (hour < 12) return hour + 'a';
    if (hour === 12) return '12p';
    return (hour - 12) + 'p';
  }

  function formatDayLabel(date) {
    return ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'][date.getDay()] +
           ' ' + date.getDate();
  }

  // ============ Renderers ============
  function renderStats(p) {
    const total = p.total_visits || 0;
    const unique = p.unique_hosts || 0;
    document.getElementById('statTotal').textContent = String(total);
    document.getElementById('statUnique').textContent = String(unique);
    document.getElementById('statRangeLabel').textContent = p.range_label || '';

    // Active time (was: top site stat tile).
    document.getElementById('statActiveTime').textContent =
        formatDuration(p.total_active_ms || 0);

    // Per-day average.
    let days = 1;
    if (p.range_key === '7d') days = 7;
    else if (p.range_key === '30d') days = 30;
    const avg = total > 0 ? (total / days).toFixed(1) : '0';
    document.getElementById('statAvgPerDay').textContent = avg;
  }

  function renderCategories(p) {
    const list = document.getElementById('categoryList');
    const sub = document.getElementById('categoriesSub');
    if (!list) return;
    const cats = p.categories || [];
    sub.textContent = cats.length
        ? '· ' + cats.length + ' categor' + (cats.length === 1 ? 'y' : 'ies')
        : '';
    if (cats.length === 0) {
      list.replaceChildren(el('p', { class: 'muted',
        text: 'No category data yet. Browse a few popular sites and they\'ll show up here.' }));
      return;
    }
    list.replaceChildren(...cats.map((c) =>
      el('div', { class: 'cat-row' },
        el('span', { class: 'cat-dot',
          style: { background: colorForCategory(c.category) } }),
        el('div', { class: 'cat-text' },
          el('div', { class: 'cat-name', text: c.category }),
          el('div', { class: 'cat-meta',
            text: c.count + ' visit' + (c.count === 1 ? '' : 's') })
        ),
        el('div', { class: 'cat-time', text: formatDuration(c.active_ms) })
      )
    ));
  }

  function renderChart(p) {
    const chart = document.getElementById('chart');
    if (!chart) return;
    chart.replaceChildren();
    const buckets = p.buckets || [];
    const max = Math.max(1, ...buckets);

    document.getElementById('chartTitle').textContent =
        p.range_key === 'today' ? 'Activity by hour'
      : p.range_key === '7d' ? 'Activity by day · last 7 days'
      : 'Activity by day · last 30 days';

    const W = 1000, H = 220;
    const pad = { top: 16, right: 8, bottom: 8, left: 8 };
    const innerW = W - pad.left - pad.right;
    const innerH = H - pad.top - pad.bottom;
    const n = buckets.length;
    if (n === 0) {
      chart.appendChild(svg('text', {
        x: W / 2, y: H / 2,
        'text-anchor': 'middle',
        fill: 'var(--text-tertiary)',
        'font-size': '13',
      })).textContent = 'No data yet';
      renderChartXAxis(p, []);
      return;
    }

    const gap = n > 24 ? 2 : 4;
    const barW = Math.max(2, (innerW - gap * (n - 1)) / n);

    for (let i = 0; i < n; i++) {
      const v = buckets[i];
      const h = max > 0 ? (v / max) * innerH : 0;
      const x = pad.left + i * (barW + gap);
      const y = pad.top + (innerH - h);
      const rect = svg('rect', {
        x, y, width: barW, height: h || 1,
        rx: 2,
        class: v > 0 ? 'chart-bar' : 'chart-bar-empty',
      });
      rect.appendChild(svg('title', {})).textContent =
          v + (v === 1 ? ' visit' : ' visits');
      chart.appendChild(rect);
    }

    // Sparse value labels on the top of the tallest bars (avoid clutter).
    if (max > 0) {
      const labelEvery = n > 24 ? 7 : n > 14 ? 3 : 1;
      for (let i = 0; i < n; i++) {
        if (i % labelEvery !== 0) continue;
        const v = buckets[i];
        if (v === 0) continue;
        const h = (v / max) * innerH;
        const x = pad.left + i * (barW + gap) + barW / 2;
        const y = pad.top + (innerH - h) - 4;
        const t = svg('text', {
          x, y, class: 'chart-value-label',
        });
        t.textContent = String(v);
        chart.appendChild(t);
      }
    }

    renderChartXAxis(p, buckets);
  }

  function renderChartXAxis(p, buckets) {
    const axis = document.getElementById('chartXAxis');
    if (!axis) return;
    axis.replaceChildren();
    if (!buckets || buckets.length === 0) return;
    const startMs = p.range_start_ms;
    if (p.range_key === 'today') {
      // Show 12a, 6a, 12p, 6p, 12a labels.
      for (let h of [0, 6, 12, 18]) {
        axis.appendChild(el('span', { text: formatHourLabel(h) }));
      }
    } else {
      // For 7d, show every day. For 30d, show ~7 labels evenly spaced.
      const days = buckets.length;
      const step = days <= 7 ? 1 : Math.ceil(days / 7);
      for (let i = 0; i < days; i += step) {
        const d = new Date(startMs + i * (p.bucket_ms || 86400000));
        axis.appendChild(el('span', { text: formatDayLabel(d) }));
      }
    }
  }

  function renderTopHosts(p) {
    const list = document.getElementById('topHostsList');
    const sub = document.getElementById('topSitesSub');
    if (!list) return;
    const hosts = p.top_hosts || [];
    sub.textContent = hosts.length ? '· ' + hosts.length + ' unique' : '';
    if (hosts.length === 0) {
      list.replaceChildren(el('p', { class: 'muted', text: 'No visits in this range yet.' }));
      return;
    }
    const max = hosts[0].count || 1;
    list.replaceChildren(...hosts.slice(0, 50).map((h) =>
      el('div', {
        class: 'host-row',
        onClick: () => loadHostDetail(h.host),
        title: 'Click to see all visits to ' + h.host,
      },
        el('img', { class: 'host-favicon', src: faviconForHost(h.host), alt: '' }),
        el('div', { class: 'host-name', text: h.host }),
        el('div', { class: 'host-bar' },
          el('div', { class: 'host-bar-fill',
            style: { width: ((h.count / max) * 100) + '%' } })
        ),
        el('div', { class: 'host-count', text: String(h.count) })
      )
    ));
  }

  function renderVisits(p, listEl, items, emptyMsg) {
    if (!listEl) return;
    if (!items || items.length === 0) {
      listEl.replaceChildren(el('p', { class: 'muted', text: emptyMsg }));
      return;
    }
    listEl.replaceChildren(...items.slice(0, 200).map((v) =>
      el('a', {
        class: 'visit-row',
        href: v.url,
        target: '_blank',
        rel: 'noopener',
      },
        el('img', { class: 'visit-favicon', src: faviconForUrl(v.url), alt: '' }),
        el('div', { class: 'visit-text' },
          el('div', { class: 'visit-title', text: v.title || v.url }),
          el('div', { class: 'visit-meta' },
            v.workspace_color
              ? el('span', { class: 'ws-dot', style: { background: v.workspace_color } })
              : null,
            document.createTextNode(
              v.host + (v.workspace_name ? ' · ' + v.workspace_name : '')
            )
          )
        ),
        el('div', { class: 'visit-time', text: timeAgo(v.timestamp) })
      )
    ));
  }

  function renderTimeline(p) {
    const sub = document.getElementById('recentVisitsSub');
    sub.textContent = p.visits && p.visits.length > 0
        ? '· ' + p.visits.length + ' shown'
        : '';
    renderVisits(p,
        document.getElementById('visitsList'),
        p.visits,
        'No visits in this range yet.');
  }

  function renderWorkspaceStats(p) {
    const box = document.getElementById('workspaceStats');
    if (!box) return;
    const ws = p.workspaces || [];
    if (ws.length === 0) {
      box.replaceChildren(el('p', { class: 'muted',
        text: 'No workspaces yet — create one in the New Tab.' }));
      return;
    }
    box.replaceChildren(...ws.map((w) =>
      el('div', {
        class: 'ws-stat' + (w.id === currentWsFilter ? ' active' : ''),
        onClick: () => {
          // Toggle filter on workspace tile click.
          currentWsFilter = (currentWsFilter === w.id) ? '' : w.id;
          document.getElementById('workspaceFilter').value = currentWsFilter;
          requestData();
        },
        title: (currentWsFilter === w.id ? 'Clear filter' : 'Filter to ' + w.name),
      },
        el('div', {
          class: 'ws-stat-icon',
          style: { background: w.color || '#f97316' },
          text: w.icon || '·',
        }),
        el('div', { class: 'ws-stat-text' },
          el('div', { class: 'ws-stat-name', text: w.name || 'Untitled' }),
          el('div', { class: 'ws-stat-count',
            text: (w.count || 0) + ' visits' })
        )
      )
    ));
  }

  // ============ Host detail drilldown ============
  function loadHostDetail(host) {
    chrome.send('getHostDetail', [currentRange, currentWsFilter, host]);
  }

  // ============ Privacy controls renderers ============
  window.sessionatAnalyticsRenderPrivacy = function (payload) {
    const tgl = document.getElementById('trackingToggle');
    if (tgl) tgl.checked = !!payload.tracking_enabled;
    const ta = document.getElementById('excludedHosts');
    if (ta) ta.value = (payload.excluded_hosts || []).join('\n');
    const s = document.getElementById('exclusionsStatus');
    if (s) s.textContent = '';
  };

  window.sessionatAnalyticsDownloadExport = function (payload) {
    if (!payload || !payload.json) return;
    const blob = new Blob([payload.json], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = payload.filename || 'sessionat-visits.json';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  };

  window.sessionatAnalyticsRenderHostDetail = function (payload) {
    const card = document.getElementById('hostDetailCard');
    const title = document.getElementById('hostDetailTitle');
    const list = document.getElementById('hostDetailList');
    if (!card || !payload) return;
    card.style.display = '';
    title.textContent = payload.host + ' — all visits in range';
    renderVisits(payload, list, payload.visits, 'No visits recorded.');
    card.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
  };

  // ============ Master entry from C++ ============
  window.sessionatAnalyticsRenderRangeData = function (payload) {
    lastPayload = payload;
    renderStats(payload);
    renderChart(payload);
    renderTopHosts(payload);
    renderTimeline(payload);
    renderCategories(payload);
    renderWorkspaceStats(payload);
  };

  // Workspaces dropdown comes via a separate handler so we can populate it
  // once at load and not on every range change.
  window.sessionatAnalyticsRenderWorkspaces = function (list) {
    workspaces = list || [];
    const select = document.getElementById('workspaceFilter');
    const current = select.value;
    select.replaceChildren();
    select.appendChild(el('option', { value: '', text: 'All workspaces' }));
    workspaces.forEach((w) => {
      select.appendChild(el('option', { value: w.id, text: w.name }));
    });
    select.value = current;
  };

  // ============ Wiring ============
  function requestData() {
    chrome.send('getRangeData', [currentRange, currentWsFilter]);
  }

  document.addEventListener('DOMContentLoaded', () => {
    // Range chips.
    document.querySelectorAll('.chip').forEach((c) => {
      c.addEventListener('click', () => {
        document.querySelectorAll('.chip').forEach((x) => x.classList.remove('active'));
        c.classList.add('active');
        currentRange = c.getAttribute('data-range');
        requestData();
      });
    });

    // Workspace filter dropdown.
    document.getElementById('workspaceFilter').addEventListener('change', (e) => {
      currentWsFilter = e.target.value;
      requestData();
    });

    // Wipe history.
    document.getElementById('clearBtn').addEventListener('click', () => {
      if (window.confirm('Wipe ALL stored visit data?\nThis cannot be undone.')) {
        chrome.send('clearAllVisits');
      }
    });

    // Close host detail card.
    document.getElementById('hostDetailClose').addEventListener('click', () => {
      document.getElementById('hostDetailCard').style.display = 'none';
    });

    // First-run privacy notice. Stored in localStorage so it shows once.
    const PRIVACY_KEY = 'sessionat_privacy_notice_v1';
    try {
      if (!localStorage.getItem(PRIVACY_KEY)) {
        document.getElementById('privacyModal').style.display = '';
      }
    } catch (e) { /* localStorage disabled — show every time. */ }
    document.getElementById('privacyDismiss').addEventListener('click', () => {
      try { localStorage.setItem(PRIVACY_KEY, '1'); } catch (e) {}
      document.getElementById('privacyModal').style.display = 'none';
    });

    // Privacy controls wiring.
    const trackTgl = document.getElementById('trackingToggle');
    if (trackTgl) trackTgl.addEventListener('change', (e) => {
      chrome.send('setTrackingEnabled', [e.target.checked]);
    });
    const saveBtn = document.getElementById('saveExclusions');
    if (saveBtn) saveBtn.addEventListener('click', () => {
      const text = document.getElementById('excludedHosts').value;
      const hosts = text.split(/\n+/).map(s => s.trim()).filter(Boolean);
      chrome.send('setExcludedHosts', [hosts]);
      const s = document.getElementById('exclusionsStatus');
      if (s) {
        s.textContent = 'Saved ✓';
        setTimeout(() => { s.textContent = ''; }, 1500);
      }
    });
    const exportBtn = document.getElementById('exportBtn');
    if (exportBtn) exportBtn.addEventListener('click', () => {
      chrome.send('exportVisits');
    });

    chrome.send('getWorkspaces');
    chrome.send('getPrivacySettings');
    requestData();
  });

  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
      requestData();
    }
  });
})();
