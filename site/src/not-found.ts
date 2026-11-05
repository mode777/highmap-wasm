import { renderFooter, renderHeader } from "./chrome.js";
import { append, el } from "./dom.js";

export function renderNotFound(path: string): HTMLElement {
  const page = el("div", "page");
  const main = el("main", "not-found");
  const back = el("a", "back-link", "Back to all specimens");
  back.href = "#/";

  const copy = el("div", "not-found-copy");
  copy.append(
    el("p", "eyebrow", "Not found"),
    el("h1", "hero-title", "This specimen is not in the catalog."),
    el(
      "p",
      "hero-lede",
      `The route “${path}” does not match a terrain. The gallery lists every example currently in the showcase.`,
    ),
  );

  main.append(copy, back);
  page.append(renderHeader(), main, renderFooter());
  return page;
}
