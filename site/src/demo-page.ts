import { renderFooter, renderHeader, repoFile } from "./chrome.js";
import { highlightSource } from "./code-view.js";
import type { DemoEntry } from "./demos/index.js";
import { getDemoSource } from "./demos/sources.js";
import { append, el, externalLink, featureList } from "./dom.js";
import { buildDiorama } from "./terrain/geometry.js";
import { Viewport } from "./viewport.js";
import type { MeshClient } from "./worker-client.js";

type Tab = "3d" | "code";

export interface DemoPageOptions {
  preview?: boolean;
}

export class DemoPage {
  readonly element: HTMLElement;

  private readonly demo: DemoEntry;
  private readonly source: string;
  private readonly client: MeshClient;
  private readonly panels: Record<Tab, HTMLElement>;
  private readonly tabs: Record<Tab, HTMLButtonElement>;
  private readonly stage: HTMLElement;
  private readonly status: HTMLElement;
  private readonly stats: HTMLElement;
  private readonly sourceLink: HTMLAnchorElement;
  private readonly wireframeButton: HTMLButtonElement;
  private readonly flatButton: HTMLButtonElement;
  private readonly copyButton: HTMLButtonElement;

  private viewport: Viewport | null = null;
  private activeTab: Tab = "3d";
  private loadToken = 0;
  private disposed = false;

  constructor(
    demo: DemoEntry,
    client: MeshClient,
    options: DemoPageOptions = {},
  ) {
    this.demo = demo;
    this.source = getDemoSource(demo.meta.id);
    this.client = client;

    this.element = el("div", options.preview ? "page is-preview" : "page");
    const main = el("main", "demo-layout");
    this.stage = el("section", "demo-stage");
    this.stage.setAttribute("aria-label", `${demo.meta.title} specimen`);

    const tabBar = el("div", "demo-tabs");
    tabBar.setAttribute("role", "tablist");
    tabBar.setAttribute("aria-label", "Specimen views");

    this.tabs = {
      "3d": el("button", "demo-tab is-active", "3D"),
      code: el("button", "demo-tab", "Code"),
    };
    this.panels = {
      "3d": el("div", "viewport-host"),
      code: el("div", "code-host"),
    };

    for (const tab of ["3d", "code"] as const) {
      const button = this.tabs[tab];
      const panel = this.panels[tab];
      button.type = "button";
      button.setAttribute("role", "tab");
      button.id = `tab-${tab}`;
      button.setAttribute("aria-controls", `panel-${tab}`);
      button.setAttribute("aria-selected", String(tab === "3d"));
      button.tabIndex = tab === "3d" ? 0 : -1;
      panel.id = `panel-${tab}`;
      panel.setAttribute("role", "tabpanel");
      panel.setAttribute("aria-labelledby", `tab-${tab}`);
      panel.hidden = tab !== "3d";
      button.addEventListener("click", () => this.setTab(tab));
      button.addEventListener("keydown", (event) =>
        this.onTabKey(event, tab),
      );
      tabBar.append(button);
    }

    const codeToolbar = el("div", "code-toolbar");
    const filename = demo.meta.source.example.split("/").pop() ?? "recipe.ts";
    codeToolbar.append(
      el("span", "code-filename", `${demo.meta.id}.ts`),
      el("span", "code-origin", filename),
    );
    this.copyButton = el("button", "code-copy", "Copy");
    this.copyButton.type = "button";
    this.copyButton.addEventListener("click", () => {
      void this.copySource();
    });
    codeToolbar.append(this.copyButton);

    const code = el("code", "hljs language-typescript");
    code.innerHTML = highlightSource(this.source);
    const pre = el("pre", "code-pre");
    pre.append(code);
    this.panels.code.append(codeToolbar, pre);

    this.status = el("div", "status-overlay");
    this.status.setAttribute("role", "status");
    this.status.setAttribute("aria-live", "polite");

    const body = el("div", "demo-stage-body");
    body.append(this.panels["3d"], this.panels.code, this.status);

    const metaBar = el("div", "demo-meta-bar");
    this.stats = el("div", "demo-stats", "Waiting for terrain…");
    const sourceWrap = el("div", "demo-source-link");
    this.sourceLink = externalLink(
      repoFile(demo.meta.source.example),
      "C++ source",
      "meta-link",
    );
    sourceWrap.append(this.sourceLink);
    metaBar.append(this.stats, sourceWrap);

    this.stage.append(tabBar, body, metaBar);

    const aside = el("aside", "demo-aside");
    const back = el("a", "back-link", "Back to all specimens");
    back.href = "#/";
    aside.append(
      back,
      el("p", "eyebrow", "Specimen"),
      el("h1", "demo-title", demo.meta.title),
      el("p", "demo-description", demo.meta.description),
      featureList(demo.meta.features),
    );

    const facts = el("dl", "demo-facts");
    const fact = (label: string, value: string | HTMLElement): void => {
      const group = el("div", "demo-fact");
      group.append(el("dt", "", label));
      const dd = el("dd");
      dd.append(typeof value === "string" ? document.createTextNode(value) : value);
      group.append(dd);
      facts.append(group);
    };
    fact("Grid", `${demo.meta.grid[0]} × ${demo.meta.grid[1]}`);
    fact(
      "Example",
      externalLink(
        repoFile(demo.meta.source.example),
        demo.meta.source.example.split("/").pop() ?? "example",
        "inline-link",
      ),
    );
    fact("Panel", demo.meta.source.panel);
    if (demo.meta.resolutionNote) {
      fact("Resolution", demo.meta.resolutionNote);
    }
    if (demo.meta.source.referenceImage) {
      fact(
        "Reference",
        externalLink(
          repoFile(demo.meta.source.referenceImage),
          "2D sample",
          "inline-link",
        ),
      );
    }
    aside.append(facts);

    const inspector = el("section", "inspector");
    inspector.append(el("h2", "inspector-title", "Inspector"));
    const toggles = el("div", "inspector-toggles");
    this.wireframeButton = el("button", "inspector-toggle", "Wireframe");
    this.flatButton = el("button", "inspector-toggle", "Flat");
    for (const [button, apply] of [
      [this.wireframeButton, (on: boolean) => this.viewport?.setWireframe(on)],
      [this.flatButton, (on: boolean) => this.viewport?.setFlatShading(on)],
    ] as const) {
      button.type = "button";
      button.setAttribute("aria-pressed", "false");
      button.addEventListener("click", () => {
        const next = button.getAttribute("aria-pressed") !== "true";
        button.setAttribute("aria-pressed", String(next));
        button.classList.toggle("is-active", next);
        apply(next);
      });
      toggles.append(button);
    }
    inspector.append(
      toggles,
      el(
        "p",
        "inspector-note",
        "The block is closed: top surface, cut faces, strata, and a flat base.",
      ),
    );
    aside.append(inspector);

    main.append(this.stage, aside);
    this.element.append(renderHeader(), main, renderFooter());
  }

  mount(): void {
    try {
      this.viewport = new Viewport(this.panels["3d"]);
    } catch (error) {
      this.showError(error);
      return;
    }
    this.load();
  }

  dispose(): void {
    this.disposed = true;
    this.loadToken += 1;
    this.viewport?.dispose();
    this.viewport = null;
  }

  private setTab(tab: Tab): void {
    if (this.activeTab === tab) return;
    this.activeTab = tab;
    for (const key of ["3d", "code"] as const) {
      const selected = key === tab;
      this.tabs[key].classList.toggle("is-active", selected);
      this.tabs[key].setAttribute("aria-selected", String(selected));
      this.tabs[key].tabIndex = selected ? 0 : -1;
      this.panels[key].hidden = !selected;
    }
    this.viewport?.setActive(tab === "3d");
  }

  private onTabKey(event: KeyboardEvent, tab: Tab): void {
    if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
    event.preventDefault();
    const next: Tab = tab === "3d" ? "code" : "3d";
    this.setTab(next);
    this.tabs[next].focus();
  }

  private load(): void {
    if (this.disposed) return;
    const token = ++this.loadToken;
    this.showLoading();
    this.stats.textContent = "Generating terrain…";
    delete this.stage.dataset.demoReady;
    this.resetInspector();

    this.client
      .generate(this.demo.meta.id)
      .then((terrain) => {
        if (this.disposed || token !== this.loadToken) return;
        const geometry = buildDiorama(terrain, {
          palette: this.demo.meta.palette,
          relief: this.demo.meta.relief,
        });
        this.viewport?.setTerrain(geometry);
        const vertices = terrain.shape[0] * terrain.shape[1];
        this.stats.textContent =
          `${terrain.shape[0]} × ${terrain.shape[1]} · ` +
          `${vertices.toLocaleString()} vertices · ` +
          `${geometry.totalTriangles.toLocaleString()} triangles · ` +
          `${Math.round(terrain.durationMs).toLocaleString()} ms`;
        this.stage.dataset.demoReady = "true";
        this.hideStatus();
      })
      .catch((error: unknown) => {
        if (this.disposed || token !== this.loadToken) return;
        this.showError(error);
      });
  }

  private resetInspector(): void {
    for (const button of [this.wireframeButton, this.flatButton]) {
      button.setAttribute("aria-pressed", "false");
      button.classList.remove("is-active");
    }
    this.viewport?.setWireframe(false);
    this.viewport?.setFlatShading(false);
  }

  private async copySource(): Promise<void> {
    try {
      await navigator.clipboard.writeText(this.source);
      this.setCopyLabel("Copied");
    } catch {
      this.setCopyLabel("Copy failed");
    }
  }

  private setCopyLabel(label: string): void {
    this.copyButton.textContent = label;
    window.setTimeout(() => {
      if (!this.disposed) this.copyButton.textContent = "Copy";
    }, 1600);
  }

  private showLoading(): void {
    this.status.className = "status-overlay is-visible";
    const card = el("div", "status-card");
    card.append(
      el("span", "status-spinner"),
      el("span", "status-label", "Generating terrain…"),
    );
    this.status.replaceChildren(card);
  }

  private showError(error: unknown): void {
    const message = error instanceof Error ? error.message : String(error);
    this.status.className = "status-overlay is-visible is-error";
    const card = el("div", "status-card");
    card.append(
      el("p", "status-title", "Generation failed"),
      el("p", "status-message", message),
    );
    const retry = el("button", "retry-button", "Try again");
    retry.type = "button";
    retry.addEventListener("click", () => this.load());
    card.append(retry);
    this.status.replaceChildren(card);
    this.stats.textContent = "No terrain";
    delete this.stage.dataset.demoReady;
  }

  private hideStatus(): void {
    this.status.className = "status-overlay";
    this.status.replaceChildren();
  }
}
