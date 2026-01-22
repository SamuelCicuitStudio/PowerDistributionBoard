export function qsa(selector, root = document) {
  return Array.from((root || document).querySelectorAll(selector));
}

export function qs(selector, root = document) {
  return (root || document).querySelector(selector);
}

