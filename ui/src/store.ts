// 全局 UI 状态：模块级单例 store + useSyncExternalStore。
// 10Hz 遥测刷新频率下浅拷贝开销可忽略。

import { useSyncExternalStore } from 'react';
import {
  CameraMode,
  CameraParams,
  ControlPlaneClient,
  DebugView,
  FrameTelemetry,
  LoadProgress,
  LoadTelemetry,
  SceneState,
  SessionInitResult,
} from './protocol';

const MAX_SAMPLES = 300; // 帧时曲线 ring buffer 长度
const MAX_LOGS = 300;

export interface UiState {
  connected: boolean;
  server: string | null;
  capabilities: string[];
  frame: FrameTelemetry | null;
  samples: FrameTelemetry[];
  scene: SceneState | null;
  loadProgress: LoadProgress | null;
  lastLoad: LoadTelemetry | null;
  debugView: DebugView;
  logs: string[];
}

let state: UiState = {
  connected: false,
  server: null,
  capabilities: [],
  frame: null,
  samples: [],
  scene: null,
  loadProgress: null,
  lastLoad: null,
  debugView: 'off',
  logs: [],
};

const listeners = new Set<() => void>();

function setState(patch: Partial<UiState>): void {
  state = { ...state, ...patch };
  listeners.forEach((l) => l());
}

function subscribe(fn: () => void): () => void {
  listeners.add(fn);
  return () => listeners.delete(fn);
}

export function useUiState(): UiState {
  return useSyncExternalStore(subscribe, () => state);
}

function stamp(): string {
  return `[${new Date().toLocaleTimeString()}]`;
}

export function logEvent(msg: string): void {
  const logs = [...state.logs, `${stamp()} ${msg}`];
  setState({ logs: logs.length > MAX_LOGS ? logs.slice(-MAX_LOGS) : logs });
}

export function clearLogs(): void {
  setState({ logs: [] });
}

// ---------- 客户端装配 ----------

const client = new ControlPlaneClient({
  onOpen: () => {
    setState({ connected: true, server: null });
    logEvent('WebSocket 已连接');
  },
  onClose: () => {
    setState({ connected: false, server: null, capabilities: [] });
    logEvent('连接已断开，3s 后重连…');
  },
  onHello: (p) => {
    setState({ server: `${p.server} (protocol v${p.protocol_version})` });
    call<SessionInitResult>('session.init', {
      protocol_version: p.protocol_version,
    })
      .then((r) => {
        setState({ capabilities: r.capabilities });
        logEvent(`session.init OK，capabilities ${r.capabilities.length} 项`);
      })
      .catch(() => undefined);
  },
  onFrame: (t) => {
    const samples = [...state.samples, t];
    setState({
      frame: t,
      samples: samples.length > MAX_SAMPLES ? samples.slice(-MAX_SAMPLES) : samples,
    });
  },
  onScene: (s) => setState({ scene: s }),
  onLoad: (l) => {
    setState({ lastLoad: l, loadProgress: null });
    logEvent(`资产加载完成: ${l.path} (load ${(l.load_us / 1000).toFixed(1)} ms)`);
  },
  onLoadProgress: (p) => setState({ loadProgress: p }),
});

client.start();

export function call<T = unknown>(method: string, params: object = {}): Promise<T> {
  return client.call<T>(method, params).catch((e: { code: number; message: string }) => {
    logEvent(`${method} → error ${e.code}: ${e.message}`);
    throw e;
  });
}

// ---------- 面板动作（fire-and-forget，错误已记录） ----------

export const actions = {
  pause: () => void call('frame.pause'),
  resume: () => void call('frame.resume'),
  step: (count: number) => void call('frame.step', { count }),
  setCameraMode: (mode: CameraMode) => void call('camera.set_mode', { mode }),
  setCameraParams: (params: CameraParams) => void call('camera.set_params', params),
  setCulling: (enabled: boolean) => void call('camera.set_culling', { enabled }),
  saveBookmark: (slot: number) => void call('camera.bookmark.save', { slot }),
  gotoBookmark: (slot: number) => void call('camera.bookmark.goto', { slot }),
  loadAsset: (path: string) => void call('scene.load_asset', { path }),
  unload: (id: number) => void call('scene.unload', { id }),
  setVisibility: (id: number, visible: boolean) =>
    void call('scene.set_visibility', { id, visible }),
  setTransform: (
    id: number,
    t: { position?: number[]; rotation?: number[]; scale?: number[] },
  ) => void call('scene.set_transform', { id, ...t }),
  select: (id: number | null) => void call('scene.select', { id }),
  setDebugView: (view: DebugView) => {
    setState({ debugView: view });
    void call<{ mode: number }>('debug.set_view', { view });
  },
  echo: (message: string) => void call('debug.echo', { message }),
};
