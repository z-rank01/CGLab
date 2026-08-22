# 基础设施层（C1，infra I0–I3）：零第三方依赖、可整体抽仓的最小并发设施。
# I0 benchmark 设施；I1 最小 job system（job_system.h/.cpp 库本体）。

add_library(infra STATIC)
add_library(cglab::infra ALIAS infra)
target_sources(infra PRIVATE
    job_system.cpp
)
target_compile_features(infra PUBLIC cxx_std_20)
target_include_directories(infra PUBLIC ${PROJECT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(infra PUBLIC /utf-8)
endif()

if(BUILD_TESTING)
    add_executable(infra_benchmark
        bench/empty_task_benchmark.cpp
    )
    target_link_libraries(infra_benchmark PRIVATE infra)

    add_executable(infra_job_system_tests
        tests/job_system_test.cpp
    )
    target_link_libraries(infra_job_system_tests PRIVATE infra)

    foreach(_infra_test IN ITEMS infra_benchmark infra_job_system_tests)
        if(MSVC)
            target_compile_options(${_infra_test} PRIVATE /FS)
        endif()
        set_target_properties(${_infra_test} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        )
    endforeach()

    add_test(NAME cglab.infra_benchmark COMMAND infra_benchmark)
    add_test(NAME cglab.infra_job_system COMMAND infra_job_system_tests)
endif()
