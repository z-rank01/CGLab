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
    <div className="panel col">
      <div className="row row-flush row-none">
        <button onClick={clearLogs}>清空</button>
        <span className="muted">{logs.length} 条</span>
      </div>
      <div className="log-box fill" ref={boxRef}>
        {logs.map((l, i) => (
          <div key={i}>{l}</div>
        ))}
      </div>
    </div>
  );
}
