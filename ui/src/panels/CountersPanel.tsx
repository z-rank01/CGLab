import { useUiState } from '../store';

const COUNTER_KEYS = [
  'instances',
  'visible',
  'culled',
  'draws',
  'shadow_draws',
  'main_draws',
  'debug_draws',
  'resolve_draws',
  'buffer_uploads',
  'image_uploads',
] as const;

// 帧计数表
export function CountersPanel() {
  const { frame } = useUiState();
  const c = frame?.counters;
  return (
    <div className="panel">
      <table>
        <thead>
          <tr>
            <th>计数器</th>
            <th>值</th>
          </tr>
        </thead>
        <tbody>
          {COUNTER_KEYS.map((k) => (
            <tr key={k} style={{ cursor: 'default' }}>
              <td className="muted">{k}</td>
              <td>{c ? c[k] : '-'}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
