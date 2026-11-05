import { append, el, externalLink } from "./dom.js";

const REPO = "https://github.com/mode777/highmap-wasm";
const COMMIT = __COMMIT_SHA__ || "main";

export const repository = REPO;
export const commit = COMMIT;

export function repoFile(path: string): string {
  return `${REPO}/blob/main/${path}`;
}

export function sourceTree(): string {
  return `${REPO}/tree/${COMMIT}`;
}

export function renderHeader(): HTMLElement {
  const header = el("header", "site-header");
  const brand = el("a", "brand");
  brand.href = "#/";

  const mark = el("span", "brand-mark", "HighMap");
  const suffix = el("span", "brand-accent", " / showcase");
  append(brand, mark, suffix);

  const nav = el("nav", "site-nav");
  nav.setAttribute("aria-label", "Site navigation");
  nav.append(
    externalLink(`${REPO}#readme`, "Docs", "nav-link"),
    externalLink(REPO, "GitHub", "nav-link"),
  );

  append(header, brand, nav);
  return header;
}

export function renderFooter(): HTMLElement {
  const footer = el("footer", "site-footer");
  const links = el("div", "footer-links");
  links.append(
    externalLink(`${REPO}#readme`, "Docs"),
    externalLink(repoFile("docs/decisions.md"), "Decisions"),
    externalLink(repoFile("docs/wasm-port-research.md"), "Port research"),
    externalLink(repoFile("examples"), "C++ examples"),
    externalLink(repoFile("scripts/build-wasm.sh"), "Build recipe"),
    externalLink(repoFile("LICENSE"), "LGPL-2.1"),
    externalLink(sourceTree(), `Source @ ${COMMIT.slice(0, 7)}`),
  );

  const note = el(
    "p",
    "footer-note",
    "HighMap is LGPL-2.1. This showcase runs a CPU-only WebAssembly build of the library.",
  );

  append(footer, links, note);
  return footer;
}
