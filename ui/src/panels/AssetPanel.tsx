import { useState } from 'react';
import { actions, useUiState } from '../store';

// 资产：加载（含流式上传进度）、分段耗时、对象 unload
export function AssetPanel() {
  const { scene, loadProgress, lastLoad } = useUiState();
  const [path, setPath] = useState('');

  const load = () => {
    const p = path.trim();
    if (p) actions.loadAsset(p);
  };

  return (
    <div className="panel">
      <div className="row">
        <input
          type="text"
          style={{ flex: 1, minWidth: 200 }}
          placeholder="glTF/GLB 路径（引擎工作目录相对或绝对）"
          value={path}
          onChange={(e) => setPath(e.target.value)}
          onKeyDown={(e) => e.key === 'Enter' && load()}
        />
        <button onClick={load}>加载资产</button>
      </div>

      {loadProgress && (
        <div className="row">
          <span className="muted">
            上传中 {loadProgress.meshes.done}/{loadProgress.meshes.total} meshes (
            {(loadProgress.fraction * 100).toFixed(0)}%)
          </span>
          <div className="bar" style={{ maxWidth: 240 }}>
            <i style={{ width: `${(loadProgress.fraction * 100).toFixed(1)}%` }} />
          </div>
        </div>
      )}

      {lastLoad && (
        <>
          <h3 className="section-title">最近加载：{lastLoad.path}</h3>
          <div className="kv">
            <b>parse</b>
            <span>{(lastLoad.parse_us / 1000).toFixed(1)} ms</span>
            <b>convert</b>
            <span>{(lastLoad.convert_us / 1000).toFixed(1)} ms</span>
            <b>decode</b>
            <span>{(lastLoad.decode_us / 1000).toFixed(1)} ms</span>
            <b>upload</b>
            <span>{(lastLoad.upload_us / 1000).toFixed(1)} ms</span>
            <b>merge</b>
            <span>{(lastLoad.merge_us / 1000).toFixed(1)} ms</span>
            <b>load 总计</b>
            <span>{(lastLoad.load_us / 1000).toFixed(1)} ms</span>
            <b>vertex / index</b>
            <span>
              {(lastLoad.vertex_bytes / 1024).toFixed(0)} /{' '}
              {(lastLoad.index_bytes / 1024).toFixed(0)} KiB
            </span>
            <b>images</b>
            <span>{lastLoad.images}</span>
          </div>
        </>
      )}

      <h3 className="section-title">已加载对象</h3>
      <table>
        <thead>
          <tr>
            <th>ID</th>
            <th>名称</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          {(scene?.objects ?? []).map((o) => (
            <tr key={o.id} style={{ cursor: 'default' }}>
              <td>{o.id}</td>
              <td>{o.name}</td>
              <td>
                <button
                  disabled={o.read_only}
                  title={o.read_only ? '只读对象不可卸载' : 'scene.unload'}
                  onClick={() => actions.unload(o.id)}
                >
                  unload
                </button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
