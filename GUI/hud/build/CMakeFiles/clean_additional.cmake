# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "CMakeFiles/hud_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/hud_autogen.dir/ParseCache.txt"
  "hud_autogen"
  )
endif()
