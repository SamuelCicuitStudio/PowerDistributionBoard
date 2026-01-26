import { qs } from "../../core/dom.js";

const LEVEL_TAG = {
  ok: "OK",
  warn: "WARN",
  err: "ERR",
  info: "INFO",
};

export function renderNotifications(listEl, items = [], templateEl) {
  if (!listEl) return;
  if (!templateEl) {
    listEl.innerHTML = "";
    return;
  }

  listEl.innerHTML = "";
  items.forEach((item) => {
    const node = createNotificationNode(templateEl, item);
    if (node) listEl.appendChild(node);
  });
}

export function createNotificationNode(templateEl, item) {
  if (!templateEl?.content) return null;

  const fragment = templateEl.content.cloneNode(true);
  const root = fragment.firstElementChild;
  if (!root) return null;

  const level = item.level || "info";
  root.dataset.noticeLevel = level;

  const titleEl = qs("[data-notice-title]", root);
  const descEl = qs("[data-notice-desc]", root);
  const tagEl = qs("[data-notice-tag]", root);

  if (titleEl) titleEl.textContent = item.title || "";
  if (descEl) descEl.textContent = item.message || "";
  if (tagEl) tagEl.textContent = LEVEL_TAG[level] || String(level).toUpperCase();

  return root;
}

