set(FILENAME "Julia.cpp")
cmake_minimum_required (VERSION 2.8)
get_filename_component(PROJECT ${FILENAME} NAME_WE)
message(STATUS "Project name is ${PROJECT}")

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.10" CACHE STRING "Minimum OS X deployment version")

project (${PROJECT})

include_directories(${SC_PATH}/include/plugin_interface)
include_directories(${SC_PATH}/include/common)
include_directories(${SC_PATH}/common)

#JULIA INCLUDES
include_directories(${JULIA_BUILD_PATH}/include/julia)
###############

set(CMAKE_SHARED_MODULE_PREFIX "")
if(APPLE OR WIN32)
set(CMAKE_SHARED_MODULE_SUFFIX ".scx")
endif()

option(SUPERNOVA "Build plugins for supernova" OFF)
if (SUPERNOVA)
    include_directories(${SC_PATH}/external_libraries/nova-tt)
    # actually just boost.atomic
    include_directories(${SC_PATH}/external_libraries/boost)
    include_directories(${SC_PATH}/external_libraries/boost_lockfree)
    include_directories(${SC_PATH}/external_libraries/boost-lockfree)
endif()

option(CPP11 "Build with c++11." ON)
set (CMAKE_CXX_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	set(CMAKE_COMPILER_IS_CLANG 1)
endif()

#Should I disable all these C flags for generic builds???
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_COMPILER_IS_CLANG)
    add_definitions(-fvisibility=hidden)

    include (CheckCCompilerFlag)
    include (CheckCXXCompilerFlag)

    CHECK_C_COMPILER_FLAG(-msse HAS_SSE)
    CHECK_CXX_COMPILER_FLAG(-msse HAS_CXX_SSE)

    if (HAS_SSE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse")
    endif()
    if (HAS_CXX_SSE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse")
    endif()

    CHECK_C_COMPILER_FLAG(-msse2 HAS_SSE2)
    CHECK_CXX_COMPILER_FLAG(-msse2 HAS_CXX_SSE2)

    if (HAS_SSE2)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2")
    endif()
    if (HAS_CXX_SSE2)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")
    endif()

    CHECK_C_COMPILER_FLAG(-mfpmath=sse HAS_FPMATH_SSE)
    CHECK_CXX_COMPILER_FLAG(-mfpmath=sse HAS_CXX_FPMATH_SSE)

    if (HAS_FPMATH_SSE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpmath=sse")
    endif()
    if (HAS_CXX_FPMATH_SSE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpmath=sse")
    endif()

    if(NATIVE)
        message(STATUS "NATIVE : ON")
        add_definitions(-march=native)
    else()
        #x86-64 architectures only for generic builds... Should add support for more architectures.
        message(STATUS "NATIVE : OFF")
        add_definitions(-march=x86-64)
    endif()

    if(CPP11)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
        if(CMAKE_COMPILER_IS_CLANG)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
        endif()
    endif()
endif()
if(MINGW)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mstackrealign")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mstackrealign")
endif()

#Julia.scx/.so. Building it from Julia.cpp and all other .cpp files
add_library(${PROJECT} MODULE "src/${FILENAME}" "src/GlobalVariables.cpp" "src/JuliaAtomicBarrier.cpp" "src/JuliaGlobalIdDict.cpp" "src/JuliaGlobalState.cpp" "src/JuliaObjectCompiler.cpp" "src/JuliaObjectsArray.cpp" "src/JuliaReplyCmds.cpp" "src/SC_AllocPoolSafe.cpp")

#JULIA LINKER FLAGS.
#-L'${JULIA_BUILD_PATH}/lib' needed to find functions at linking time. -Wl,-rpath to find them at runtime.
#-L'${JULIA_BUILD_PATH}/lib' is in the deps directory, where julia has been built.
#-Wl,-rpath  is in the built folder, and it must look for "scide_lib" (instead of just "lib") because of a SC problem in loading .so files that are not SC plugins on Linux. 
#-ldl needed for <dlfcn.h>, to run dlopen() on libjulia.so on Linux. It is to solve a Julia bug. Refer to: https://discourse.julialang.org/t/different-behaviours-in-linux-and-macos-with-julia-embedded-in-c/18101
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(${PROJECT} "-L'${JULIA_BUILD_PATH}/lib' -Wl,-rpath,'@loader_path/julia/scide_lib' -Wl,-rpath,'@loader_path/julia/scide_lib/julia' -ljulia")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    target_link_libraries(${PROJECT} "-L'${JULIA_BUILD_PATH}/lib' -Wl,--export-dynamic -Wl,-rpath,'$ORIGIN/julia/scide_lib' -Wl,-rpath,'$ORIGIN/julia/scide_lib/julia' -ljulia -ldl")
endif()
###########################################

if(SUPERNOVA)
    add_library(${PROJECT}_supernova MODULE ${FILENAME})
    set_property(TARGET ${PROJECT}_supernova
                 PROPERTY COMPILE_DEFINITIONS SUPERNOVA)
endif()
