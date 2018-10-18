if(NOT(USE_X11 OR ENABLE_HEADLESS))
  return()
endif()

add_executable(dolphin-fm-nogui
  MainNoGUI.cpp
)

set_target_properties(dolphin-fm-nogui PROPERTIES OUTPUT_NAME dolphin-fm-nogui)

target_link_libraries(dolphin-fm-nogui
PRIVATE
  core
  uicommon
  cpp-optparse
)

if(USE_DISCORD_PRESENCE)
  target_compile_definitions(dolphin-fm-nogui PRIVATE -DUSE_DISCORD_PRESENCE)
endif()
