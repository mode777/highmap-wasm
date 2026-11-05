export type PaletteId = "temperate" | "arid" | "volcanic" | "arctic";

export type Rgb = [number, number, number];

export interface Palette {
  id: PaletteId;
  label: string;
  top: readonly { at: number; rgb: Rgb }[];
  rock: readonly Rgb[];
  slope: Rgb;
  topsoil: Rgb;
  bottom: Rgb;
  topsoilThickness: number;
}

function srgbToLinear(channel: number): number {
  return channel <= 0.04045
    ? channel / 12.92
    : ((channel + 0.055) / 1.055) ** 2.4;
}

function hexToLinear(hex: string): Rgb {
  const value = hex.replace("#", "");
  const r = Number.parseInt(value.slice(0, 2), 16) / 255;
  const g = Number.parseInt(value.slice(2, 4), 16) / 255;
  const b = Number.parseInt(value.slice(4, 6), 16) / 255;
  return [srgbToLinear(r), srgbToLinear(g), srgbToLinear(b)];
}

function ramp(colors: readonly string[]): readonly { at: number; rgb: Rgb }[] {
  return colors.map((color, index) => ({
    at: colors.length === 1 ? 0 : index / (colors.length - 1),
    rgb: hexToLinear(color),
  }));
}

function palette(
  id: PaletteId,
  label: string,
  top: readonly string[],
  rock: readonly string[],
  slope: string,
  topsoil: string,
  bottom: string,
): Palette {
  return {
    id,
    label,
    top: ramp(top),
    rock: ramp(rock).map((entry) => entry.rgb),
    slope: hexToLinear(slope),
    topsoil: hexToLinear(topsoil),
    bottom: hexToLinear(bottom),
    topsoilThickness: 0.03,
  };
}

const PALETTES: Record<PaletteId, Palette> = {
  temperate: palette(
    "temperate",
    "Temperate",
    ["#3f5a3a", "#5a6f45", "#728a52", "#8f9a68", "#a89a7c", "#bdb3a1", "#efebe4"],
    ["#2b241e", "#6b4a32", "#a8754a", "#5a4636", "#c29a68"],
    "#7a7064",
    "#3f2e21",
    "#241f1a",
  ),
  arid: palette(
    "arid",
    "Arid",
    ["#8f6b45", "#b98a55", "#d0a26b", "#dfbd8d", "#f0e0c4"],
    ["#2f2118", "#7a4a2e", "#c08a52", "#8a5a3a", "#e0b478"],
    "#9a6b45",
    "#4f3523",
    "#2f2118",
  ),
  volcanic: palette(
    "volcanic",
    "Volcanic",
    ["#2f2b28", "#4a3e35", "#6b5543", "#8a6a4a", "#b89a6a"],
    ["#151311", "#4a3a2e", "#7a5a40", "#3a3028", "#9a6a45"],
    "#4a403a",
    "#241d18",
    "#151311",
  ),
  arctic: palette(
    "arctic",
    "Arctic",
    ["#4a5f6a", "#6f8288", "#9aa8ab", "#c8d0d0", "#f4f6f5"],
    ["#202629", "#4e585c", "#8b969a", "#5a666a", "#c0cacc"],
    "#6f7a7e",
    "#384246",
    "#202629",
  ),
};

export function getPalette(id: PaletteId): Palette {
  return PALETTES[id];
}

export function clamp01(value: number): number {
  return Math.min(1, Math.max(0, value));
}

export function smoothstep(edge0: number, edge1: number, x: number): number {
  const t = clamp01((x - edge0) / Math.max(1e-8, edge1 - edge0));
  return t * t * (3 - 2 * t);
}

function mix(a: Rgb, b: Rgb, t: number): Rgb {
  return [
    a[0] + (b[0] - a[0]) * t,
    a[1] + (b[1] - a[1]) * t,
    a[2] + (b[2] - a[2]) * t,
  ];
}

function sampleRamp(
  stops: readonly { at: number; rgb: Rgb }[],
  t: number,
): Rgb {
  if (stops.length === 0) return [0, 0, 0];
  const first = stops[0]!;
  const last = stops[stops.length - 1]!;
  const value = clamp01(t);
  if (value <= first.at) return [...first.rgb];
  if (value >= last.at) return [...last.rgb];

  for (let i = 0; i < stops.length - 1; i += 1) {
    const left = stops[i]!;
    const right = stops[i + 1]!;
    if (value >= left.at && value <= right.at) {
      const local = (value - left.at) / Math.max(1e-8, right.at - left.at);
      return mix(left.rgb, right.rgb, local);
    }
  }
  return [...last.rgb];
}

/** Terrain-top color from normalized height and world-space slope. */
export function topColor(
  config: Palette,
  height: number,
  slope: number,
): Rgb {
  const base = sampleRamp(config.top, height);
  const rockMix = smoothstep(1.4, 3.8, slope) * 0.55;
  return mix(base, config.slope, rockMix);
}

/** Cut-face color: topsoil near the surface, stepped rock bands below. */
export function strataColor(
  config: Palette,
  absoluteY: number,
  baseY: number,
  topY: number,
  depthBelowTop: number,
): Rgb {
  if (depthBelowTop <= config.topsoilThickness) {
    return [...config.topsoil];
  }

  const t = clamp01((absoluteY - baseY) / Math.max(1e-6, topY - baseY));
  const bands = config.rock.length;
  const stepped = bands <= 1 ? 0.5 : Math.round(t * (bands - 1)) / (bands - 1);
  const index = Math.min(
    config.rock.length - 1,
    Math.max(0, Math.round(stepped * (config.rock.length - 1))),
  );
  return [...config.rock[index]!];
}

export function bottomColor(config: Palette): Rgb {
  return [...config.bottom];
}
