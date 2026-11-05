import { renderFooter, renderHeader } from "./chrome.js";
import { demos, FAMILIES, FAMILY_LABELS, type DemoEntry } from "./demos/index.js";
import { append, el, featureList } from "./dom.js";
import type { TerrainFamily } from "./demos/types.js";

const previewUrls = import.meta.glob("./assets/previews/*.webp", {
  eager: true,
  query: "?url",
  import: "default",
}) as Record<string, string>;

const previewById = new Map(
  Object.entries(previewUrls).map(([file, url]) => {
    const name = file.split("/").pop() ?? "";
    return [name.replace(/\.webp$/, ""), url] as const;
  }),
);

const ACCENTS = ["#a85a32", "#6f5940", "#4e6a5d", "#8a5d76", "#9a6b2f", "#4f647c"];

function demoCard(demo: DemoEntry, index: number): HTMLElement {
  const card = el("a", "demo-card");
  card.href = `#/p/${encodeURIComponent(demo.meta.id)}`;
  card.dataset.family = demo.meta.family;
  card.dataset.palette = demo.meta.palette;
  card.style.setProperty(
    "--card-accent",
    ACCENTS[index % ACCENTS.length] ?? ACCENTS[0]!,
  );

  const visual = el("div", "demo-card-visual");
  visual.setAttribute("aria-hidden", "true");
  const preview = previewById.get(demo.meta.id);
  if (preview) {
    const image = el("img", "demo-card-image");
    image.src = preview;
    image.alt = "";
    image.width = 1280;
    image.height = 800;
    image.loading = "lazy";
    image.decoding = "async";
    visual.append(image);
  } else {
    visual.append(el("span", "demo-card-fallback"));
  }
  visual.append(
    el("span", "demo-card-ordinal", String(index + 1).padStart(2, "0")),
    el("span", "demo-card-family", FAMILY_LABELS[demo.meta.family]),
  );

  const body = el("div", "demo-card-body");
  body.append(
    el("p", "demo-card-source", demo.meta.source.example.split("/").pop() ?? ""),
    el("h2", "demo-card-title", demo.meta.title),
    el("p", "demo-card-description", demo.meta.description),
    featureList(demo.meta.features),
  );

  append(card, visual, body);
  return card;
}

export function renderGallery(): HTMLElement {
  const page = el("div", "page");
  const main = el("main", "gallery-main");

  const hero = el("section", "hero");
  const copy = el("div", "hero-copy");
  copy.append(
    el("p", "eyebrow", "Terrain specimens"),
    el("h1", "hero-title", "Landscapes, cut from a seed."),
    el(
      "p",
      "hero-lede",
      `${demos.length} small C++ terrain pipelines, ported to the browser and rendered as solid specimens. Orbit each block, inspect the cut faces, and read the exact recipe that produced it.`,
    ),
  );

  const facts = el("dl", "hero-facts");
  for (const [label, value] of [
    ["kernel", "HighMap"],
    ["runtime", "WebAssembly"],
    ["license", "LGPL-2.1"],
    ["specimens", String(demos.length)],
  ] as const) {
    const group = el("div", "hero-fact");
    group.append(el("dt", "", label), el("dd", "", value));
    facts.append(group);
  }
  hero.append(copy, facts);

  const filterBar = el("div", "filter-bar");
  filterBar.setAttribute("role", "group");
  filterBar.setAttribute("aria-label", "Filter specimens by family");

  const grid = el("section", "demo-grid");
  grid.setAttribute("aria-label", "Terrain specimens");
  for (const [index, demo] of demos.entries()) {
    grid.append(demoCard(demo, index));
  }

  const empty = el("p", "gallery-empty", "No specimens match this family.");
  empty.hidden = true;

  const cards = Array.from(grid.children) as HTMLElement[];
  const buttons: HTMLButtonElement[] = [];

  const applyFilter = (family: TerrainFamily | null): void => {
    for (const [index, button] of buttons.entries()) {
      const buttonFamily = index === 0 ? null : FAMILIES[index - 1]!;
      const pressed = buttonFamily === family;
      button.setAttribute("aria-pressed", String(pressed));
      button.classList.toggle("is-active", pressed);
    }
    for (const card of cards) {
      card.hidden = family !== null && card.dataset.family !== family;
    }
    empty.hidden = cards.some((card) => !card.hidden);
  };

  const filterButton = (
    label: string,
    family: TerrainFamily | null,
  ): HTMLButtonElement => {
    const button = el("button", "filter-button", label);
    button.type = "button";
    button.setAttribute("aria-pressed", "false");
    button.addEventListener("click", () => applyFilter(family));
    buttons.push(button);
    filterBar.append(button);
    return button;
  };

  filterButton("All", null);
  for (const family of FAMILIES) {
    filterButton(FAMILY_LABELS[family], family);
  }
  applyFilter(null);

  main.append(hero, filterBar, grid, empty);
  page.append(renderHeader(), main, renderFooter());
  return page;
}
