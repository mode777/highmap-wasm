import type { Heightmap } from "../demos/types.js";
import {
  bottomColor,
  getPalette,
  strataColor,
  topColor,
  type PaletteId,
} from "./palette.js";
import type { DioramaGeometry, MeshData } from "./types.js";

export interface DioramaOptions {
  palette: PaletteId;
  relief: number;
  baseDepth?: number;
  width?: number;
  depth?: number;
}

const SHELL_ROWS = 24;

function slopeAt(
  values: Float32Array,
  ni: number,
  nj: number,
  i: number,
  j: number,
): number {
  const left = values[j * ni + Math.max(0, i - 1)]!;
  const right = values[j * ni + Math.min(ni - 1, i + 1)]!;
  const down = values[Math.max(0, j - 1) * ni + i]!;
  const up = values[Math.min(nj - 1, j + 1) * ni + i]!;
  const dx = ((right - left) * 0.5) * Math.max(1, ni - 1);
  const dz = ((up - down) * 0.5) * Math.max(1, nj - 1);
  return Math.hypot(dx, dz);
}

function perimeter(ni: number, nj: number): Array<[number, number]> {
  const edges: Array<[number, number]> = [];

  for (let i = 0; i < ni - 1; i += 1) {
    edges.push([i, i + 1]);
  }
  for (let j = 0; j < nj - 1; j += 1) {
    edges.push([j * ni + (ni - 1), (j + 1) * ni + (ni - 1)]);
  }
  for (let i = ni - 1; i > 0; i -= 1) {
    edges.push([(nj - 1) * ni + i, (nj - 1) * ni + (i - 1)]);
  }
  for (let j = nj - 1; j > 0; j -= 1) {
    edges.push([j * ni, (j - 1) * ni]);
  }

  return edges;
}

export function buildDiorama(
  heightmap: Heightmap,
  options: DioramaOptions,
): DioramaGeometry {
  const [ni, nj] = heightmap.shape;
  if (ni < 2 || nj < 2) {
    throw new Error(`diorama needs at least a 2 x 2 heightmap, got ${ni} x ${nj}`);
  }
  if (heightmap.data.length !== ni * nj) {
    throw new Error(
      `heightmap length ${heightmap.data.length} does not match ${ni} x ${nj}`,
    );
  }

  let min = Infinity;
  let max = -Infinity;
  for (const value of heightmap.data) {
    if (!Number.isFinite(value)) continue;
    if (value < min) min = value;
    if (value > max) max = value;
  }
  if (!Number.isFinite(min) || !Number.isFinite(max)) {
    throw new Error("heightmap contains no finite values");
  }
  const range = max - min;
  const normalized = new Float32Array(ni * nj);
  for (let index = 0; index < heightmap.data.length; index += 1) {
    const value = heightmap.data[index]!;
    normalized[index] = range > 1e-12 ? (value - min) / range : 0.5;
  }

  const palette = getPalette(options.palette);
  const width = options.width ?? 1;
  const depth = options.depth ?? 1;
  const baseDepth = options.baseDepth ?? 0.22;
  const baseY = 0;
  const topMin = baseDepth;
  const topMax = baseDepth + options.relief;

  const topPositions = new Float32Array(ni * nj * 3);
  const topColors = new Float32Array(ni * nj * 3);
  const topIndices = new Uint32Array((ni - 1) * (nj - 1) * 6);

  for (let j = 0; j < nj; j += 1) {
    for (let i = 0; i < ni; i += 1) {
      const vertex = j * ni + i;
      const height = normalized[vertex]!;
      const x = (i / (ni - 1) - 0.5) * width;
      const z = (j / (nj - 1) - 0.5) * depth;
      const y = topMin + height * options.relief;

      topPositions[vertex * 3] = x;
      topPositions[vertex * 3 + 1] = y;
      topPositions[vertex * 3 + 2] = z;

      const slope = slopeAt(normalized, ni, nj, i, j);
      const color = topColor(palette, height, slope);
      topColors[vertex * 3] = color[0];
      topColors[vertex * 3 + 1] = color[1];
      topColors[vertex * 3 + 2] = color[2];
    }
  }

  let cursor = 0;
  for (let j = 0; j < nj - 1; j += 1) {
    for (let i = 0; i < ni - 1; i += 1) {
      const a = j * ni + i;
      const b = a + 1;
      const c = a + ni;
      const d = c + 1;
      topIndices[cursor] = a;
      topIndices[cursor + 1] = c;
      topIndices[cursor + 2] = b;
      topIndices[cursor + 3] = b;
      topIndices[cursor + 4] = c;
      topIndices[cursor + 5] = d;
      cursor += 6;
    }
  }

  const edges = perimeter(ni, nj);
  const shellVertexCount = edges.length * (SHELL_ROWS + 1) * 2 + 4;
  const shellPositions = new Float32Array(shellVertexCount * 3);
  const shellColors = new Float32Array(shellVertexCount * 3);
  const shellIndices = new Uint32Array(edges.length * SHELL_ROWS * 6 + 6);

  let vertexCursor = 0;
  let indexCursor = 0;

  const pushVertex = (
    x: number,
    y: number,
    z: number,
    color: readonly [number, number, number],
  ): number => {
    const index = vertexCursor;
    shellPositions[index * 3] = x;
    shellPositions[index * 3 + 1] = y;
    shellPositions[index * 3 + 2] = z;
    shellColors[index * 3] = color[0];
    shellColors[index * 3 + 1] = color[1];
    shellColors[index * 3 + 2] = color[2];
    vertexCursor += 1;
    return index;
  };

  for (const [topA, topB] of edges) {
    const start = vertexCursor;
    const columnA = topA;
    const columnB = topB;
    const xA = topPositions[columnA * 3]!;
    const zA = topPositions[columnA * 3 + 2]!;
    const yA = topPositions[columnA * 3 + 1]!;
    const xB = topPositions[columnB * 3]!;
    const zB = topPositions[columnB * 3 + 2]!;
    const yB = topPositions[columnB * 3 + 1]!;

    for (let row = 0; row <= SHELL_ROWS; row += 1) {
      const t = row / SHELL_ROWS;
      const yLeft = yA + (baseY - yA) * t;
      const yRight = yB + (baseY - yB) * t;
      const leftColor = strataColor(
        palette,
        yLeft,
        baseY,
        topMax,
        yA - yLeft,
      );
      const rightColor = strataColor(
        palette,
        yRight,
        baseY,
        topMax,
        yB - yRight,
      );
      pushVertex(xA, yLeft, zA, leftColor);
      pushVertex(xB, yRight, zB, rightColor);
    }

    for (let row = 0; row < SHELL_ROWS; row += 1) {
      const a = start + row * 2;
      const b = a + 1;
      const c = a + 2;
      const d = a + 3;
      shellIndices[indexCursor] = a;
      shellIndices[indexCursor + 1] = b;
      shellIndices[indexCursor + 2] = c;
      shellIndices[indexCursor + 3] = b;
      shellIndices[indexCursor + 4] = d;
      shellIndices[indexCursor + 5] = c;
      indexCursor += 6;
    }
  }

  const bottomStart = vertexCursor;
  const corners: Array<[number, number]> = [
    [-width / 2, -depth / 2],
    [width / 2, -depth / 2],
    [width / 2, depth / 2],
    [-width / 2, depth / 2],
  ];
  const bottom = bottomColor(palette);
  for (const [x, z] of corners) {
    pushVertex(x, baseY, z, bottom);
  }
  shellIndices[indexCursor] = bottomStart;
  shellIndices[indexCursor + 1] = bottomStart + 1;
  shellIndices[indexCursor + 2] = bottomStart + 2;
  shellIndices[indexCursor + 3] = bottomStart;
  shellIndices[indexCursor + 4] = bottomStart + 2;
  shellIndices[indexCursor + 5] = bottomStart + 3;
  indexCursor += 6;

  const topTriangles = topIndices.length / 3;
  const shellTriangles = shellIndices.length / 3;

  const top: MeshData = {
    positions: topPositions,
    colors: topColors,
    indices: topIndices,
    triangleCount: topTriangles,
  };
  const shell: MeshData = {
    positions: shellPositions.subarray(0, vertexCursor * 3),
    colors: shellColors.subarray(0, vertexCursor * 3),
    indices: shellIndices.subarray(0, indexCursor),
    triangleCount: shellTriangles,
  };

  return {
    top,
    shell,
    baseY,
    topMin,
    topMax,
    width,
    depth,
    totalTriangles: topTriangles + shellTriangles,
  };
}
