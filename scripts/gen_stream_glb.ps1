# M7 验证用合成资产：100 mesh × 4000 三角形（每 mesh 12000 顶点 + 12000 uint16 索引）。
# 文件原始几何 ≈ 16.8MB；引擎侧展开后每 mesh = 12000×72B（engine::vertex）+ 12000×4B
#（uint32 索引）= 912,000B，全资产 91.2MB —— 分块预算是引擎侧口径（8MiB/片），
# 9 mesh/片 → ceil(100/9) = 12 片（已用 plan_upload_chunk 实测复核）。
$ErrorActionPreference = "Stop"
$meshCount = 100
$triPerMesh = 4000
$vertsPerMesh = $triPerMesh * 3
$out = Join-Path $PSScriptRoot "..\build\stream_test.glb"

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
# ---- glTF 2.0 binary header（每个 chunk：chunkLength 在前，chunkType 在后） ----
$bw.Write([uint32]0x46546C67)   # magic 'glTF'
$bw.Write([uint32]2)             # version
$jsonLenPos = $ms.Position
$bw.Write([uint32]0)             # 总长占位（末尾回填）
$jsonChunkLenPos = $ms.Position
$bw.Write([uint32]0)             # JSON chunk length 占位（末尾回填）
$bw.Write([uint32]0x4E4F534A)    # JSON chunk type
$jsonDataPos = $ms.Position      # JSON chunk data 起点 = 20

# ---- 构建二进制几何数据 ----
$binStream = New-Object System.IO.MemoryStream
$bbw = New-Object System.IO.BinaryWriter($binStream)
$binStart = 0
for ($m = 0; $m -lt $meshCount; $m++) {
  $bx = [float](($m % 10) * 2.5)
  $bz = [float]([math]::Floor($m / 10) * 2.5)
  for ($v = 0; $v -lt $vertsPerMesh; $v++) {
    $local = $v % 3
    if ($local -eq 0) { $x = 0.0 } elseif ($local -eq 1) { $x = 1.0 } else { $x = 0.0 }
    $bbw.Write([float]($bx + $x))
    $bbw.Write([float]0.0)
    $bbw.Write([float]($bz + 0.0))
  }
  for ($i = 0; $i -lt $vertsPerMesh; $i++) { $bbw.Write([uint16]$i) }
}
$bbw.Flush()
$binBytes = $binStream.ToArray()

# ---- JSON ----
$meshes = @()
$nodes = @()
$accessors = @()
$bufferViews = @()
for ($m = 0; $m -lt $meshCount; $m++) {
  $vOff = $m * $vertsPerMesh * 3 * 4
  $iOff = $meshCount * $vertsPerMesh * 3 * 4 + $m * $vertsPerMesh * 2
  $meshes += @{
    name = "StreamMesh$m"
    primitives = @(@{
      attributes = @{ POSITION = ($m * 2) }
      indices = ($m * 2 + 1)
      material = 0
      mode = 4
    })
  }
  $nodes += @{
    name = "StreamNode$m"
    mesh = $m
    translation = @( [double](($m % 10) * 2.5), 0.0, [double]([math]::Floor($m / 10) * 2.5) )
  }
  $accessors += @{
    bufferView = ($m * 2)
    componentType = 5126
    count = $vertsPerMesh
    type = "VEC3"
    min = @( [double]($(($m % 10) * 2.5)), 0.0, [double]($( [math]::Floor($m / 10) * 2.5)) )
    max = @( [double]($(($m % 10) * 2.5) + 1.0), 0.0, [double]($( [math]::Floor($m / 10) * 2.5)) )
  }
  $accessors += @{
    bufferView = ($m * 2 + 1)
    componentType = 5123
    count = $vertsPerMesh
    type = "SCALAR"
  }
  $bufferViews += @{ buffer = 0; byteOffset = $vOff; byteLength = ($vertsPerMesh * 3 * 4); target = 34962 }
  $bufferViews += @{ buffer = 0; byteOffset = $iOff; byteLength = ($vertsPerMesh * 2); target = 34963 }
}
$jsonObj = @{
  asset = @{ version = "2.0"; generator = "CGLab M7 stream verification" }
  scene = 0
  scenes = @(@{ name = "StreamScene"; nodes = @(0..($meshCount - 1)) })
  nodes = $nodes
  meshes = $meshes
  materials = @(@{ name = "StreamMaterial"; pbrMetallicRoughness = @{ baseColorFactor = @(0.6, 0.7, 0.9, 1.0); metallicFactor = 0.0; roughnessFactor = 1.0 } })
  accessors = $accessors
  bufferViews = $bufferViews
  buffers = @(@{ byteLength = $binBytes.Length })
}
$json = $jsonObj | ConvertTo-Json -Depth 10 -Compress
$jsonBytes = [System.Text.Encoding]::UTF8.GetBytes($json)
# glTF 规范：JSON chunk 以空格（0x20）填充到 4 字节对齐
$jsonPad = (4 - ($jsonBytes.Length % 4)) % 4
if ($jsonPad -gt 0) { $jsonBytes += [byte[]]::new($jsonPad); for ($i = 1; $i -le $jsonPad; $i++) { $jsonBytes[$jsonBytes.Length - $i] = 0x20 } }

# ---- 写 chunk（注意：BinaryWriter.Write(byte[]) 会写 7-bit 长度前缀，
# 必须经 MemoryStream.Write 裸写；chunk = length + type + data） ----
$ms.Write($jsonBytes, 0, $jsonBytes.Length)
$bw.Write([uint32]$binBytes.Length)    # BIN chunk length
$bw.Write([uint32]0x004E4942)          # BIN chunk type
$ms.Write($binBytes, 0, $binBytes.Length)
$bw.Flush()
# ---- 回填总长与 JSON chunk 长度 ----
$total = $ms.Length
$ms.Position = 8
$bw.Write([uint32]$total)
$ms.Position = $jsonChunkLenPos
$bw.Write([uint32]$jsonBytes.Length)
$bw.Flush()
$bytes = $ms.ToArray()
[System.IO.File]::WriteAllBytes($out, $bytes)
Write-Host "written $out ($([math]::Round($bytes.Length/1MB,1)) MB, json $($jsonBytes.Length) bytes, bin $($binBytes.Length) bytes)"
