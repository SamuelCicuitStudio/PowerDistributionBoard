import { qsa } from "../core/dom.js";
import { TRANSLATIONS_EN } from "./i18n.en.js";
import { TRANSLATIONS_IT } from "./i18n.it.js";
import { TRANSLATIONS_FR } from "./i18n.fr.js";

const STORAGE_KEY = "ui.lang";

const TRANSLATIONS = {
  en: TRANSLATIONS_EN,
  it: TRANSLATIONS_IT,
  fr: TRANSLATIONS_FR,
};


function interpolate(value, vars) {
  if (!vars) return value;
  return Object.entries(vars).reduce(
    (acc, [key, val]) => acc.replaceAll(`{${key}}`, String(val)),
    value,
  );
}

export function initI18n() {
  let current = localStorage.getItem(STORAGE_KEY) || "en";

  const t = (key, vars) => {
    const base =
      TRANSLATIONS[current]?.[key] ||
      TRANSLATIONS.en[key] ||
      String(key || "");
    return interpolate(base, vars);
  };

  const apply = (root = document) => {
    qsa("[data-i18n]", root).forEach((node) => {
      node.textContent = t(node.dataset.i18n);
    });
    qsa("[data-i18n-placeholder]", root).forEach((node) => {
      node.setAttribute("placeholder", t(node.dataset.i18nPlaceholder));
    });
    qsa("[data-i18n-aria]", root).forEach((node) => {
      node.setAttribute("aria-label", t(node.dataset.i18nAria));
    });
    qsa("[data-i18n-title]", root).forEach((node) => {
      node.setAttribute("title", t(node.dataset.i18nTitle));
    });
    qsa("[data-output-label]", root).forEach((node) => {
      const index = node.getAttribute("data-output-label");
      if (!index) return;
      node.textContent = t("wizard.step4.output", { index });
    });
  };

  const setLang = (lang) => {
    if (!TRANSLATIONS[lang]) return;
    current = lang;
    localStorage.setItem(STORAGE_KEY, lang);
    document.documentElement.setAttribute("lang", lang);
    document.title = t("app.title");
    apply(document);
    qsa("[data-language-select]", document).forEach((select) => {
      select.value = lang;
    });
    document.dispatchEvent(
      new CustomEvent("language:change", { detail: { lang } }),
    );
  };

  const setupSelects = () => {
    qsa("[data-language-select]", document).forEach((select) => {
      select.value = current;
      select.addEventListener("change", (event) => {
        const next = event.target.value;
        setLang(next);
      });
    });
  };

  setLang(current);
  setupSelects();

  return { t, setLang, getLang: () => current, apply, translations: TRANSLATIONS };
}


