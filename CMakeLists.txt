cmake_minimum_required(VERSION 3.20)

if(COMMAND zephyr_library)
  zephyr_include_directories(include)

  if(CONFIG_RT_DIAGNOSTICS)
    zephyr_library()
    zephyr_library_include_directories(include)
    zephyr_library_sources(src/diagnostics.c)
  endif()
else()
  project(rt_diagnostics VERSION 0.1.0 LANGUAGES C)

  option(RT_DIAGNOSTICS_DETAILED "Enable stage and interval collection" ON)
  add_library(rt_diagnostics src/diagnostics.c)
  target_include_directories(rt_diagnostics PUBLIC include)
  target_compile_features(rt_diagnostics PUBLIC c_std_11)
  target_compile_definitions(rt_diagnostics PUBLIC
    CONFIG_RT_DIAGNOSTICS=1
    CONFIG_RT_DIAGNOSTICS_DETAILED_TIMING=$<BOOL:${RT_DIAGNOSTICS_DETAILED}>)
endif()
