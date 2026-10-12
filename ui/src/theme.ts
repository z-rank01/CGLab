// 设计令牌的 JS 取色口（I6 / U1）。
//
// CSS 无法给 canvas 2D 用，`theme.ts` 因此把 `styles.css` 里的 `--chart-*`/`--rg-*`
// 令牌读成实际颜色值，供 FrameGraphPanel（2D canvas）与 RgGraphPanel（SVG 内联
// stroke）使用。**颜色只在 styles.css 定义一次**，这里不写任何颜色字面量。
//
// 生命周期：令牌在 :root 上是静态的，故读一次并缓存；挂载前调用 initTheme()
// （main.tsx），组件内直接用同步的 token() 取得。

export type ThemeToken =
  | '--chart-bg'
  | '--chart-grid'
  | '--chart-axis'
  | '--chart-line'
  | '--chart-fill'
  | '--chart-band'
  | '--rg-raster'
  | '--rg-compute'
  | '--rg-copy'
  | '--rg-cross-queue'
  | '--rg-grid';

const cache = new Map<ThemeToken, string>();

function readToken(name: ThemeToken, fallback: string): string {
  if (typeof window === 'undefined') return fallback;
  const value = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  return value.length > 0 ? value : fallback;
}

// 令牌表。fallback 只在令牌缺失（样式未加载）时兜底，取值口径与 styles.css 一致。
const FALLBACKS: Record<ThemeToken, string> = {
  '--chart-bg': '#12141a',
  '--chart-grid': '#2c313c',
  '--chart-axis': '#8a919e',
  '--chart-line': '#4fa3ff',
  '--chart-fill': 'rgba(79, 163, 255, 0.8)',
  '--chart-band': 'rgba(79, 163, 255, 0.35)',
  '--rg-raster': '#4fa3ff',
  '--rg-compute': '#4caf50',
  '--rg-copy': '#d39e43',
  '--rg-cross-queue': '#d39e43',
  '--rg-grid': '#8a919e',
};

/** 解析并缓存全部令牌（在 React 挂载前调用一次即可，重复调用是安全的）。 */
export function initTheme(): void {
  for (const name of Object.keys(FALLBACKS) as ThemeToken[]) {
    cache.set(name, readToken(name, FALLBACKS[name]));
  }
}

/** 取令牌颜色。未调用 initTheme 时按需读取并缓存。 */
export function token(name: ThemeToken): string {
  const cached = cache.get(name);
  if (cached !== undefined) return cached;
  const value = readToken(name, FALLBACKS[name]);
  cache.set(name, value);
  return value;
}

/** pass 类型 → 颜色（RG 图节点描边与图例同一来源；未知类型落回边框色）。 */
export function passKindColor(kind: string): string {
  switch (kind) {
    case 'raster':
      return token('--rg-raster');
    case 'compute':
      return token('--rg-compute');
    case 'copy':
      return token('--rg-copy');
    default:
      return readToken('--chart-grid', FALLBACKS['--chart-grid']);
  }
}

/** RG 图例条目（与 passKindColor 同源，避免两处色表漂移）。函数而非常量：
 *  模块顶层求值会早于 initTheme()，拿不到样式表里的实际令牌值。 */
export function passKindLegend(): { kind: string; color: string }[] {
  return [
    { kind: 'raster', color: token('--rg-raster') },
    { kind: 'compute', color: token('--rg-compute') },
    { kind: 'copy', color: token('--rg-copy') },
  ];
}
