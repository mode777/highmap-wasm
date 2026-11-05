import hljs from "highlight.js/lib/core";
import typescript from "highlight.js/lib/languages/typescript";

hljs.registerLanguage("typescript", typescript);

export function highlightSource(source: string): string {
  return hljs.highlight(source, { language: "typescript" }).value;
}
