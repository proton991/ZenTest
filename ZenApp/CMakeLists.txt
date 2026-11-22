add_test(NAME ZenAppStartupOptions
  COMMAND ${CMAKE_COMMAND} "-DZEN_APP=$<TARGET_FILE:ZenApp>"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckStartupOptions.cmake")
