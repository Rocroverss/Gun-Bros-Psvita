cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Please define VITASDK env var to point to your SDK path or set CMAKE_TOOLCHAIN_FILE.")
endif ()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")

project(so_loader C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

# Vita application configuration
set(VITA_APP_NAME "Gun Bros")
set(VITA_TITLEID "SLDR00000")
set(VITA_VPKNAME "so_loader")
set(VITA_VERSION "01.00")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

# Optional: used for "send" and "dump" targets
set(PSVITAIP "192.168.0.198" CACHE STRING "PSVita IP (for FTP access)")
set(PARSECOREPATH "$ENV{PARSECOREPATH}" CACHE STRING "vita-parse-core main.py")

# Game-specific definitions
set(DATA_PATH "ux0:data/gunbros/gunbros_free/" CACHE STRING "Path to data (with trailing /)")
set(SO_PATH "${DATA_PATH}libandroidplatformjni.so" CACHE STRING "Path to .so")
set(GUNBROS_LOGOS_PATH "ux0:data/gunbros/logos/" CACHE STRING "Shared version logos directory")
add_definitions(-DGUNBROS_LOGOS_PATH="${GUNBROS_LOGOS_PATH}")
add_definitions(-DDATA_PATH="${DATA_PATH}" -DSO_PATH="${SO_PATH}")

option(USE_SCELIBC_IO "Use SceLibcBridge for IO and some other std functions" ON)
if (USE_SCELIBC_IO)
  add_definitions(-DUSE_SCELIBC_IO)
endif()

# Runtime diagnostics are excluded from the shipping port. They cross the Vita
# debug transport and can stall menu/audio frames; fixed values also override
# stale CMake-cache settings from older diagnostic builds.
set(GUNBROS_ENABLE_LOGS OFF)
set(GUNBROS_ENABLE_PERF_TRACE OFF)
if (GUNBROS_ENABLE_PERF_TRACE)
  add_definitions(-DGUNBROS_ENABLE_PERF_TRACE=1)
endif()

set(SHADER_FORMAT "GLSL" CACHE STRING "Preferred shader format (one of 'GLSL', 'CG', 'GXP')")
if (${SHADER_FORMAT} STREQUAL "GLSL")
  add_definitions(-DUSE_GLSL_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "CG")
  add_definitions(-DUSE_CG_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "GXP")
  add_definitions(-DUSE_GXP_SHADERS)
else()
  message(FATAL_ERROR "SHADER_FORMAT option can only be one of: 'GLSL', 'CG', 'GXP'")
endif()

set(valid_dump_formats GLSL CG)
option(DUMP_COMPILED_SHADERS "When using GLSL or CG shader format, cache compiled shaders on disk" ON)
if (DUMP_COMPILED_SHADERS)
  if (SHADER_FORMAT IN_LIST valid_dump_formats)
    add_definitions(-DDUMP_COMPILED_SHADERS)
  else()
    message(WARNING "DUMP_COMPILED_SHADERS has no effect for the chosen shader format ${SHADER_FORMAT}")
  endif()
endif()

# Unique debug definition for loader logging. A Debug build remains silent
# unless runtime logging was explicitly requested as well.
if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND GUNBROS_ENABLE_LOGS)
  add_definitions(-DDEBUG_SOLOADER)
  # Uncomment this for verbose FalsoJNI logging:
  #add_definitions(-DFALSOJNI_DEBUGLEVEL=0)
endif()

# makes sincos, sincosf, etc. visible
add_definitions(-D_GNU_SOURCE -D__POSIX_VISIBLE=999999)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu11 -Wno-deprecated")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu++20 -Wno-write-strings -Wno-psabi")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O1 -g3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1 -g3")
else ()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -g0 -ffast-math")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g0 -ffast-math")
endif()


include_directories(
    ${CMAKE_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/lib/falso_jni
    ${CMAKE_SOURCE_DIR}/lib/so_util
)

add_executable(${CMAKE_PROJECT_NAME}
               source/main.c
               source/audio.cpp
               source/dynlib.c
               source/java.c
               source/patch.c
               source/reimpl/bits/_ctype.c
               source/reimpl/controls.c
               source/reimpl/controls.h
               source/reimpl/egl.c
               source/reimpl/errno.c
               source/reimpl/io.c
               source/reimpl/log.c
               source/reimpl/mem.c
               source/reimpl/pthr.c
               source/reimpl/sys.c
               source/reimpl/time64.c
               source/reimpl/asset_manager.cpp
               source/utils/dialog.c
               source/utils/classic_dialog.c
               source/utils/data_check.c
               source/utils/glutil.c
               source/utils/init.c
               source/utils/logger.c
               source/utils/settings.c
               source/utils/utils.c
               
               lib/falso_jni/jni.h
               lib/falso_jni/FalsoJNI.c
               lib/falso_jni/FalsoJNI_ImplBridge.c
               lib/falso_jni/FalsoJNI_Logger.c
               lib/falso_jni/FalsoRocro.c
               lib/falso_jni/converter.c
               lib/sha1/sha1.c
               lib/fios/fios.c
               lib/so_util/so_util.c)

# The VitaSDK SoLoud package is built without exported RTTI for its base
# AudioSource classes. This file derives the original Gun Bros PCM pull source
# from those classes but does not use dynamic_cast or typeid, so match the
# library ABI locally instead of disabling RTTI for the rest of the loader.
set_source_files_properties(source/audio.cpp PROPERTIES COMPILE_OPTIONS "-fno-rtti")

if (NOT GUNBROS_ENABLE_LOGS)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE GUNBROS_QUIET_LOGS=1)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
                         "-include${CMAKE_SOURCE_DIR}/source/utils/quiet_log.h")
endif()

add_subdirectory(lib/libc_bridge)
add_dependencies(${CMAKE_PROJECT_NAME} SceLibcBridge)

target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM
                           PUBLIC ${CMAKE_SOURCE_DIR}/lib
                           )

target_include_directories(${CMAKE_PROJECT_NAME}
                           PUBLIC ${CMAKE_SOURCE_DIR}/source
                           )

# Recent VitaSDK sndfile builds enable Opus and MP3 support. Older SDKs omit
# these codecs, so only add their static dependencies when installed.
set(GUNBROS_SNDFILE_CODECS "")
foreach(codec opus mpg123 mp3lame)
  find_library(GUNBROS_CODEC_${codec} NAMES ${codec})
  if(GUNBROS_CODEC_${codec})
    list(APPEND GUNBROS_SNDFILE_CODECS ${GUNBROS_CODEC_${codec}})
  endif()
endforeach()

target_link_libraries(${CMAKE_PROJECT_NAME}
                      -Wl,--whole-archive pthread -Wl,--no-whole-archive
                      OpenSLES
                      sndfile
                      vorbis
                      ogg
                      vorbisenc
                      vorbisfile
                      FLAC
                      ${GUNBROS_SNDFILE_CODECS}
                      soloud
                      stdc++
                      vitaGL
                      vitashark
                      SceShaccCgExt
                      taihen_stub
                      SceShaccCg_stub
                      z
                      m
                      ScePgf_stub
                      mathneon
                      kubridge_stub
                      ${CMAKE_BINARY_DIR}/lib/libc_bridge/stubs/libSceLibcBridge_stub.a
                      SceHid_stub
                      SceTouch_stub
                      SceMotion_stub
                      SceAppMgr_stub
                      SceAppUtil_stub
                      SceAudio_stub
                      SceAudioIn_stub
                      SceCommonDialog_stub
                      SceDisplay_stub
                      SceKernelDmacMgr_stub
                      SceFios2_stub
                      SceCtrl_stub
                      SceGxm_stub
                      ScePower_stub
                      SceTouch_stub
                      SceVshBridge_stub
                      )

# --- Post-build: copy raw ELF for parsing ---
add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.elf
    COMMENT "Copying raw ELF for parsing output"
)

add_executable(gunbros_launcher source/launcher/main.c)
target_link_libraries(gunbros_launcher vita2d freetype png jpeg z m
    SceAppMgr_stub SceCtrl_stub SceTouch_stub SceDisplay_stub SceGxm_stub
    SceSysmodule_stub ScePgf_stub ScePvf_stub SceCommonDialog_stub)
vita_create_self(classic.bin ${CMAKE_PROJECT_NAME} UNSAFE NOASLR)
vita_create_self(eboot.bin gunbros_launcher UNSAFE)
vita_create_vpk(${VITA_VPKNAME}.vpk ${VITA_TITLEID} eboot.bin
                VERSION ${VITA_VERSION}
                NAME ${VITA_APP_NAME}
                FILE ${CMAKE_SOURCE_DIR}/extras/cpuinfo cpuinfo
                ${CMAKE_BINARY_DIR}/classic.bin classic.bin
                ${CMAKE_SOURCE_DIR}/extras/meminfo meminfo
                ${CMAKE_SOURCE_DIR}/extras/urandom urandom
                ${CMAKE_SOURCE_DIR}/extras/livearea/icon0.png sce_sys/icon0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/pic0.png sce_sys/pic0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/startup.png sce_sys/livearea/contents/startup.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/bg0.png sce_sys/livearea/contents/bg0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/template.xml sce_sys/livearea/contents/template.xml
                )
add_dependencies(${VITA_VPKNAME}.vpk-vpk classic.bin-self)

add_custom_target(send
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND curl -T classic.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND curl --ftp-create-dirs -T ${CMAKE_SOURCE_DIR}/logos/classic.png ftp://${PSVITAIP}:1337/${GUNBROS_LOGOS_PATH}
                  COMMAND curl --ftp-create-dirs -T ${CMAKE_SOURCE_DIR}/logos/reloaded.png ftp://${PSVITAIP}:1337/${GUNBROS_LOGOS_PATH}
                  COMMAND curl --ftp-create-dirs -T ${CMAKE_SOURCE_DIR}/logos/info_logo.png ftp://${PSVITAIP}:1337/${GUNBROS_LOGOS_PATH}
                  COMMAND curl --ftp-create-dirs -T "${CMAKE_SOURCE_DIR}/logos/choose your version.png" "ftp://${PSVITAIP}:1337/${GUNBROS_LOGOS_PATH}choose%20your%20version.png"
                  COMMAND curl --ftp-create-dirs -T "${CMAKE_SOURCE_DIR}/logos/selection arrow.png" "ftp://${PSVITAIP}:1337/${GUNBROS_LOGOS_PATH}selection%20arrow.png"
                  COMMAND echo launch ${VITA_TITLEID} | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(send_kvdb
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND curl -T classic.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND curl --ftp-create-dirs -T ${CMAKE_SOURCE_DIR}/logos/classic.png ftp://${PSVITAIP}:1337/${GUNBROS_LOGOS_PATH}
                  COMMAND curl --ftp-create-dirs -T ${CMAKE_SOURCE_DIR}/logos/reloaded.png ftp://${PSVITAIP}:1337/${GUNBROS_LOGOS_PATH}
                  COMMAND curl --ftp-create-dirs -T ${CMAKE_SOURCE_DIR}/logos/info_logo.png ftp://${PSVITAIP}:1337/${GUNBROS_LOGOS_PATH}
                  COMMAND curl --ftp-create-dirs -T "${CMAKE_SOURCE_DIR}/logos/choose your version.png" "ftp://${PSVITAIP}:1337/${GUNBROS_LOGOS_PATH}choose%20your%20version.png"
                  COMMAND curl --ftp-create-dirs -T "${CMAKE_SOURCE_DIR}/logos/selection arrow.png" "ftp://${PSVITAIP}:1337/${GUNBROS_LOGOS_PATH}selection%20arrow.png"
                  COMMAND echo launch ux0:/app/${VITA_TITLEID}/eboot.bin | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(dump
                  COMMAND ${CMAKE_SOURCE_DIR}/extras/scripts/get_dump.sh ${PSVITAIP}
                  COMMAND python3 ${PARSECOREPATH} coredump ${CMAKE_PROJECT_NAME}
                  )

add_custom_target(reboot
                  COMMAND echo reboot | nc ${PSVITAIP} 1338
                  )
