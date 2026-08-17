import { actions, useUiState } from '../store';

// 帧控制：Pause/Resume/Step + paused 状态 + 分位数
export function FramePanel() {
  const { frame } = useUiState();
  const q = frame?.quantiles;
  return (
    <div className="panel">
      <div className="row">
        <button onClick={actions.pause}>Pause</button>
        <button onClick={actions.resume}>Resume</button>
        <button onClick={() => actions.step(1)}>Step ×1</button>
        <button onClick={() => actions.step(8)}>Step ×8</button>
        <span className="muted">{frame?.paused ? '已暂停' : '运行中'}</span>
      </div>
      <h3 className="section-title">分位数</h3>
      <div className="kv">
        <b>p50</b>
        <span>{q ? q.frame_p50_ms.toFixed(2) : '-'} ms</span>
        <b>p95</b>
        <span>{q ? q.frame_p95_ms.toFixed(2) : '-'} ms</span>
        <b>p99</b>
        <span>{q ? q.frame_p99_ms.toFixed(2) : '-'} ms</span>
        <b>validation</b>
        <span>{frame?.validation_errors ?? '-'}</span>
      </div>
    </div>
  );
}
