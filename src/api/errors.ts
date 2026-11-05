/** Package-level error type thrown by all highmap-wasm API surfaces. */
export class HighmapError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "HighmapError";
  }
}
