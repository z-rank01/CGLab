import { DEBUG_VIEW_NAMES } from '../protocol';
import { actions, useUiState } from '../store';

// 调试视图：debug.set_view；展示值（选中态与"当前"）以 store 中
// 经 telemetry.frame.debug_view 同步后的值为准，点击仅乐观更新
export function DebugViewPanel() {
  const { debugView } = useUiState();
  return (
    <div className="panel">
      <div className="row" role="radiogroup">
        {DEBUG_VIEW_NAMES.map((v) => (
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
