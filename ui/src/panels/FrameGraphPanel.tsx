import { useEffect, useRef, useState } from 'react';
import { useUiState } from '../store';
import { initTheme, token } from '../theme';

type Metric = 'fps' | 'frame_time_ms';

// 帧时曲线：手写 canvas，ring buffer（store.samples，最近 300 个样本）
export function FrameGraphPanel() {
  const { samples, frame } = useUiState();
  const [metric, setMetric] = useState<Metric>('frame_time_ms');
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    if (w === 0 || h === 0) return;
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.scale(dpr, dpr);
    initTheme(); // 幂等；保证画布取色走令牌表

    ctx.fillStyle = token('--chart-bg');
    ctx.fillRect(0, 0, w, h);

    const pad = { l: 44, r: 8, t: 8, b: 18 };
    const plotW = w - pad.l - pad.r;
    const plotH = h - pad.t - pad.b;
    if (plotW <= 0 || plotH <= 0 || samples.length === 0) return;

    const values = samples.map((s) => s[metric]);
    const refs: { label: string; value: number }[] =
      metric === 'frame_time_ms' && frame
        ? [
            { label: 'p50', value: frame.quantiles.frame_p50_ms },
            { label: 'p95', value: frame.quantiles.frame_p95_ms },
            { label: 'p99', value: frame.quantiles.frame_p99_ms },
          ]
        : [];
    const max = Math.max(1e-6, ...values, ...refs.map((r) => r.value)) * 1.1;

    const x = (i: number) => pad.l + (i / Math.max(1, values.length - 1)) * plotW;
    const y = (v: number) => pad.t + plotH - (v / max) * plotH;

    // 网格与纵轴刻度
    ctx.strokeStyle = token('--chart-grid');
    ctx.fillStyle = token('--chart-axis');
    ctx.font = '10px Consolas, monospace';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (let i = 0; i <= 4; i++) {
      const v = (max / 4) * i;
      const yy = y(v);
      ctx.beginPath();
      ctx.moveTo(pad.l, yy);
      ctx.lineTo(w - pad.r, yy);
      ctx.stroke();
      ctx.fillText(v.toFixed(1), pad.l - 4, yy);
    }

    // p50/p95/p99 参考线
    ctx.textAlign = 'left';
    for (const r of refs) {
      const yy = y(r.value);
      ctx.strokeStyle = token('--chart-band');
      ctx.setLineDash([4, 3]);
      ctx.beginPath();
      ctx.moveTo(pad.l, yy);
      ctx.lineTo(w - pad.r, yy);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = token('--chart-fill');
      ctx.fillText(`${r.label} ${r.value.toFixed(1)}`, pad.l + 4, yy - 6);
    }

    // 折线
    ctx.strokeStyle = token('--chart-line');
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    values.forEach((v, i) => {
      if (i === 0) ctx.moveTo(x(i), y(v));
      else ctx.lineTo(x(i), y(v));
    });
    ctx.stroke();
  }, [samples, metric, frame]);

  const latest = samples.length > 0 ? samples[samples.length - 1] : null;
  return (
    <div className="panel col">
      <div className="row row-flush row-none">
        <button
          className={metric === 'frame_time_ms' ? 'active' : ''}
          onClick={() => setMetric('frame_time_ms')}
        >
          frame_time
        </button>
        <button
          className={metric === 'fps' ? 'active' : ''}
          onClick={() => setMetric('fps')}
        >
          fps
        </button>
        <span className="muted">
          {latest
            ? metric === 'fps'
              ? `${latest.fps.toFixed(1)} fps`
              : `${latest.frame_time_ms.toFixed(2)} ms`
            : '等待遥测…'}
        </span>
      </div>
      <canvas ref={canvasRef} className="fill" style={{ minHeight: 0, width: '100%' }} />
    </div>
  );
}
