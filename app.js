const fileList = document.querySelector('#file-list');
const searchInput = document.querySelector('#search-input');
const scanButton = document.querySelector('#scan-button');
const toast = document.querySelector('#toast');
let files = [];
let toastTimer;

const iconFor = (type) => {
  if (type.startsWith('image/')) return '▧';
  if (type.includes('zip') || type.includes('archive')) return '▱';
  if (type.includes('csv') || type.includes('spreadsheet')) return '▦';
  return '▤';
};

const formatBytes = (bytes) => {
  if (bytes < 1024) return `${bytes} B`;
  const units = ['KB', 'MB', 'GB'];
  let value = bytes / 1024;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit += 1;
  }
  return `${value.toFixed(value >= 10 ? 0 : 1)} ${units[unit]}`;
};

const escapeHtml = (value) => String(value).replace(/[&<>"']/g, (character) => ({
  '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
})[character]);

async function api(path, options = {}) {
  const response = await fetch(path, {
    ...options,
    headers: { Accept: 'application/json', ...(options.headers || {}) },
  });
  const payload = await response.json();
  if (!response.ok) throw new Error(payload.message || `Request failed (${response.status}).`);
  return payload;
}

function showToast(message, isError = false) {
  window.clearTimeout(toastTimer);
  toast.textContent = message;
  toast.classList.toggle('error', isError);
  toast.classList.add('visible');
  toastTimer = window.setTimeout(() => toast.classList.remove('visible'), 3200);
}

function updateStats() {
  const counts = files.reduce((result, file) => {
    result[file.status] = (result[file.status] || 0) + 1;
    return result;
  }, {});
  document.querySelector('#stat-total').textContent = files.length;
  document.querySelector('#stat-active').textContent = counts.Active || 0;
  document.querySelector('#stat-deleted').textContent = counts.Deleted || 0;
  document.querySelector('#stat-recovered').textContent = counts.Recovered || 0;
}

function renderFiles() {
  const query = searchInput.value.trim().toLowerCase();
  const visibleFiles = files.filter((file) =>
    `${file.name} ${file.id} ${file.type} ${file.status}`.toLowerCase().includes(query));
  if (!visibleFiles.length) {
    fileList.innerHTML = `<tr><td class="table-message" colspan="6">${files.length ? 'No matching files.' : 'No files in this case yet.'}</td></tr>`;
  } else {
    fileList.innerHTML = visibleFiles.map((file) => {
      const status = file.status.toLowerCase();
      const action = file.status === 'Deleted'
        ? `<button class="text-button recover" data-action="recover" data-id="${escapeHtml(file.id)}">Recover</button>`
        : file.status === 'Active'
          ? `<button class="text-button" data-action="delete" data-id="${escapeHtml(file.id)}">Delete</button>`
          : '';
      return `<tr>
        <td><div class="file-cell"><span class="file-icon" aria-hidden="true">${iconFor(file.type)}</span><div><div class="file-name">${escapeHtml(file.name)}</div><div class="file-id">${escapeHtml(file.id)}</div></div></div></td>
        <td>${escapeHtml(file.type)}</td><td>${formatBytes(file.sizeBytes)}</td>
        <td><span class="status status-${status}">${escapeHtml(file.status)}</span></td>
        <td>${escapeHtml(file.modifiedAt || '—')}</td>
        <td><div class="row-actions"><button class="text-button" data-action="inspect" data-id="${escapeHtml(file.id)}">Details</button>${action}</div></td>
      </tr>`;
    }).join('');
  }
  document.querySelector('#file-count').textContent =
    `Showing ${visibleFiles.length} of ${files.length} records`;
  updateStats();
}

async function refreshFiles() {
  files = await api('/api/files');
  renderFiles();
}

async function runScan() {
  scanButton.disabled = true;
  scanButton.innerHTML = '<span aria-hidden="true">…</span> Scanning virtual storage';
  try {
    const deleted = await api('/api/scan');
    await refreshFiles();
    showToast(`Scan complete — ${deleted.length} deleted ${deleted.length === 1 ? 'file' : 'files'} found.`);
  } catch (error) {
    showToast(error.message, true);
  } finally {
    scanButton.disabled = false;
    scanButton.innerHTML = '<span aria-hidden="true">⌕</span> Scan for deleted files';
  }
}

async function inspectFile(id) {
  try {
    const file = await api(`/api/files/${encodeURIComponent(id)}`);
    document.querySelector('#details-title').textContent = file.name;
    const fields = [
      ['File ID', file.id], ['Status', file.status], ['File type', file.type],
      ['Size', formatBytes(file.sizeBytes)], ['Created', file.createdAt],
      ['Last modified', file.modifiedAt], ['Deleted', file.deletedAt || 'Not deleted'],
      ['Simulated offset', `${file.simulatedOffset} bytes`],
    ];
    document.querySelector('#details-content').innerHTML = fields.map(([label, value]) =>
      `<div class="detail-item"><div class="detail-label">${escapeHtml(label)}</div><div class="detail-value">${escapeHtml(value)}</div></div>`).join('');
    document.querySelector('#details-dialog').showModal();
  } catch (error) {
    showToast(error.message, true);
  }
}

async function changeFileState(id, action) {
  const verb = action === 'recover' ? 'recover' : 'mark as deleted';
  if (!window.confirm(`Simulate ${verb} for ${id}? This only changes the virtual record.`)) return;
  try {
    const result = await api(`/api/files/${encodeURIComponent(id)}/${action}`, { method: 'POST' });
    await refreshFiles();
    showToast(result.message);
  } catch (error) {
    showToast(error.message, true);
  }
}

async function openReport() {
  try {
    const result = await api('/api/report');
    document.querySelector('#report-content').textContent = result.report;
    document.querySelector('#report-dialog').showModal();
  } catch (error) {
    showToast(error.message, true);
  }
}

fileList.addEventListener('click', (event) => {
  const button = event.target.closest('button[data-action]');
  if (!button) return;
  if (button.dataset.action === 'inspect') inspectFile(button.dataset.id);
  else changeFileState(button.dataset.id, button.dataset.action);
});
searchInput.addEventListener('input', renderFiles);
scanButton.addEventListener('click', runScan);
document.querySelector('#report-button').addEventListener('click', openReport);
document.querySelector('#download-report').addEventListener('click', () => {
  const report = document.querySelector('#report-content').textContent;
  const link = document.createElement('a');
  link.href = URL.createObjectURL(new Blob([report], { type: 'text/plain;charset=utf-8' }));
  link.download = 'forensics-simulation-report.txt';
  link.click();
  URL.revokeObjectURL(link.href);
});
document.querySelectorAll('[data-close]').forEach((button) => {
  button.addEventListener('click', () => document.querySelector(`#${button.dataset.close}`).close());
});
document.querySelectorAll('dialog').forEach((dialog) => {
  dialog.addEventListener('click', (event) => {
    if (event.target === dialog) dialog.close();
  });
});

refreshFiles().catch((error) => {
  fileList.innerHTML = `<tr><td class="table-message" colspan="6">${escapeHtml(error.message)} Check that the local C++ server is running.</td></tr>`;
  document.querySelector('#file-count').textContent = 'Could not load records';
  showToast(error.message, true);
});
