import { useCallback } from 'react';
import {
  DockviewReact,
  DockviewReadyEvent,
  IDockviewPanelProps,
  themeDark,
} from 'dockview-react';
import type { AddPanelPositionOptions } from 'dockview-core';
import { useUiState } from './store';
import { FramePanel } from './panels/FramePanel';
import { FrameGraphPanel } from './panels/FrameGraphPanel';
import { PhasePanel } from './panels/PhasePanel';
import { CountersPanel } from './panels/CountersPanel';
import { SceneTreePanel } from './panels/SceneTreePanel';
import { InspectorPanel } from './panels/InspectorPanel';
import { CameraPanel } from './panels/CameraPanel';
import { AssetPanel } from './panels/AssetPanel';
import { DebugViewPanel } from './panels/DebugViewPanel';
import { ConsolePanel } from './panels/ConsolePanel';
import { RgGraphPanel } from './panels/RgGraphPanel';

// v3（I6/U1）：设计令牌 + 默认布局重配（控制窄栏 + 数据主区）。
// 布局缓存版本 bump，避免旧缓存盖住新默认布局。
const LAYOUT_KEY = 'cglab.ui.layout.v3';

// 默认布局尺寸（dockview 的 initialWidth 只接受 number，无法直接消费 CSS 令牌，
// 故在这里作为权威值；styles.css 的 --sidebar-w 与本值同口径，仅作样式侧参考）。
const SIDEBAR_W = 264;
const RIGHT_COL_W = 380;
const MAIN_COL_W = 620;

const PANEL_DEFS: Record<
  string,
  { component: React.FunctionComponent<IDockviewPanelProps>; title: string }
> = {
  frame: { component: FramePanel, title: '帧控制' },
  graph: { component: FrameGraphPanel, title: '帧时曲线' },
  phases: { component: PhasePanel, title: '阶段耗时' },
  counters: { component: CountersPanel, title: '帧计数' },
  scene: { component: SceneTreePanel, title: '场景树' },
  inspector: { component: InspectorPanel, title: '检视器' },
  camera: { component: CameraPanel, title: '相机' },
  assets: { component: AssetPanel, title: '资产' },
  debugView: { component: DebugViewPanel, title: '调试视图' },
  rg: { component: RgGraphPanel, title: 'RG 图' },
  console: { component: ConsolePanel, title: '控制台' },
};

const components: Record<string, React.FunctionComponent<IDockviewPanelProps>> =
  Object.fromEntries(Object.entries(PANEL_DEFS).map(([k, v]) => [k, v.component]));

// 默认布局（I6/U1）：控制窄栏 + 数据主区 + 场景/资产收纳列。
//
// 左：帧控制 / 调试视图 / 帧计数（tab 组，内容单薄 → 窄栏）
// 中：帧时曲线 / RG 图 / 阶段耗时 / 控制台（数据密集 → 主区）
// 右：场景树 / 检视器 / 相机 / 资产
function buildDefaultLayout(event: DockviewReadyEvent): void {
  const api = event.api;
  const add = (id: keyof typeof PANEL_DEFS, position?: AddPanelPositionOptions) =>
    api.addPanel({ id, component: id, title: PANEL_DEFS[id].title, position });

  // 控制窄栏（三 tab 一组）
  api.addPanel({
    id: 'frame',
    component: 'frame',
    title: PANEL_DEFS.frame.title,
    initialWidth: SIDEBAR_W,
  });
  add('debugView', { referencePanel: 'frame', direction: 'within' });
  add('counters', { referencePanel: 'frame', direction: 'within' });

  // 数据主区：帧时曲线（上）→ RG 图 / 控制台（下）
  api.addPanel({
    id: 'graph',
    component: 'graph',
    title: PANEL_DEFS.graph.title,
    position: { referencePanel: 'frame', direction: 'right' },
    initialWidth: MAIN_COL_W,
  });
  add('rg', { referencePanel: 'graph', direction: 'below' });
  add('console', { referencePanel: 'rg', direction: 'below' });
  add('phases', { referencePanel: 'rg', direction: 'within' });

  // 场景/资产收纳列
  api.addPanel({
    id: 'scene',
    component: 'scene',
    title: PANEL_DEFS.scene.title,
    position: { referencePanel: 'graph', direction: 'right' },
    initialWidth: RIGHT_COL_W,
  });
  add('inspector', { referencePanel: 'scene', direction: 'below' });
  add('camera', { referencePanel: 'inspector', direction: 'within' });
  add('assets', { referencePanel: 'inspector', direction: 'within' });

  api.getPanel('graph')?.api.setActive();
}

export default function App() {
  const { connected, server, frame } = useUiState();

  const onReady = useCallback((event: DockviewReadyEvent) => {
    const api = event.api;
    let restored = false;
    const saved = localStorage.getItem(LAYOUT_KEY);
    if (saved) {
      try {
        api.fromJSON(JSON.parse(saved));
        restored = api.panels.length > 0;
      } catch {
        api.clear();
      }
    }
    if (!restored) buildDefaultLayout(event);

    let timer: ReturnType<typeof setTimeout> | null = null;
    api.onDidLayoutChange(() => {
      if (timer !== null) clearTimeout(timer);
      timer = setTimeout(() => {
        try {
          localStorage.setItem(LAYOUT_KEY, JSON.stringify(api.toJSON()));
        } catch {
          /* 忽略持久化失败 */
        }
      }, 200);
    });
  }, []);

  return (
    <div className="app">
      <header className="app-header">
        <span className={`dot ${connected ? 'on' : 'off'}`} />
        <h1>CGLab Control Panel</h1>
        <span className="muted">{server ?? (connected ? '握手等待…' : '未连接')}</span>
        <span className="fill" />
        <span className="numeric">
          {frame ? `${frame.fps.toFixed(1)} fps · ${frame.frame_time_ms.toFixed(2)} ms` : ''}
        </span>
      </header>
      <div className="dock-host">
        <DockviewReact
          className="dockview-theme-dark"
          theme={themeDark}
          components={components}
          onReady={onReady}
        />
      </div>
    </div>
  );
}
