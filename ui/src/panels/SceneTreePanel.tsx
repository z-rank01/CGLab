import { actions, useUiState } from '../store';

// 场景树：对象表，点击行 scene.select（再点已选中行取消选择），选中行高亮
export function SceneTreePanel() {
  const { scene } = useUiState();
  if (!scene) return <div className="panel muted">等待场景遥测…</div>;
  return (
    <div className="panel">
      <table>
        <thead>
          <tr>
            <th>ID</th>
            <th>名称</th>
            <th>可见</th>
            <th>只读</th>
            <th>Draws</th>
          </tr>
        </thead>
        <tbody>
          {scene.objects.map((o) => (
            <tr
              key={o.id}
              className={o.id === scene.selected ? 'sel' : ''}
              onClick={() => actions.select(o.id === scene.selected ? null : o.id)}
            >
              <td>{o.id}</td>
              <td>{o.name}</td>
              <td onClick={(e) => e.stopPropagation()}>
                <input
                  type="checkbox"
                  checked={o.visible}
                  onChange={(e) => actions.setVisibility(o.id, e.target.checked)}
                />
              </td>
              <td>{o.read_only ? 'yes' : ''}</td>
              <td>{o.draw_count}</td>
            </tr>
          ))}
        </tbody>
      </table>
      {scene.objects.length === 0 && <div className="muted">场景为空</div>}
    </div>
  );
}
