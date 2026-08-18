// M8/B3：RG 事件浏览器 —— 已编译 render graph 的 DAG 视图（手写 SVG，零新依赖）。
// 数据源：store.rg（telemetry.rg 推送 + 连接后 rg.get_dump 拉取）；recompile 自动刷新。
import { useMemo, useState } from 'react';
import type { RgBarrier, RgDump } from '../protocol';
import { useUiState } from '../store';

const NODE_W = 132;
const NODE_H = 40;
const GAP_X = 56;
const GAP_Y = 18;
const PAD = 16;

// pass 类型配色（raster/compute/copy），未知类型落回边框色
const KIND_COLORS: Record<string, string> = {
  raster: '#4fa3ff',
  compute: '#4caf50',
  copy: '#d39e43',
};

interface GraphLayout {
  x: number[];
  y: number[];
  width: number;
  height: number;
}

// 最长路分层：依赖边 from→to 总是 schedule 前向（拓扑序），按序 DP 即可；
// 防御：回边不参与分层（仍照常绘制）。
function layoutGraph(dump: RgDump): GraphLayout {
  const n = dump.passes.length;
  const layer = new Array<number>(n).fill(0);
  for (let to = 0; to < n; to++) {
    for (const e of dump.edges) {
      if (e.to === to && e.from < to) layer[to] = Math.max(layer[to], layer[e.from] + 1);
    }
  }
  const slot = new Array<number>(n).fill(0);
  const layerSizes = new Map<number, number>();
  let maxLayer = 0;
  for (let i = 0; i < n; i++) {
    const size = layerSizes.get(layer[i]) ?? 0;
    slot[i] = size;
    layerSizes.set(layer[i], size + 1);
    maxLayer = Math.max(maxLayer, layer[i]);
  }
  const maxSlot = Math.max(0, ...layerSizes.values());
  return {
    x: layer.map((l) => PAD + l * (NODE_W + GAP_X)),
    y: slot.map((s) => PAD + s * (NODE_H + GAP_Y)),
    width: PAD * 2 + (maxLayer + 1) * NODE_W + maxLayer * GAP_X,
    height: PAD * 2 + maxSlot * NODE_H + Math.max(0, maxSlot - 1) * GAP_Y,
  };
}

function edgePath(x1: number, y1: number, x2: number, y2: number): string {
  const mid = (x1 + x2) / 2;
  return `M ${x1} ${y1} C ${mid} ${y1}, ${mid} ${y2}, ${x2} ${y2}`;
}

function BarrierRow({ barrier }: { barrier: RgBarrier }) {
  return (
    <tr>
      <td>{barrier.resource}</td>
      <td>{barrier.phase}</td>
      <td>{barrier.intents.join('|')}</td>
      <td>
        {barrier.before.usage.join('|') || '∅'} → {barrier.after.usage.join('|') || '∅'}
        <span className="muted">
          {' '}
          ({barrier.before.access} → {barrier.after.access})
        </span>
      </td>
    </tr>
  );
}

// 已编译 render graph DAG（M8/B3 事件浏览器本体）
export function RgGraphPanel() {
  const { rg } = useUiState();
  const [selected, setSelected] = useState<number | null>(null);
  const layout = useMemo(() => (rg ? layoutGraph(rg.dump) : null), [rg]);

  if (!rg) return <div className="panel muted">等待 RG 遥测…</div>;
  const { dump, revision } = rg;
  if (dump.passes.length === 0 || !layout) {
    return <div className="panel muted">尚无已编译图（首帧渲染后推送）</div>;
  }

  const sel = selected !== null && selected < dump.passes.length ? dump.passes[selected] : null;
  const selBarriers = sel
    ? dump.barriers.filter((b) => b.pass === sel.index || b.producer === sel.index)
    : [];
  const selAliases = sel ? dump.aliases.filter((a) => a.at_pass === sel.index) : [];

  return (
    <div className="panel">
      <div className="row muted">
        <span>
          revision {revision} · {dump.statistics.active_pass_count ?? dump.passes.length}/
          {dump.statistics.pass_count ?? dump.passes.length} pass · {dump.edges.length} 边 ·{' '}
          {dump.barriers.length} barrier
          {(dump.statistics.culled_pass_count ?? 0) > 0 &&
            ` · 剔除 ${dump.statistics.culled_pass_count}`}
        </span>
        <span style={{ flex: 1 }} />
        {Object.entries(KIND_COLORS).map(([kind, color]) => (
          <span key={kind}>
            <i className="rg-chip" style={{ background: color }} /> {kind}
          </span>
        ))}
        <span>
          <i className="rg-chip rg-chip-dashed" /> 跨队列
        </span>
      </div>
      <svg
        width={layout.width}
        height={layout.height}
        className="rg-svg"
        onClick={() => setSelected(null)}
      >
        {dump.edges.map((e, i) => (
          <path
            key={`e${i}`}
            d={edgePath(
              layout.x[e.from] + NODE_W,
              layout.y[e.from] + NODE_H / 2,
              layout.x[e.to],
              layout.y[e.to] + NODE_H / 2,
            )}
            className={e.kind === 'cross_queue' ? 'rg-edge rg-edge-queue' : 'rg-edge'}
          />
        ))}
        {dump.passes.map((p) => (
          <g
            key={p.index}
            transform={`translate(${layout.x[p.index]}, ${layout.y[p.index]})`}
            onClick={(ev) => {
              ev.stopPropagation();
              setSelected(p.index === selected ? null : p.index);
            }}
            style={{ cursor: 'pointer' }}
          >
            <rect
              width={NODE_W}
              height={NODE_H}
              rx={6}
              className="rg-node"
              style={{
                stroke: KIND_COLORS[p.kind] ?? 'var(--border)',
                strokeWidth: selected === p.index ? 2.5 : 1.5,
              }}
            />
            <text x={8} y={17} className="rg-node-name">
              {p.name}
            </text>
            <text x={8} y={32} className="rg-node-kind">
              {p.kind}
              {p.queue !== 'graphics' ? ` · ${p.queue}` : ''}
              {p.flags.length > 0 ? ` · ${p.flags.join('|')}` : ''}
            </text>
          </g>
        ))}
      </svg>
      {sel && (
        <div>
          <div className="section-title">
            pass #{sel.index} {sel.name}
          </div>
          <div className="kv">
            <b>类型</b>
            <span>
              {sel.kind} / {sel.queue}
              {sel.flags.length > 0 ? ` / ${sel.flags.join('|')}` : ''}
            </span>
            <b>颜色附件</b>
            <span>
              {sel.colors.length === 0
                ? '—'
                : sel.colors.map((c) => `${c.resource} (${c.load}/${c.store})`).join('，')}
            </span>
            <b>深度附件</b>
            <span>{sel.depth ? `${sel.depth.resource} (${sel.depth.load}/${sel.depth.store})` : '—'}</span>
          </div>
          {selBarriers.length > 0 && (
            <table>
              <thead>
                <tr>
                  <th>资源</th>
                  <th>相位</th>
                  <th>意图</th>
                  <th>状态迁移</th>
                </tr>
              </thead>
              <tbody>
                {selBarriers.map((b, i) => (
                  <BarrierRow key={i} barrier={b} />
                ))}
              </tbody>
            </table>
          )}
          {selAliases.length > 0 && (
            <div className="row muted">
              别名：
              {selAliases
                .map((a) => `${a.previous} → ${a.next}（block ${a.memory_block ?? '?'}）`)
                .join('，')}
            </div>
          )}
        </div>
      )}
    </div>
  );
}
