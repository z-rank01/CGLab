import { useUiState } from '../store';

// 阶段耗时：phase_us 横向条形图（相对最大值归一）
export function PhasePanel() {
  const { frame } = useUiState();
  const phases = frame?.phase_us;
  if (!phases) return <div className="panel muted">等待遥测…</div>;
  const entries = Object.entries(phases);
  const max = Math.max(1, ...entries.map(([, v]) => v));
  return (
    <div className="panel">
      <h3 className="section-title">阶段耗时（p50 µs）</h3>
      {entries.map(([k, v]) => (
        <div className="row phase-row" key={k}>
          <b className="muted phase-name" title={k}>
            {k}
          </b>
          <span className="tick">{v.toFixed(0)}</span>
          <div className="bar">
            <i style={{ width: `${((100 * v) / max).toFixed(1)}%` }} />
          </div>
        </div>
      ))}
    </div>
  );
}
