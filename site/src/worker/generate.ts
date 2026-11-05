import { initHighmap, type Highmap } from "highmap-wasm";
import { demos } from "../demos/index.js";

interface GenerateRequest {
  id: number;
  demoId: string;
}

interface GenerateSuccess {
  id: number;
  ok: true;
  data: Float32Array;
  shape: [number, number];
  durationMs: number;
}

interface GenerateFailure {
  id: number;
  ok: false;
  error: string;
}

type GenerateResponse = GenerateSuccess | GenerateFailure;

const scope = self as unknown as {
  onmessage: ((event: MessageEvent<GenerateRequest>) => void) | null;
  postMessage(message: GenerateResponse, transfer?: Transferable[]): void;
};

let apiPromise: Promise<Highmap> | null = null;

function getApi(): Promise<Highmap> {
  if (!apiPromise) {
    apiPromise = initHighmap().catch((error: unknown) => {
      // Allow the retry action to try initialization again after a failed
      // fetch or instantiation.
      apiPromise = null;
      throw error;
    });
  }
  return apiPromise;
}

scope.onmessage = (event: MessageEvent<GenerateRequest>) => {
  const { id, demoId } = event.data;
  const demo = demos.find((entry) => entry.meta.id === demoId);
  if (!demo) {
    scope.postMessage({
      id,
      ok: false,
      error: `unknown demo: ${demoId}`,
    });
    return;
  }

  void getApi()
    .then((hmap) => {
      const started = performance.now();
      const heightmap = demo.build(hmap);
      const durationMs = performance.now() - started;
      const response: GenerateSuccess = {
        id,
        ok: true,
        data: heightmap.data,
        shape: heightmap.shape,
        durationMs,
      };
      scope.postMessage(response, [heightmap.data.buffer]);
    })
    .catch((error: unknown) => {
      scope.postMessage({
        id,
        ok: false,
        error: error instanceof Error ? error.message : String(error),
      });
    });
};
