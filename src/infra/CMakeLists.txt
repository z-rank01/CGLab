# 基础设施层（C1，infra I0–I3）：零第三方依赖、可整体抽仓的最小并发设施。
# I0 先立 benchmark 设施（验收：跑出空任务 thread-per-task vs 队列的基线数据）；
# I1 起在此承载 job system 库源码。

add_library(infra INTERFACE)
add_library(cglab::infra ALIAS infra)
target_compile_features(infra INTERFACE cxx_std_20)
target_include_directories(infra INTERFACE ${PROJECT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(infra INTERFACE /utf-8)
endif()

if(BUILD_TESTING)
    add_executable(infra_benchmark
        bench/empty_task_benchmark.cpp
    )
    target_link_libraries(infra_benchmark PRIVATE infra)
    if(MSVC)
        target_compile_options(infra_benchmark PRIVATE /FS)
    endif()
    set_target_properties(infra_benchmark PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )
    add_test(NAME cglab.infra_benchmark COMMAND infra_benchmark)
endif()
