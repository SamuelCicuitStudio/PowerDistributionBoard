  // ========================================================
  // ===============  USER / ADMIN MENU & TABS  =============
  // ========================================================

  function toggleUserMenu() {
    const menu = document.getElementById("userMenu");
    if (!menu) return;
    menu.style.display = menu.style.display === "block" ? "none" : "block";
  }

  document.addEventListener("click", (e) => {
    const menu = document.getElementById("userMenu");
    const icon = document.querySelector(".user-icon");
    if (!menu || !icon) return;
    if (!icon.contains(e.target) && !menu.contains(e.target)) {
      menu.style.display = "none";
    }
  });

