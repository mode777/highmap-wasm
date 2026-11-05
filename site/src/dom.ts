export type Child = Node | string;

export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  className = "",
  text?: string,
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

export function append(parent: Element, ...children: Child[]): Element {
  for (const child of children) {
    parent.append(typeof child === "string" ? document.createTextNode(child) : child);
  }
  return parent;
}

export function externalLink(
  href: string,
  text: string,
  className = "",
): HTMLAnchorElement {
  const link = el("a", className, text);
  link.href = href;
  link.target = "_blank";
  link.rel = "noreferrer";
  return link;
}

export function featureList(features: readonly string[]): HTMLElement {
  const list = el("ul", "feature-list");
  for (const feature of features) {
    list.append(el("li", "feature", feature));
  }
  return list;
}
