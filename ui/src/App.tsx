import { useCallback } from 'react';
import {
  DockviewReact,
  DockviewReadyEvent,
  IDockviewPanelProps,
  themeDark,
} from 'dockview-react';
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

// v2：M8/B3 新增 RG 图面板，旧缓存布局不含该面板，整体重置一次
const LAYOUT_KEY = 'cglab.ui.layout.v2';

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

function buildDefaultLayout(event: DockviewReadyEvent): void {
  const api = event.api;
  api.addPanel({ id: 'frame', component: 'frame', title: PANEL_DEFS.frame.title });
  api.addPanel({
    id: 'debugView',
    component: 'debugView',
    title: PANEL_DEFS.debugView.title,
    position: { referencePanel: 'frame', direction: 'within' },
  });
  api.addPanel({
    id: 'counters',
    component: 'counters',
    title: PANEL_DEFS.counters.title,
    position: { referencePanel: 'frame', direction: 'within' },
  });
  api.addPanel({
    id: 'graph',
    component: 'graph',
    title: PANEL_DEFS.graph.title,
    position: { referencePanel: 'frame', direction: 'right' },
    initialWidth: 560,
  });
  api.addPanel({
    id: 'rg',
    component: 'rg',
    title: PANEL_DEFS.rg.title,
    position: { referencePanel: 'graph', direction: 'within' },
  });
  api.addPanel({
    id: 'phases',
    component: 'phases',
    title: PANEL_DEFS.phases.title,
    position: { referencePanel: 'graph', direction: 'below' },
  });
  api.addPanel({
    id: 'console',
    component: 'console',
    title: PANEL_DEFS.console.title,
    position: { referencePanel: 'phases', direction: 'below' },
  });
  api.addPanel({
    id: 'scene',
    component: 'scene',
    title: PANEL_DEFS.scene.title,
    position: { referencePanel: 'graph', direction: 'right' },
    initialWidth: 380,
  });
  api.addPanel({
    id: 'inspector',
    component: 'inspector',
    title: PANEL_DEFS.inspector.title,
    position: { referencePanel: 'scene', direction: 'below' },
  });
  api.addPanel({
    id: 'camera',
    component: 'camera',
    title: PANEL_DEFS.camera.title,
    position: { referencePanel: 'inspector', direction: 'below' },
  });
  api.addPanel({
    id: 'assets',
    component: 'assets',
    title: PANEL_DEFS.assets.title,
    position: { referencePanel: 'camera', direction: 'below' },
  });
  api.getPanel('frame')?.api.setActive();
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
        <span style={{ flex: 1 }} />
        <span style={{ fontVariantNumeric: 'tabular-nums' }}>
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
