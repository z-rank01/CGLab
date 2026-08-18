# CGLab 控制平面协议

> 状态：生效（I1，2026-08-17）。方法/遥测负载的**唯一事实源**是
> [`control_plane_protocol.schema.json`](control_plane_protocol.schema.json)——
> 它由 `control_plane::build_protocol_schema()`（`src/control_plane/json_rpc.cpp` 的方法表/通知表）
> 生成，golden 测试（`cglab.control_plane_protocol`）保证二者逐字节一致。
> 有意修改协议后运行 `cglab_control_plane_protocol_tests --write` 重新生成并随提交入库。
> 本文只写 schema 表达不了的传输与生命周期约定。

## 传输与端点

- JSON-RPC 2.0 over WebSocket，默认 `ws://127.0.0.1:17381`（`--ui-port` 可改，只绑回环地址）。
- 同端口提供 HTTP 静态托管（I1）：`GET /` 返回 `web/index.html` dev console；
  `--ui-open-browser` 启动后自动打开。console 的 WS 与页面同源同端口，无需配置。
- CLI：`--no-ui` 关闭控制平面；`--ui-port <port>`；`--ui-open-browser`。

## 生命周期

1. 连接建立 → 服务端推送 `session.hello` 通知（`protocol_version`、`server`）。
2. 客户端应回 `session.init {protocol_version}` 握手；版本不符回 `error -32602`。
3. 之后双向：客户端发方法调用，服务端推送 `telemetry.*` 通知。

## 语义约定

- 命令**全部帧边界生效**（IO 线程解析校验入队，主线程 drain 消费），UI 永不直接触碰引擎内存。
- 参数校验失败只产生 `-32602` error response，不进入引擎命令队列；未知方法 `-32601`。
- 命令队列有界（256），溢出丢弃并回 `-32600` "Command queue is full"。
- 二进制帧不支持；非对象 `params` 拒绝。
- 遥测背压：10Hz `telemetry.frame`；`telemetry.scene` 仅修订号变化时推送；
  `telemetry.load_progress` 仅在流式上传期间。

## 索引

- 方法（`session.init` + 17 个命令方法）的参数规则、值域、result 形态：schema `methods`。
- 通知负载字段（`session.hello`、`telemetry.frame/scene/load/load_progress`）：schema `notifications`。
- 引擎侧命令执行与遥测拼装：`src/engine/engine_runtime.cpp`。
