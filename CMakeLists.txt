# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_BOARD pico CACHE STRING "Board type")
if(NOT PICO_SDK_PATH)
  set(PICO_SDK_PATH $ENV{PICO_SDK_PATH} CACHE STRING "Pico SDK path")
endif()
set(LOG 0 CACHE STRING "Log level")
option(PROFILE "Enable profiling" NO)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(${PICO_SDK_PATH}/lib/tinyusb/hw/bsp/family_support.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.5.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.5.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(PROJECT "usb_sound_with_pico")

project(${PROJECT})

# Initialise the Raspberry Pi Pico SDK
# pico_sdk_init()
family_initialize_project(${PROJECT} ${CMAKE_CURRENT_LIST_DIR})

# Add executable. Default name is the project name, version 0.1
add_executable(${PROJECT})

pico_set_program_name(${PROJECT} ${PROJECT})
pico_set_program_version(${PROJECT} "0.1")

pico_enable_stdio_uart(${PROJECT} 0)
pico_enable_stdio_usb(${PROJECT} 0)

pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_LIST_DIR}/pio/audio_i2s_32_out.pio)
pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_LIST_DIR}/pio/audio_i2s_32_in.pio)
pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_LIST_DIR}/pio/spdif_out.pio)
pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_LIST_DIR}/pio/spdif_in.pio)
pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_LIST_DIR}/pio/pulse_out.pio)

# Example source
target_sources(${PROJECT} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/device_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/usb_descriptors.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/streaming.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/converter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mixer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/spdifdefs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/job_queue.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/debug.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/profiler.cpp
)

# Add the standard include files to the build
target_include_directories(${PROJECT} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/src
)

# Add the standard library to the build
target_link_libraries(${PROJECT}
        PRIVATE pico_stdlib
        PRIVATE pico_multicore)
# Add any user requested libraries
target_link_libraries(${PROJECT}
        PRIVATE hardware_dma
        PRIVATE hardware_pio
        PRIVATE hardware_pwm
        PRIVATE hardware_interp)

# pico_add_extra_outputs(usbdac)
family_configure_device_example(${PROJECT} noos)

target_compile_options(${PROJECT} PUBLIC
  -Wno-unused-variable 
  -Wno-unused-but-set-variable
  -Wno-unused-function
  -Wno-unused-parameter
  -Wno-suggest-attribute=format
  -Wno-type-limits
  -Wno-conversion
  -Wno-sign-compare
  -Wno-shadow
  -Wno-cast-qual
  -Wno-cast-align
  -Wno-redundant-decls
  )

if(PROFILE)
  target_compile_options(${PROJECT} PUBLIC -DPROFILE=1)
endif()