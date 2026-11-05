export interface MeshData {
  positions: Float32Array;
  colors: Float32Array;
  indices: Uint32Array;
  triangleCount: number;
}

export interface DioramaGeometry {
  top: MeshData;
  shell: MeshData;
  baseY: number;
  topMin: number;
  topMax: number;
  width: number;
  depth: number;
  totalTriangles: number;
}
