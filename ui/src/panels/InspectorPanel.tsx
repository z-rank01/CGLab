import { useEffect, useState } from 'react';
import { actions, logEvent, useUiState } from '../store';
import { parseVec3, Vec3Input } from './common';

interface Vec3Form {
  position: string[];
  rotation: string[];
  scale: string[];
}

// 检视器：选中对象 transform 编辑 + bounds 显示；read_only 禁用编辑
export function InspectorPanel() {
  const { scene } = useUiState();
  const selected = scene?.objects.find((o) => o.id === scene.selected) ?? null;

  const [form, setForm] = useState<Vec3Form | null>(null);

  // 选中对象或其 transform 变化时重置表单
  const key = selected
    ? `${selected.id}:${JSON.stringify(selected.transform)}`
    : '';
  useEffect(() => {
    if (!selected) {
      setForm(null);
      return;
    }
    setForm({
      position: selected.transform.position.map(String),
      rotation: selected.transform.rotation.map(String),
      scale: selected.transform.scale.map(String),
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key]);

  if (!selected || !form) {
    return <div className="panel muted">未选中对象（在场景树中点击行）</div>;
  }

  const setField =
    (field: keyof Vec3Form) => (index: number, value: string) => {
      setForm((f) => {
        if (!f) return f;
        const next = { ...f, [field]: [...f[field]] };
        next[field][index] = value;
        return next;
      });
    };

  const submit = () => {
    const position = parseVec3(form.position);
    const rotation = parseVec3(form.rotation);
    const scale = parseVec3(form.scale);
    if (!position || !rotation || !scale) {
      logEvent('set_transform: 输入包含非法数值');
      return;
    }
    actions.setTransform(selected.id, { position, rotation, scale });
  };

  const ro = selected.read_only;
  return (
    <div className="panel">
      <h3 className="section-title">
        #{selected.id} {selected.name}
        {ro ? '（只读）' : ''}
      </h3>
      <div className="row">
        <b className="muted" style={{ width: 64 }}>
          position
        </b>
        <Vec3Input values={form.position} onChange={setField('position')} disabled={ro} />
      </div>
      <div className="row">
        <b className="muted" style={{ width: 64 }}>
          rotation
        </b>
        <Vec3Input values={form.rotation} onChange={setField('rotation')} disabled={ro} />
      </div>
      <div className="row">
        <b className="muted" style={{ width: 64 }}>
          scale
        </b>
        <Vec3Input values={form.scale} onChange={setField('scale')} disabled={ro} />
      </div>
      <div className="row">
        <button onClick={submit} disabled={ro}>
          应用 transform
        </button>
      </div>
      <h3 className="section-title">bounds</h3>
      <div className="kv">
        <b>min</b>
        <span>{selected.bounds.min.map((v) => (+v).toFixed(3)).join(', ')}</span>
        <b>max</b>
        <span>{selected.bounds.max.map((v) => (+v).toFixed(3)).join(', ')}</span>
      </div>
    </div>
  );
}
