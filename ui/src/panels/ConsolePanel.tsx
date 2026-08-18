import { useEffect, useRef } from 'react';
import { clearLogs, useUiState } from '../store';

// 控制台：事件日志（连接状态、错误响应、加载完成等），可清空
export function ConsolePanel() {
  const { logs } = useUiState();
  const boxRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const el = boxRef.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [logs]);

  return (
    <div
      className="panel"
      style={{ display: 'flex', flexDirection: 'column', gap: 6 }}
    >
      <div className="row" style={{ flex: 'none', margin: 0 }}>
        <button onClick={clearLogs}>清空</button>
        <span className="muted">{logs.length} 条</span>
      </div>
      <div className="log-box" ref={boxRef} style={{ flex: 1 }}>
        {logs.map((l, i) => (
          <div key={i}>{l}</div>
        ))}
      </div>
    </div>
  );
}
