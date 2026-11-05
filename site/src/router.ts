export type Route =
  | { kind: "gallery" }
  | { kind: "demo"; id: string; preview: boolean }
  | { kind: "not-found"; path: string };

export function parseHash(hash: string): Route {
  const raw = hash.startsWith("#") ? hash.slice(1) : hash;
  const [pathPart = "", query = ""] = raw.split("?");
  const path = pathPart || "/";

  if (path === "/") return { kind: "gallery" };

  if (path.startsWith("/p/")) {
    let id: string;
    try {
      id = decodeURIComponent(path.slice(3));
    } catch {
      return { kind: "not-found", path };
    }
    if (!id) return { kind: "not-found", path };
    const preview = new URLSearchParams(query).get("preview") === "1";
    return { kind: "demo", id, preview };
  }

  return { kind: "not-found", path };
}

export function startRouter(onChange: (route: Route) => void): () => void {
  const update = (): void => onChange(parseHash(window.location.hash));
  update();
  window.addEventListener("hashchange", update);
  return () => window.removeEventListener("hashchange", update);
}
