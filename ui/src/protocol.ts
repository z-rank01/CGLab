// 控制平面协议层：类型对照 docs/control_plane_protocol/control_plane_protocol.schema.json 手写。
// JSON-RPC 2.0 over WebSocket，与页面同源同端口。

// ---------- 通知负载 ----------

export interface SessionHello {
  protocol_version: number;
  server: string;
}

export interface SessionInitResult {
  protocol_version: number;
  server: string;
  capabilities: string[];
}

export type CameraMode = 'fly' | 'orbit';

export interface CameraState {
  mode: CameraMode;
  position: number[];
  yaw: number;
  pitch: number;
  fov: number;
  orbit_distance: number;
  focus_point: number[];
  movement_speed: number;
  mouse_sensitivity: number;
  zoom_speed: number;
  near_plane: number;
  far_plane: number;
  bookmarks_valid: boolean[];
  blending: boolean;
  culling: boolean;
}

export interface PhaseUs {
  poll_events: number;
  consume_control_commands: number;
  merge_asset_results: number;
  update_scene_transforms: number;
  update_cameras: number;
  run_sample_systems: number;
  extract_render_packet: number;
  apply_resource_changes: number;
  submit_render_packet: number;
  publish_telemetry: number;
}

export interface FrameQuantiles {
  frame_p50_ms: number;
  frame_p95_ms: number;
  frame_p99_ms: number;
}

export interface FrameCounters {
  instances: number;
  visible: number;
  culled: number;
  draws: number;
  buffer_uploads: number;
  image_uploads: number;
  shadow_draws: number;
  main_draws: number;
  debug_draws: number;
  resolve_draws: number;
}

export interface FrameTelemetry {
  fps: number;
  frame_time_ms: number;
  presented_frames: number;
  draw_pass_executions: number;
  upload_pass_executions: number;
  steady_frame_descriptor_updates: number;
  pipeline_creations: number;
  indirect_groups: number;
  validation_errors: number;
  paused: boolean;
  camera: CameraState;
  phase_us: PhaseUs;
  quantiles: FrameQuantiles;
  counters: FrameCounters;
  // 引擎当前实际生效的调试视图（0=off 1=shadow 2=depth 3=hdr 4=resolved）；
  // 老引擎不下发该字段，可选兜底
  debug_view?: number;
}

export interface Vec3Bounds {
  min: number[];
  max: number[];
}

export interface ObjectTransform {
  position: number[];
  rotation: number[];
  scale: number[];
}

export interface SceneObject {
  id: number;
  name: string;
  visible: boolean;
  read_only: boolean;
  draw_count: number;
  bounds: Vec3Bounds;
  transform: ObjectTransform;
}

export interface SceneState {
  revision: number;
  selected: number | null;
  objects: SceneObject[];
}

export interface GeometryArena {
  count: number;
  created: number;
  reserved_bytes: number;
  used_bytes: number;
  allocation_us: number;
  plan_us: number;
  transfer_us: number;
}

export interface LoadTelemetry {
  path: string;
  load_us: number;
  merge_us: number;
  upload_us: number;
  parse_us: number;
  convert_us: number;
  decode_us: number;
  vertex_bytes: number;
  index_bytes: number;
  images: number;
  geometry_arena: GeometryArena;
}

export interface LoadProgress {
  path: string;
  uploaded_bytes: number;
  total_bytes: number;
  fraction: number;
  meshes: { done: number; total: number };
}

export type DebugView = 'off' | 'shadow' | 'depth' | 'hdr' | 'resolved';

// telemetry.frame.debug_view 下标 → 名称（顺序即引擎侧枚举值）
export const DEBUG_VIEW_NAMES = [
  'off',
  'shadow',
  'depth',
  'hdr',
  'resolved',
] as const satisfies readonly DebugView[];

export interface CameraParams {
  fov?: number;
  movement_speed?: number;
  mouse_sensitivity?: number;
  zoom_speed?: number;
  orbit_distance?: number;
  near_plane?: number;
  far_plane?: number;
}

export interface RpcError {
  code: number;
  message: string;
}

// ---------- telemetry.rg（M8/B3：RG 快照，recompile 时推送） ----------

export interface RgPass {
  index: number; // schedule 序下标（edges/barriers 的 pass 引用口径）
  name: string;
  kind: string; // raster | compute | copy
  queue: string; // graphics | compute | copy
  flags: string[]; // backend_upload | side_effect
  colors: { resource: string; load: string; store: string }[];
  depth: { resource: string; load: string; store: string } | null;
}

export interface RgEdge {
  from: number;
  to: number;
  kind: string; // sync | cross_queue
}

export interface RgBarrierState {
  usage: string[];
  access: string;
  domain: string;
  queue: string;
}

export interface RgBarrier {
  pass: number | null;
  scope: string; // prologue | epilogue
  kind: string; // image | buffer
  resource: string;
  phase: string; // full | release | acquire
  intents: string[];
  producer: number | null;
  before: RgBarrierState;
  after: RgBarrierState;
}

export interface RgAlias {
  kind: string; // image | buffer
  previous: string;
  next: string;
  memory_block: number | null;
  at_pass: number | null;
}

export interface RgResource {
  name: string;
  kind: string; // image | buffer
  imported: boolean;
  first_pass: number | null;
  last_pass: number | null;
  physical: number | null;
  memory_block: number | null;
}

export interface RgDump {
  passes: RgPass[];
  edges: RgEdge[];
  barriers: RgBarrier[];
  aliases: RgAlias[];
  resources: RgResource[];
  statistics: Record<string, number>;
}

export interface RgTelemetry {
  revision: number;
  dump: RgDump;
}

// rg.get_dump 结果（与 telemetry.rg 负载同形）
export type RgGetDumpResult = RgTelemetry;

// ---------- RPC 客户端 ----------

export interface ClientHandlers {
  onOpen?: () => void;
  onClose?: () => void;
  onHello?: (p: SessionHello) => void;
  onFrame?: (p: FrameTelemetry) => void;
  onScene?: (p: SceneState) => void;
  onLoad?: (p: LoadTelemetry) => void;
  onLoadProgress?: (p: LoadProgress) => void;
  onRg?: (p: RgTelemetry) => void;
}

const RECONNECT_MS = 3000;

export class ControlPlaneClient {
  private ws: WebSocket | null = null;
  private nextId = 0;
  private pending = new Map<
    number,
    { resolve: (r: unknown) => void; reject: (e: RpcError) => void }
  >();
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private stopped = false;

  constructor(private readonly handlers: ClientHandlers) {}

  start(): void {
    this.stopped = false;
    this.connect();
  }

  dispose(): void {
    this.stopped = true;
    if (this.reconnectTimer !== null) clearTimeout(this.reconnectTimer);
    this.ws?.close();
  }

  get connected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN;
  }

  call<T = unknown>(method: string, params: object = {}): Promise<T> {
    return new Promise<T>((resolve, reject) => {
      if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
        reject({ code: -1, message: 'WebSocket 未连接' });
        return;
      }
      const id = ++this.nextId;
      this.pending.set(id, {
        resolve: resolve as (r: unknown) => void,
        reject,
      });
      this.ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
    });
  }

  private connect(): void {
    const proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
    const ws = new WebSocket(proto + location.host);
    this.ws = ws;

    ws.onopen = () => this.handlers.onOpen?.();
    ws.onclose = () => {
      // 断开时拒绝所有在途请求
      const err: RpcError = { code: -1, message: '连接已断开' };
      for (const p of this.pending.values()) p.reject(err);
      this.pending.clear();
      this.handlers.onClose?.();
      if (!this.stopped) {
        this.reconnectTimer = setTimeout(() => this.connect(), RECONNECT_MS);
      }
    };
    ws.onerror = () => {
      /* onclose 会随后触发重连 */
    };
    ws.onmessage = (ev: MessageEvent<string>) => {
      let msg: {
        id?: number | null;
        method?: string;
        params?: unknown;
        result?: unknown;
        error?: RpcError;
      };
      try {
        msg = JSON.parse(ev.data);
      } catch {
        return;
      }
      if (msg.id !== undefined && msg.id !== null && this.pending.has(msg.id)) {
        const p = this.pending.get(msg.id)!;
        this.pending.delete(msg.id);
        if (msg.error) p.reject(msg.error);
        else p.resolve(msg.result);
        return;
      }
      switch (msg.method) {
        case 'session.hello':
          this.handlers.onHello?.(msg.params as SessionHello);
          break;
        case 'telemetry.frame':
          this.handlers.onFrame?.(msg.params as FrameTelemetry);
          break;
        case 'telemetry.scene':
          this.handlers.onScene?.(msg.params as SceneState);
          break;
        case 'telemetry.load':
          this.handlers.onLoad?.(msg.params as LoadTelemetry);
          break;
        case 'telemetry.load_progress':
          this.handlers.onLoadProgress?.(msg.params as LoadProgress);
          break;
        case 'telemetry.rg':
          this.handlers.onRg?.(msg.params as RgTelemetry);
          break;
      }
    };
  }
}
