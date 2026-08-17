import { useEffect, useState } from 'react';
import { actions, logEvent, useUiState } from '../store';
import { fmtVec } from './common';

const PARAM_FIELDS = [
  'fov',
  'movement_speed',
  'mouse_sensitivity',
  'zoom_speed',
  'near_plane',
  'far_plane',
] as const;
type ParamField = (typeof PARAM_FIELDS)[number];

// 相机面板：模式/culling/参数编辑/bookmark/实时状态
export function CameraPanel() {
  const { frame } = useUiState();
  const cam = frame?.camera;

  const [params, setParams] = useState<Record<ParamField, string> | null>(null);

  // 相机参数首次到达时初始化表单（之后不跟随遥测覆盖用户编辑）
  useEffect(() => {
    if (cam && !params) {
      setParams({
        fov: String(cam.fov),
        movement_speed: String(cam.movement_speed),
        mouse_sensitivity: String(cam.mouse_sensitivity),
        zoom_speed: String(cam.zoom_speed),
        near_plane: String(cam.near_plane),
        far_plane: String(cam.far_plane),
      });
    }
  }, [cam, params]);

  const applyParams = () => {
    if (!params) return;
    const out: Record<string, number> = {};
    for (const k of PARAM_FIELDS) {
      const n = Number(params[k]);
      if (!Number.isFinite(n)) {
        logEvent(`camera.set_params: ${k} 非法数值`);
        return;
      }
      out[k] = n;
    }
    actions.setCameraParams(out);
  };

  return (
    <div className="panel">
      <div className="row">
        <button
          className={cam?.mode === 'fly' ? 'active' : ''}
          onClick={() => actions.setCameraMode('fly')}
        >
          Fly
        </button>
        <button
          className={cam?.mode === 'orbit' ? 'active' : ''}
          onClick={() => actions.setCameraMode('orbit')}
        >
          Orbit
        </button>
        <label>
          <input
            type="checkbox"
            checked={cam?.culling ?? false}
            onChange={(e) => actions.setCulling(e.target.checked)}
          />{' '}
          culling
        </label>
      </div>

      <h3 className="section-title">参数</h3>
      {params ? (
        <>
          {PARAM_FIELDS.map((k) => (
            <div className="row" key={k}>
              <b className="muted" style={{ width: 130, fontWeight: 400 }}>
                {k}
              </b>
              <input
                type="number"
                step="any"
                value={params[k]}
                onChange={(e) => setParams({ ...params, [k]: e.target.value })}
              />
            </div>
          ))}
          <div className="row">
            <button onClick={applyParams}>应用参数</button>
          </div>
        </>
      ) : (
        <div className="muted">等待相机状态…</div>
      )}

      <h3 className="section-title">书签</h3>
      <div className="row">
        {(cam?.bookmarks_valid ?? Array<boolean>(8).fill(false)).map((valid, i) => (
          <span key={i} style={{ display: 'inline-flex', gap: 2 }}>
            <button
              title={`跳转 slot ${i}${valid ? '' : '（空）'}`}
              disabled={!valid}
              onClick={() => actions.gotoBookmark(i)}
            >
              {i}
            </button>
            <button
              title={`保存到 slot ${i}`}
              className="muted"
              onClick={() => actions.saveBookmark(i)}
            >
              S
            </button>
          </span>
        ))}
      </div>

      <h3 className="section-title">状态</h3>
      <div className="kv">
        <b>position</b>
        <span>{fmtVec(cam?.position)}</span>
        <b>yaw / pitch</b>
        <span>
          {cam ? `${cam.yaw.toFixed(1)} / ${cam.pitch.toFixed(1)}` : '-'}
        </span>
        <b>focus</b>
        <span>{fmtVec(cam?.focus_point)}</span>
        <b>orbit_distance</b>
        <span>{cam?.orbit_distance.toFixed(2) ?? '-'}</span>
        <b>blending</b>
        <span>{cam ? (cam.blending ? 'yes' : 'no') : '-'}</span>
      </div>
    </div>
  );
}
