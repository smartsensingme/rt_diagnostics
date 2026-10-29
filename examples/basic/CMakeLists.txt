cmake_minimum_required(VERSION 3.20.0)

set(RT_LOG_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../rt_log" CACHE PATH "Path to rt_log module")
set(RT_DIAGNOSTICS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../rt_diagnostics" CACHE PATH "Path to rt_diagnostics module")

list(APPEND ZEPHYR_EXTRA_MODULES
  "${RT_LOG_DIR}"
  "${RT_DIAGNOSTICS_DIR}"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rt_diagnostics_basic)
target_sources(app PRIVATE src/main.c)
