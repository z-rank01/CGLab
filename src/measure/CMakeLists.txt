# measure —— A0 测量设施（叶子库，只依赖 std）。
# 与渲染/引擎完全解耦：engine_runtime 生产样本，control_plane 只消费 JSON。
add_library(measure INTERFACE)

target_include_directories(measure INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_compile_features(measure INTERFACE cxx_std_20)
