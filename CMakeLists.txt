cmake_minimum_required(VERSION 3.23)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(ZenTest LANGUAGES C CXX)
  set(ZEN_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "ZenEngine checkout")
  if(NOT EXISTS "${ZEN_ENGINE_SOURCE_DIR}/ZenSubproject.cmake")
    message(FATAL_ERROR "Set ZEN_ENGINE_SOURCE_DIR to a ZenEngine checkout")
  endif()
  include("${ZEN_ENGINE_SOURCE_DIR}/ZenSubproject.cmake")
endif()
set(ZEN_TEST_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT BUILD_TESTING)
  message(FATAL_ERROR "ZenTest requires BUILD_TESTING=ON")
endif()
add_library(ZenVulkanTestMain STATIC CommonTest/VulkanTestMain.cpp)
target_link_libraries(ZenVulkanTestMain PUBLIC ZenCore gtest)
add_executable(RuntimeLaunchTest CommonTest/RuntimeLaunchTests.cpp)
target_link_libraries(RuntimeLaunchTest PRIVATE ZenCore gtest_main)
add_test(NAME RuntimeLaunchTest COMMAND RuntimeLaunchTest)
set_tests_properties(RuntimeLaunchTest PROPERTIES LABELS "unit" TIMEOUT 30)
add_executable(ThreadPoolTest ThreadPoolTest/main.cpp)
add_executable(FlatHashMapTest FlatHashMapTest/FlatHashMapTests.cpp)
add_executable(VulkanRHITest
    CommonTest/VulkanReviewTests.cpp
    CommonTest/ShaderReflectionTests.cpp
    CommonTest/VulkanSynchronizationTests.cpp
    CommonTest/VulkanLifetimeTrackerTests.cpp
    CommonTest/VulkanResourceCreationTests.cpp
    CommonTest/VulkanShaderLayoutTests.cpp
    CommonTest/VulkanVerificationTests.cpp
    CommonTest/VulkanQueueSelectionTests.cpp
)
target_link_libraries(VulkanRHITest PRIVATE ZenCore gtest_main)
# Explicit integration target: requires a Vulkan device and validation layer.
add_executable(RayQueryIntegrationTest CommonTest/RayQueryIntegrationTests.cpp)
target_link_libraries(RayQueryIntegrationTest PRIVATE ZenCore ZenVulkanTestMain)
add_executable(VulkanRHIIntegrationTest
    CommonTest/VulkanReviewIntegrationTests.cpp
    CommonTest/VulkanPresentationReviewTests.cpp
    CommonTest/RHIProgressIntegrationTests.cpp
    CommonTest/RHIWindowThreadingTests.cpp
    CommonTest/VulkanDescriptorIntegrationTests.cpp
    CommonTest/VulkanDescriptorLifetimeTests.cpp
    CommonTest/VulkanQueueIntegrationTests.cpp
    CommonTest/VulkanCommandBufferLifetimeTests.cpp
    CommonTest/VulkanGPUTimingIntegrationTests.cpp
    CommonTest/VulkanPipelineIntegrationTests.cpp
    CommonTest/VulkanShaderLayoutIntegrationTests.cpp
    CommonTest/VulkanCommandValidationIntegrationTests.cpp
    CommonTest/VulkanProductionFailureTests.cpp
    CommonTest/VulkanBindingIntegrationTests.cpp
    CommonTest/VulkanSwapchainIntegrationTests.cpp
    CommonTest/VulkanCapabilityIntegrationTests.cpp
    CommonTest/VulkanAttachmentIntegrationTests.cpp
    CommonTest/VulkanRecordingIntegrationTests.cpp
    CommonTest/VulkanBindlessRetirementIntegrationTests.cpp
    CommonTest/VulkanAliasedQueueIntegrationTests.cpp
    CommonTest/VulkanAsyncComputeIntegrationTests.cpp
    CommonTest/VulkanUploadIntegrationTests.cpp
    CommonTest/BindlessSceneCapacityIntegrationTests.cpp
)
target_link_libraries(VulkanRHIIntegrationTest PRIVATE ZenCore ZenVulkanTestMain)
# Compile reflection fixtures as test inputs, without adding them to engine shader assets.
set(RDG_REFLECTION_TEST_PATH "${CMAKE_CURRENT_BINARY_DIR}/rdg-reflection/")
set(RDG_REFLECTION_TEST_BINARIES)
foreach(SHADER review_acceleration.comp review_runtime_array.comp review_reserved_set.comp review_duplicate.vert review_duplicate.frag rdg_access.comp rdg_shared.vert rdg_shared.frag
        descriptor_storage.comp descriptor_array.comp descriptor_bindings.comp
        descriptor_uniform.comp descriptor_uniform_large.comp descriptor_uniform_limit.comp descriptor_sampler_limit.comp
        pipeline_specialization.comp pipeline.vert pipeline.frag pipeline_depth.frag pipeline_dual_source.frag
        binding_sampler.comp binding_recording_array.comp binding_single_layer_array.comp binding_uniform_array.comp binding_bindless.comp binding_bindless.frag binding_bindless_array.comp binding_bindless_cube_array.comp binding_push_constants.comp
        recording_uniform.frag recording_uniform.comp recording_vertex.vert async_resources.comp binding_texel.comp
        binding_high_set.comp binding_required_sets.comp binding_validation.comp
        unsupported_tessellation.tesc unsupported_tessellation.tese)
    set(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/CommonTest/Shaders/${SHADER}")
    set(OUTPUT "${RDG_REFLECTION_TEST_PATH}${SHADER}.spv")
    add_custom_command(OUTPUT "${OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RDG_REFLECTION_TEST_PATH}"
        COMMAND ${GLSL_VALIDATOR} --target-env spirv1.3 -V "${SOURCE}" "-I${ZEN_ENGINE_SOURCE_DIR}/ZenCore/Include" "-I${ZEN_ENGINE_SOURCE_DIR}/Data/Shaders" -o "${OUTPUT}"
        DEPENDS "${SOURCE}" "${ZEN_ENGINE_SOURCE_DIR}/Data/Shaders/Common/bindless_heap.glsl"
                "${ZEN_ENGINE_SOURCE_DIR}/ZenCore/Include/Graphics/Shared/Bindless.h" VERBATIM)
    list(APPEND RDG_REFLECTION_TEST_BINARIES "${OUTPUT}")
endforeach()
add_custom_target(RDGReflectionTestShaders DEPENDS ${RDG_REFLECTION_TEST_BINARIES})
add_dependencies(VulkanRHITest RDGReflectionTestShaders)
add_dependencies(VulkanRHIIntegrationTest RDGReflectionTestShaders)
target_compile_definitions(VulkanRHITest PRIVATE RDG_REFLECTION_TEST_PATH="${RDG_REFLECTION_TEST_PATH}")
target_compile_definitions(VulkanRHIIntegrationTest PRIVATE RDG_REFLECTION_TEST_PATH="${RDG_REFLECTION_TEST_PATH}")

add_executable(RenderCoreTest
    RenderCoreTest/RenderCoreTests.cpp
    RenderCoreTest/RDGMetricsTests.cpp
    RenderCoreTest/RDGProfilingTests.cpp
    RenderCoreTest/RHIGPUTimingTests.cpp
    RenderCoreTest/RHIGPUFrameTimingTests.cpp
    RenderCoreTest/VoxelResourcePlanningTests.cpp
    RenderCoreTest/VoxelGISettingsTests.cpp
)
target_include_directories(RenderCoreTest PRIVATE
    ${ZEN_ENGINE_SOURCE_DIR}/ZenCore/Include
)
target_compile_definitions(RenderCoreTest PRIVATE
    $<TARGET_PROPERTY:ZenCore,INTERFACE_COMPILE_DEFINITIONS>
)
target_link_libraries(RenderCoreTest PRIVATE ZenRenderCoreTestSources gtest_main spdlog::spdlog gli glm spirv-reflect)
# The shared fake-backend test translation unit exceeds the standard COFF section limit.
set_source_files_properties(RenderCoreTest/RenderCoreTests.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/bigobj>")
add_dependencies(RenderCoreTest SpvShaders)
add_executable(EncoderAllocationBenchmark RenderCoreTest/EncoderAllocationBenchmark.cpp)
target_link_libraries(EncoderAllocationBenchmark PRIVATE ZenCore)
add_executable(LRUCacheTest LRUCacheTest/LRUCacheTests.cpp)
target_include_directories(LRUCacheTest PRIVATE ${ZEN_ENGINE_SOURCE_DIR}/ZenCore/Include)
target_link_libraries(LRUCacheTest PRIVATE gtest_main spdlog::spdlog)
add_executable(InputControllerTest CommonTest/InputControllerTests.cpp)
target_link_libraries(InputControllerTest PRIVATE ZenCore gtest_main)
add_executable(ConfigLoaderTest CommonTest/ConfigLoaderTests.cpp)
target_link_libraries(ConfigLoaderTest PRIVATE ZenCore gtest_main)

add_executable(CommonTest
    CommonTest/CameraInputTests.cpp
    CommonTest/CleanupRegressionTests.cpp
    CommonTest/SmallVectorTests.cpp
    CommonTest/NameIDTests.cpp
    CommonTest/MemoryTests.cpp
    CommonTest/ErrorTests.cpp
    CommonTest/LockTests.cpp
    CommonTest/PagedAllocatorTest.h
    CommonTest/PagedAllocatorTest.cpp
)
add_executable(SmartPtrTest
    SmartPtrTest/SharedPtrTests.cpp
    SmartPtrTest/SharedPtrLifetimeTests.cpp
    SmartPtrTest/UniquePtrTests.cpp
    SmartPtrTest/IntrusivePtrTests.cpp
    SmartPtrTest/RefCountPtrTests.cpp
)


# Native Cone visibility regression, isolated from suite-global Vulkan fixtures.
add_executable(ConeVoxelGIIntegrationTest CommonTest/ConeVoxelGIIntegrationTests.cpp
    CommonTest/VoxelReflectanceIntegrationTests.cpp)
target_link_libraries(ConeVoxelGIIntegrationTest PRIVATE ZenCore ZenVulkanTestMain)
add_dependencies(ConeVoxelGIIntegrationTest SpvShaders)

target_link_libraries(ThreadPoolTest ZenCore)
target_link_libraries(FlatHashMapTest ZenCore gtest_main)
target_link_libraries(SmartPtrTest ZenCore gtest_main)
target_link_libraries(CommonTest PRIVATE ZenCore gtest_main)
target_include_directories(CommonTest PRIVATE ${ZEN_ENGINE_SOURCE_DIR}/ZenCore/Source)

find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_test(NAME NoEngineExceptions COMMAND ${Python3_EXECUTABLE} ${ZEN_ENGINE_SOURCE_DIR}/tools/check_no_exceptions.py)
    set_tests_properties(NoEngineExceptions PROPERTIES LABELS "unit;policy" TIMEOUT 30)
endif ()
foreach(TEST_TARGET CommonTest SmartPtrTest FlatHashMapTest LRUCacheTest
        InputControllerTest ConfigLoaderTest VulkanRHITest RenderCoreTest)
    add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
    set_tests_properties(${TEST_TARGET} PROPERTIES LABELS "unit" TIMEOUT 120)
endforeach()
add_test(NAME VulkanRHIIntegrationTest COMMAND VulkanRHIIntegrationTest)
set_tests_properties(VulkanRHIIntegrationTest PROPERTIES LABELS "vulkan;integration" TIMEOUT 300)

# Native acceptance tests deliberately inspect the private window adapter.
target_include_directories(VulkanRHIIntegrationTest PRIVATE ${ZEN_ENGINE_SOURCE_DIR}/ZenCore/Source)
target_link_libraries(VulkanRHIIntegrationTest PRIVATE ZenWindowBackendInternal)
add_executable(WindowPlatformTest CommonTest/WindowPlatformTests.cpp)
target_link_libraries(WindowPlatformTest PRIVATE ZenCore ZenWindowBackendInternal ZenVulkanTestMain)
add_test(NAME WindowPlatformTest COMMAND WindowPlatformTest)
set_tests_properties(WindowPlatformTest PROPERTIES LABELS "native" TIMEOUT 60)

if(TARGET ZenUI)
  add_subdirectory(ZenUI)
endif()
if(TARGET ZenEditorModel)
  add_subdirectory(ZenEditor)
endif()
if(TARGET ZenApp)
  add_subdirectory(ZenApp)
endif()
if(Python3_Interpreter_FOUND)
  add_test(NAME PythonToolTests COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/Python" -p "test_*.py")
  set_tests_properties(PythonToolTests PROPERTIES ENVIRONMENT "PYTHONPATH=${ZEN_ENGINE_SOURCE_DIR}/tools" LABELS "unit" TIMEOUT 120)
endif()
