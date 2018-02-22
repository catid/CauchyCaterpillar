cmake_minimum_required(VERSION 3.5)
project(ccat)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
else()
    set(CMAKE_CXX_FLAGS "-Wall -Wextra")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -march=native")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")
endif()

set(CMAKE_CXX_STANDARD 11)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Dependency: GF256 library source files
set(GF256_LIB_SRCFILES
        gf256.cpp
        gf256.h)

# Dependency: PacketAllocator library source files
set(PKTALLOC_LIB_SRCFILES
        PacketAllocator.cpp
        PacketAllocator.h)

# CCat library source files
set(CCAT_LIB_SRCFILES
        ccat.cpp
        ccat.h
        CCatCodec.cpp
        CCatCodec.h
        Counter.h)

# CCat unit tester
set(CCAT_TEST_SRCFILES
        CCatCpp.h
        tests/Counter.h
        tests/Logger.cpp
        tests/Logger.h
        tests/SiameseTools.cpp
        tests/SiameseTools.h
        tests/StrikeRegister.cpp
        tests/StrikeRegister.h
        tests/Tester.cpp)

add_library(gf256 ${GF256_LIB_SRCFILES})
add_library(pktalloc ${PKTALLOC_LIB_SRCFILES})
add_library(ccat ${CCAT_LIB_SRCFILES})

add_executable(unit_test ${CCAT_TEST_SRCFILES})
target_link_libraries(unit_test gf256 pktalloc ccat Threads::Threads)
