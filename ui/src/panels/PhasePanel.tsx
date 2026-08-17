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
        <div className="row" key={k} style={{ margin: '2px 0' }}>
          <b
            className="muted"
            style={{
              width: 180,
              overflow: 'hidden',
              textOverflow: 'ellipsis',
              fontWeight: 400,
              flex: 'none',
            }}
            title={k}
          >
            {k}
          </b>
          <span style={{ width: 64, textAlign: 'right', flex: 'none' }}>{v.toFixed(0)}</span>
          <div className="bar">
            <i style={{ width: `${((100 * v) / max).toFixed(1)}%` }} />
          </div>
        </div>
      ))}
    </div>
  );
}
