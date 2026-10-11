cmake_minimum_required(VERSION 3.24)
project(CCSEngine LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "MemoryScout is a Windows-only application.")
endif()

add_executable(CCSEngine WIN32
  src/main.cpp
  src/MemoryScanner.cpp
)

target_compile_features(CCSEngine PRIVATE cxx_std_17)
target_compile_definitions(CCSEngine PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(CCSEngine PRIVATE comctl32 dwmapi uxtheme shell32)

if(MSVC)
  target_compile_options(CCSEngine PRIVATE /W4 /permissive- /EHsc /utf-8)
  set_property(TARGET CCSEngine PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set_target_properties(CCSEngine PROPERTIES
  OUTPUT_NAME "CCS engine"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Publish"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/Publish"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/Publish"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/Publish"
)

