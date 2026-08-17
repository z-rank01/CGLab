import { DebugView } from '../protocol';
import { actions, useUiState } from '../store';

const VIEWS: DebugView[] = ['off', 'shadow', 'depth', 'hdr', 'resolved'];

// 调试视图：debug.set_view，本地跟踪当前值
export function DebugViewPanel() {
  const { debugView } = useUiState();
  return (
    <div className="panel">
      <div className="row" role="radiogroup">
        {VIEWS.map((v) => (
          <label key={v}>
            <input
              type="radio"
              name="debug-view"
              checked={debugView === v}
              onChange={() => actions.setDebugView(v)}
            />{' '}
            {v}
          </label>
        ))}
      </div>
      <div className="muted">当前：{debugView}</div>
    </div>
  );
}
