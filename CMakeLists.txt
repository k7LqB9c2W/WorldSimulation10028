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

set(CORE_SOURCES
  src/country.cpp
  src/map.cpp
  src/news.cpp
  src/technology.cpp
  src/culture.cpp
  src/great_people.cpp
  src/resource.cpp
  src/trade.cpp
  src/economy.cpp
  src/domestic_packages.cpp
  src/settlement_system.cpp
  src/simulation_runner.cpp
  src/simulation_context.cpp
)

set(GUI_SOURCES
  src/main.cpp
  src/renderer.cpp
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/imgui_stdlib.cpp
  third_party/imgui-sfml/imgui-SFML.cpp
)

add_executable(WorldSimulation ${CORE_SOURCES} ${GUI_SOURCES})

add_executable(worldsim_cli
  src/cli_main.cpp
  ${CORE_SOURCES}
)

target_include_directories(WorldSimulation PRIVATE
  include
  third_party/imgui
  third_party/imgui-sfml
  third_party/tomlplusplus/include
)

target_include_directories(worldsim_cli PRIVATE
  include
  third_party/tomlplusplus/include
)

if(MSVC)
  target_compile_options(WorldSimulation PRIVATE /W3 /EHsc)
  target_compile_options(worldsim_cli PRIVATE /W3 /EHsc)
endif()

if(WIN32)
  target_link_libraries(WorldSimulation PRIVATE
    sfml-graphics
    sfml-window
    sfml-system
    sfml-audio
    opengl32
  )
  target_link_libraries(worldsim_cli PRIVATE
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
  target_link_libraries(worldsim_cli PRIVATE
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
set_target_properties(worldsim_cli PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

include(CTest)
if(BUILD_TESTING)
  add_executable(food_accounting_test tests/food_accounting.cpp ${CORE_SOURCES})
  target_include_directories(food_accounting_test PRIVATE include third_party/tomlplusplus/include)
  get_target_property(WORLDSIM_TEST_LIBRARIES worldsim_cli LINK_LIBRARIES)
  target_link_libraries(food_accounting_test PRIVATE ${WORLDSIM_TEST_LIBRARIES})
  if(MSVC)
    target_compile_options(food_accounting_test PRIVATE /W3 /EHsc)
  endif()
  set_target_properties(food_accounting_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  add_dependencies(food_accounting_test worldsim_cli)
  add_test(NAME food_accounting COMMAND food_accounting_test)
  set_tests_properties(food_accounting PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# Asset-only GIS updates must also ship when no C++ target needs relinking.
add_custom_target(stage_world_assets ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:WorldSimulation>/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:WorldSimulation>/data"
  DEPENDS WorldSimulation worldsim_cli
)

add_custom_command(TARGET WorldSimulation POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:WorldSimulation>/assets"
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:WorldSimulation>/data"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/data/sim_config.toml"
    "$<TARGET_FILE_DIR:WorldSimulation>/data/sim_config.toml"
)
add_custom_command(TARGET worldsim_cli POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:worldsim_cli>/assets"
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:worldsim_cli>/data"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/data/sim_config.toml"
    "$<TARGET_FILE_DIR:worldsim_cli>/data/sim_config.toml"
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
    add_custom_command(TARGET worldsim_cli POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${EXISTING_SFML_RUNTIME_DLLS}
        "$<TARGET_FILE_DIR:worldsim_cli>"
      COMMAND_EXPAND_LISTS
    )
  endif()
endif()
