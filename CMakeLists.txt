cmake_minimum_required(VERSION 3.7)
project(ccat)

set(CMAKE_CXX_STANDARD 11)

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
        CCatCpp.h
        CCatSession.cpp
        CCatSession.h
        CCatTools.cpp
        CCatTools.h
        Counter.h)

# CCat unit tester
set(CCAT_TEST_SRCFILES
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
target_link_libraries(unit_test gf256 pktalloc ccat)
