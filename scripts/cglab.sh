#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ACTION="${1:-help}"
if [[ $# -gt 0 ]]; then shift; fi

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "CGLab officially supports this script on Linux. Use scripts/cglab.ps1 on Windows." >&2
  exit 1
fi

PRESET="${CGLAB_PRESET:-linux-clang-ninja}"
CONFIG="${CGLAB_CONFIG:-Debug}"
TARGET="TriangleSample"
JOBS=""
TESTS="OFF"
LEGACY="OFF"
FRESH=0
TRIPLET=""
VCPKG_ROOT_VALUE="${VCPKG_ROOT:-$ROOT/vcpkg}"
VCPKG_ROOT_EXPLICIT=0
[[ -n "${VCPKG_ROOT:-}" ]] && VCPKG_ROOT_EXPLICIT=1
FRAMES="6"
ASSET=""

usage() {
  cat <<'EOF'
CGLab development entry point

Usage: ./scripts/cglab.sh <action> [options]

Actions: doctor, setup, deps, configure, build, test, smoke, clean

Options:
  --preset NAME       windows-msvc-ninja, linux-clang-ninja, or a complete preset name
  --config TYPE       Debug, Release or RelWithDebInfo
  --target NAME       CMake target, or app target for smoke
  --jobs COUNT        Parallel build jobs
  --fresh             Use cmake --fresh when configuring
  --tests ON|OFF      Configure CTest targets
  --legacy ON|OFF     Configure VulkanSampleLegacy
  --triplet NAME      vcpkg target triplet
  --vcpkg-root PATH   vcpkg checkout location
  --frames COUNT      GPU smoke frame count
  --asset PATH        glTF/GLB asset for GltfSponzaSample
EOF
}

die() { echo "CGLab: $*" >&2; exit 1; }
run() {
  "$@"
}
require_command() {
  command -v "$1" >/dev/null 2>&1 || die "Missing required tool '$1'. Install git, cmake, ninja-build, clang and clang++, then rerun doctor."
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --fresh) FRESH=1; shift ;;
    --preset|--config|--target|--jobs|--tests|--legacy|--triplet|--vcpkg-root|--frames|--asset)
      [[ $# -ge 2 ]] || die "Missing value for $1"
      key="${1#--}"; value="$2"; shift 2
      case "$key" in
        preset) PRESET="$value";; config) CONFIG="$value";; target) TARGET="$value";; jobs) JOBS="$value";;
        tests) TESTS="${value^^}";; legacy) LEGACY="${value^^}";; triplet) TRIPLET="$value";;
        vcpkg-root) VCPKG_ROOT_VALUE="$value"; VCPKG_ROOT_EXPLICIT=1;; frames) FRAMES="$value";; asset) ASSET="$value";;
      esac
      ;;
    --help|-h) usage; exit 0 ;;
    --*=*) key="${1%%=*}"; value="${1#*=}"; set -- "${key}" "${value}" "$@"; shift 2 ;;
    *) die "Unknown argument '$1'. Use --help for usage." ;;
  esac
done

resolve_preset() {
  if [[ "$PRESET" =~ -(debug|release|relwithdebinfo)$ ]]; then
    printf '%s\n' "$PRESET"
  elif [[ "$PRESET" == "windows-msvc-ninja" || "$PRESET" == "linux-clang-ninja" ]]; then
    printf '%s-%s\n' "$PRESET" "${CONFIG,,}"
  else
    printf '%s\n' "$PRESET"
  fi
}

default_triplet() {
  [[ -n "$TRIPLET" ]] && printf '%s\n' "$TRIPLET" && return
  local preset="$1"
  [[ "$preset" == linux-* ]] && printf '%s\n' "x64-linux" || printf '%s\n' "x64-windows"
}

build_directory() {
  [[ -n "${CGLAB_BUILD_DIR:-}" ]] && printf '%s\n' "$CGLAB_BUILD_DIR" && return
  case "$1" in
    windows-msvc-ninja-debug) printf '%s\n' "$ROOT/build/windows-msvc-ninja/Debug";;
    windows-msvc-ninja-release) printf '%s\n' "$ROOT/build/windows-msvc-ninja/Release";;
    windows-msvc-ninja-relwithdebinfo) printf '%s\n' "$ROOT/build/windows-msvc-ninja/RelWithDebInfo";;
    linux-clang-ninja-debug) printf '%s\n' "$ROOT/build/linux-clang-ninja/Debug";;
    linux-clang-ninja-release) printf '%s\n' "$ROOT/build/linux-clang-ninja/Release";;
    linux-clang-ninja-relwithdebinfo) printf '%s\n' "$ROOT/build/linux-clang-ninja/RelWithDebInfo";;
    *) printf '%s\n' "";;
  esac
}

doctor() {
  local failed=0
  echo "CGLab environment doctor"
  for tool in git cmake ninja clang clang++; do
    if command -v "$tool" >/dev/null 2>&1; then echo "  [OK] $tool"; else echo "  [MISSING] $tool"; failed=1; fi
  done
  if [[ -d "$VCPKG_ROOT_VALUE/.git" ]]; then echo "  [OK] vcpkg checkout: $VCPKG_ROOT_VALUE"; else echo "  [INFO] vcpkg will be bootstrapped by setup"; fi
  if [[ -d "$ROOT/third_party/render-graph/.git" ]]; then echo "  [OK] submodules"; else echo "  [INFO] submodules will be initialized by setup"; fi
  if command -v glslc >/dev/null 2>&1; then echo "  [OK] glslc (shader rebuild)"; else echo "  [OPTIONAL] glslc not found; checked-in SPIR-V can still be used"; fi
  if [[ -n "${VULKAN_SDK:-}" ]]; then echo "  [OK] VULKAN_SDK=$VULKAN_SDK"; else echo "  [OPTIONAL] VULKAN_SDK is not set"; fi
  if command -v vulkaninfo >/dev/null 2>&1; then echo "  [OK] Vulkan runtime query"; else echo "  [OPTIONAL] vulkaninfo not found; GPU smoke may require a driver/runtime"; fi
  if [[ $failed -ne 0 ]]; then
    echo "Install missing required tools, then rerun doctor." >&2
    echo "Suggested packages: git cmake ninja-build clang clang++." >&2
    return 1
  fi
}

ensure_submodules() {
  git -C "$ROOT" submodule sync --recursive
  git -C "$ROOT" submodule update --init --recursive
}

vcpkg_baseline() {
  sed -n 's/.*"builtin-baseline"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/vcpkg.json" | head -n 1
}

ensure_vcpkg() {
  local baseline path="$VCPKG_ROOT_VALUE" remote status
  baseline="$(vcpkg_baseline)"
  [[ -n "$baseline" ]] || die "vcpkg.json does not contain builtin-baseline."
  if [[ -e "$path" ]]; then
    [[ -d "$path/.git" ]] || die "Vcpkg path exists but is not a git checkout: $path"
    remote="$(git -C "$path" config --get remote.origin.url || true)"
    if [[ "$remote" == "git@github.com:microsoft/vcpkg.git" ]]; then
      git -C "$path" remote set-url origin https://github.com/microsoft/vcpkg.git
      remote="https://github.com/microsoft/vcpkg.git"
    fi
    [[ "$remote" == "https://github.com/microsoft/vcpkg.git" || "$remote" == "https://github.com/microsoft/vcpkg" ]] || die "Existing vcpkg remote is '$remote'. No files were changed."
    status="$(git -C "$path" status --porcelain)"
    [[ -z "$status" ]] || die "Existing vcpkg checkout has local changes. Commit or move them before setup."
  else
    mkdir -p "$(dirname "$path")"
    git clone https://github.com/microsoft/vcpkg.git "$path"
  fi
  git -C "$path" cat-file -e "${baseline}^{commit}" 2>/dev/null || git -C "$path" fetch origin "$baseline"
  git -C "$path" checkout --detach "$baseline"
  export VCPKG_ROOT="$path"
  local sentinel="$ROOT/.cglab-vcpkg-bootstrap-baseline"
  if [[ ! -x "$path/vcpkg" || ! -f "$sentinel" || "$(<"$sentinel")" != "$baseline" ]]; then
    (cd "$path" && ./bootstrap-vcpkg.sh -disableMetrics)
    printf '%s' "$baseline" > "$sentinel"
  fi
}

install_dependencies() {
  local preset="$(resolve_preset)" triplet="$(default_triplet "$preset")"
  ensure_vcpkg
  (cd "$ROOT" && "$VCPKG_ROOT_VALUE/vcpkg" install --triplet "$triplet")
}

configure_project() {
  local force_tests="${1:-$TESTS}" preset="$(resolve_preset)"
  [[ -f "$VCPKG_ROOT_VALUE/scripts/buildsystems/vcpkg.cmake" ]] || die "vcpkg is not initialized. Run ./scripts/cglab.sh setup first."
  local args=(--preset "$preset" "-DBUILD_TESTING=$force_tests" "-DCGLAB_BUILD_LEGACY_VULKAN_SAMPLE=$LEGACY")
  [[ $VCPKG_ROOT_EXPLICIT -eq 1 ]] && args+=("-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT_VALUE/scripts/buildsystems/vcpkg.cmake")
  [[ $FRESH -eq 1 ]] && args=(--fresh "${args[@]}")
  (cd "$ROOT" && cmake "${args[@]}")
  printf '%s\n' "$preset"
}

build_target() {
  local preset="$1" target="$2"
  local args=(--build --preset "$preset" --target "$target")
  [[ -n "$JOBS" ]] && args+=(--parallel "$JOBS")
  (cd "$ROOT" && cmake "${args[@]}")
}

run_smoke() {
  local preset="$(resolve_preset)" target="$TARGET"
  [[ "$target" == "TriangleSample" || "$target" == "GltfSponzaSample" ]] || die "Smoke target must be TriangleSample or GltfSponzaSample."
  build_target "$preset" "$target"
  local dir="$(build_directory "$preset")" executable=""
  [[ -n "$dir" && -x "$dir/$target" ]] && executable="$dir/$target"
  [[ -n "$executable" ]] || executable="$(find "$ROOT/build" -type f -name "$target" -print -quit 2>/dev/null || true)"
  [[ -n "$executable" ]] || die "Built executable '$target' was not found under build/."
  local args=(--smoke-test --frames "$FRAMES" --validation --no-ui)
  if [[ "$target" == "GltfSponzaSample" ]]; then args+=(--asset "${ASSET:-assets/triangle.gltf}"); fi
  (cd "$ROOT" && "$executable" "${args[@]}")
}

case "${ACTION,,}" in
  help|-h|--help) usage;;
  doctor) require_command git; require_command cmake; require_command ninja; require_command clang; require_command clang++; doctor;;
  setup) require_command git; require_command cmake; require_command ninja; require_command clang; require_command clang++; doctor; ensure_submodules; install_dependencies; configure_project;;
  deps) install_dependencies;;
  configure) configure_project >/dev/null;;
  build) preset="$(configure_project)"; build_target "$preset" "$TARGET";;
  test) preset="$(configure_project ON)"; build_target "$preset" all; (cd "$ROOT" && ctest --preset "$preset");;
  smoke) run_smoke;;
  clean) preset="$(resolve_preset)"; build_target "$preset" clean;;
  *) die "Unknown action '$ACTION'. Use --help for usage.";;
esac
