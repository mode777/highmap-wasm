import type { Heightmap } from "./demos/types.js";

export interface GeneratedTerrain extends Heightmap {
  durationMs: number;
}

interface WorkerSuccess {
  id: number;
  ok: true;
  data: Float32Array;
  shape: [number, number];
  durationMs: number;
}

interface WorkerFailure {
  id: number;
  ok: false;
  error: string;
}

type WorkerResponse = WorkerSuccess | WorkerFailure;

interface Pending {
  resolve: (value: GeneratedTerrain) => void;
  reject: (reason: Error) => void;
}

/** One wasm worker per detail-page session. */
export class MeshClient {
  private readonly worker: Worker;
  private readonly pending = new Map<number, Pending>();
  private nextId = 1;
  private disposed = false;

  constructor() {
    this.worker = new Worker(new URL("./worker/generate.ts", import.meta.url), {
      type: "module",
    });
    this.worker.onmessage = (event: MessageEvent<WorkerResponse>) => {
      const response = event.data;
      const request = this.pending.get(response.id);
      if (!request) return;
      this.pending.delete(response.id);

      if (response.ok) {
        request.resolve({
          data: response.data,
          shape: response.shape,
          durationMs: response.durationMs,
        });
      } else {
        request.reject(new Error(response.error));
      }
    };
    this.worker.onerror = (event) => {
      const error = new Error(event.message || "terrain worker failed");
      for (const request of this.pending.values()) request.reject(error);
      this.pending.clear();
    };
  }

  generate(demoId: string): Promise<GeneratedTerrain> {
    if (this.disposed) {
      return Promise.reject(new Error("the terrain worker has been disposed"));
    }

    const id = this.nextId;
    this.nextId += 1;

    return new Promise<GeneratedTerrain>((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.worker.postMessage({ id, demoId });
    });
  }

  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.worker.terminate();
    for (const request of this.pending.values()) {
      request.reject(new Error("the terrain worker was disposed"));
    }
    this.pending.clear();
  }
}
