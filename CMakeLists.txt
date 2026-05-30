cmake_minimum_required(VERSION 3.23)

project(cmx_blog_mcp VERSION 0.1.0 LANGUAGES CXX)

option(CMX_BLOG_MCP_FETCH_CXXMCP "Fetch cxxmcp from GitHub when no package is installed" ON)
option(CMX_BLOG_MCP_ENABLE_HTTP "Enable cxxmcp Streamable HTTP transport" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8)
endif()

if(CMX_BLOG_MCP_FETCH_CXXMCP)
  include(FetchContent)
  set(CXXMCP_BUILD_CLIENT OFF CACHE BOOL "" FORCE)
  set(CXXMCP_BUILD_SERVER ON CACHE BOOL "" FORCE)
  set(CXXMCP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CXXMCP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(CXXMCP_ENABLE_HTTP ${CMX_BLOG_MCP_ENABLE_HTTP} CACHE BOOL "" FORCE)
  FetchContent_Declare(
    cxxmcp
    GIT_REPOSITORY https://github.com/caomengxuan666/cxxmcp.git
    GIT_TAG a9fd5ed0d817bdb52e2e19ee301bdec4636027f7
  )
  FetchContent_MakeAvailable(cxxmcp)
else()
  find_package(cxxmcp CONFIG REQUIRED)
endif()

add_executable(cmx-blog-mcp
  src/main.cpp
)

target_compile_features(cmx-blog-mcp PRIVATE cxx_std_20)

if(TARGET cxxmcp::sdk)
  target_link_libraries(cmx-blog-mcp PRIVATE cxxmcp::sdk)
elseif(TARGET mcp_sdk)
  target_link_libraries(cmx-blog-mcp PRIVATE mcp_sdk)
elseif(TARGET mcp_server)
  target_link_libraries(cmx-blog-mcp PRIVATE mcp_server)
else()
  message(FATAL_ERROR "Could not find a cxxmcp SDK/server target")
endif()

if(MSVC)
  target_compile_definitions(cmx-blog-mcp PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(cmx-blog-mcp PRIVATE /W4)
else()
  target_compile_options(cmx-blog-mcp PRIVATE -Wall -Wextra -Wpedantic)
endif()
