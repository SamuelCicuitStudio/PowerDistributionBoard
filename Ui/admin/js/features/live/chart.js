import { qs } from "../../core/dom.js";

export function initLiveChart() {
  const scrollWrap = qs("[data-live-scroll]");
  if (!scrollWrap) return;
  const centerBtn = qs("[data-live-center]");

  let isDragging = false;
  let startX = 0;
  let startScroll = 0;

  const onPointerDown = (event) => {
    isDragging = true;
    startX = event.clientX;
    startScroll = scrollWrap.scrollLeft;
    scrollWrap.classList.add("is-dragging");
    scrollWrap.setPointerCapture?.(event.pointerId);
  };

  const onPointerMove = (event) => {
    if (!isDragging) return;
    const delta = event.clientX - startX;
    scrollWrap.scrollLeft = startScroll - delta;
  };

  const onPointerUp = (event) => {
    if (!isDragging) return;
    isDragging = false;
    scrollWrap.classList.remove("is-dragging");
    scrollWrap.releasePointerCapture?.(event.pointerId);
  };

  scrollWrap.addEventListener("pointerdown", onPointerDown);
  scrollWrap.addEventListener("pointermove", onPointerMove);
  scrollWrap.addEventListener("pointerup", onPointerUp);
  scrollWrap.addEventListener("pointerleave", onPointerUp);

  if (centerBtn) {
    centerBtn.addEventListener("click", () => {
      scrollWrap.scrollLeft = scrollWrap.scrollWidth - scrollWrap.clientWidth;
    });
  }
}
