if(ZEN_BUILD_ASSET_TOOLS)
  add_executable(MakeRuntimeFixture MakeRuntimeFixture.cpp)
  target_link_libraries(MakeRuntimeFixture PRIVATE ZenEngineAssetTools)
  target_compile_definitions(MakeRuntimeFixture PRIVATE
    ZEN_RUNTIME_FIXTURE="${ZEN_TEST_SOURCE_DIR}/ZenEditor/Fixtures/inspection.gltf")
endif()

add_test(NAME ZenAppStartupOptions
  COMMAND ${CMAKE_COMMAND} "-DZEN_APP=$<TARGET_FILE:ZenApp>"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckStartupOptions.cmake")
