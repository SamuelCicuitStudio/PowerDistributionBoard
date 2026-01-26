const messageEl = document.querySelector("[data-login-fail-message]");
const backBtn = document.querySelector("[data-login-back]");

const params = new URLSearchParams(window.location.search);
const reason = params.get("reason");
const base = params.get("base");

if (messageEl && reason) {
  messageEl.textContent = reason;
}

if (backBtn) {
  backBtn.addEventListener("click", () => {
    const next = "login.html" + (base ? "?base=" + encodeURIComponent(base) : "");
    window.location.href = next;
  });
}

