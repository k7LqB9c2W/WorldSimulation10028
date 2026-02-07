cmake_minimum_required(VERSION 3.16)
project(WorldSimulation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(SFML_ROOT)
  list(APPEND CMAKE_PREFIX_PATH
    "${SFML_ROOT}"
    "${SFML_ROOT}/lib/cmake/SFML")
endif()

find_package(SFML 2.6 COMPONENTS graphics window system audio REQUIRED)

set(PROJECT_SOURCES
  src/main.cpp
  src/country.cpp
  src/map.cpp
  src/renderer.cpp
  src/news.cpp
  src/technology.cpp
  src/culture.cpp
  src/great_people.cpp
  src/resource.cpp
  src/trade.cpp
  src/economy.cpp
  src/simulation_context.cpp
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/imgui_stdlib.cpp
  third_party/imgui-sfml/imgui-SFML.cpp
)

add_executable(WorldSimulation ${PROJECT_SOURCES})

target_include_directories(WorldSimulation PRIVATE
  include
  third_party/imgui
  third_party/imgui-sfml
  third_party/tomlplusplus/include
)

if(MSVC)
  target_compile_options(WorldSimulation PRIVATE /W3 /EHsc)
endif()

if(WIN32)
  target_link_libraries(WorldSimulation PRIVATE
    sfml-graphics
    sfml-window
    sfml-system
    sfml-audio
    opengl32
  )
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(WorldSimulation PRIVATE
    sfml-graphics
    sfml-window
    sfml-system
    sfml-audio
    OpenGL::GL
  )
endif()

set_target_properties(WorldSimulation PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET WorldSimulation POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:WorldSimulation>/assets"
)

if(WIN32 AND SFML_ROOT)
  set(SFML_RUNTIME_DLLS
    "${SFML_ROOT}/bin/openal32.dll"
    "${SFML_ROOT}/bin/sfml-system-2.dll"
    "${SFML_ROOT}/bin/sfml-window-2.dll"
    "${SFML_ROOT}/bin/sfml-graphics-2.dll"
    "${SFML_ROOT}/bin/sfml-audio-2.dll"
    "${SFML_ROOT}/bin/sfml-system-d-2.dll"
    "${SFML_ROOT}/bin/sfml-window-d-2.dll"
    "${SFML_ROOT}/bin/sfml-graphics-d-2.dll"
    "${SFML_ROOT}/bin/sfml-audio-d-2.dll"
  )

  set(EXISTING_SFML_RUNTIME_DLLS)
  foreach(dll IN LISTS SFML_RUNTIME_DLLS)
    if(EXISTS "${dll}")
      list(APPEND EXISTING_SFML_RUNTIME_DLLS "${dll}")
    endif()
  endforeach()

  if(EXISTING_SFML_RUNTIME_DLLS)
    add_custom_command(TARGET WorldSimulation POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${EXISTING_SFML_RUNTIME_DLLS}
        "$<TARGET_FILE_DIR:WorldSimulation>"
      COMMAND_EXPAND_LISTS
    )
  endif()
endif()
