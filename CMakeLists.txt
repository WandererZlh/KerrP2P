cmake_minimum_required(VERSION 3.17)
project(KerrP2P)

set(CMAKE_CXX_STANDARD 17)
# remove FindBoost module from cmake
cmake_policy(SET CMP0167 NEW)

option(ENABLE_TESTING "Enable Test Builds" ON)
option(FLOAT128_NATIVE "Enable float128 native support quadmath" ON)
option(ENABLE_MPFR "Enable mpfr support" OFF)
option(ENABLE_EXAMPLES "Enable Examples" ON)

if (WIN32)
    SET(FLOAT128_NATIVE OFF)
    SET(ENABLE_MPFR OFF)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    add_definitions(-DNOMINMAX)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    SET(FLOAT128_NATIVE OFF)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(fmt CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(Boost 1.82.0 COMPONENTS filesystem REQUIRED)
set(LIBRARIES Boost::boost Boost::filesystem fmt::fmt Eigen3::Eigen TBB::tbb TBB::tbbmalloc)

# add_definitions(-DPRINT_DEBUG)

if (FLOAT128_NATIVE)
    message("Enable float128 precision")
    set(LIBRARIES ${LIBRARIES} quadmath)
    add_definitions(-DFLOAT128_NATIVE)
endif()

if (ENABLE_MPFR)
    message("Enable bigfloat precision")
    find_package(GMP REQUIRED)
    find_package(MPFR REQUIRED)
    find_package(MPC REQUIRED)
    include_directories(${GMP_INCLUDES} ${MPFR_INCLUDES} ${MPC_INCLUDES})
    set(LIBRARIES ${LIBRARIES} ${GMP_LIBRARIES} ${MPFR_LIBRARIES} ${MPC_LIBRARIES})
    add_definitions(-DENABLE_MPFR)
endif ()

include_directories(${PROJECT_SOURCE_DIR}/src)

set(SOURCE_FILES src/Common.h src/ForwardRayTracing.h src/GIntegral.h src/IIntegral2.h src/IIntegral3.h src/ObjectPool.h src/Utils.h src/Integral.h src/Broyden.h)

#add_executable(KerrP2P src/Main.cpp ${SOURCE_FILES})
#target_link_libraries(KerrP2P PRIVATE Boost::program_options ${LIBRARIES})

# build examples
if (ENABLE_EXAMPLES)
    add_executable(cpp_tutorial_basic examples/cpp_tutorial_basic.cpp ${SOURCE_FILES})
    target_link_libraries(cpp_tutorial_basic PRIVATE ${LIBRARIES})

    add_executable(cpp_tutorial_sweep examples/cpp_tutorial_sweep.cpp ${SOURCE_FILES})
    target_link_libraries(cpp_tutorial_sweep PRIVATE ${LIBRARIES})
endif()

if (ENABLE_TESTING)
find_package(Catch2 3 REQUIRED)

add_executable(tests tests/Test.cpp
        tests/TestData.h
        tests/TestData.cpp
        ${SOURCE_FILES}
        tests/Main.cpp
)
target_compile_definitions(tests PRIVATE TESTS)
target_link_libraries(tests PRIVATE ${LIBRARIES} Catch2::Catch2)

# https://github.com/catchorg/Catch2/issues/2382
# include(CTest)
# include(Catch)
# catch_discover_tests(tests EXTRA_ARGS "--data_path" ${CMAKE_CURRENT_SOURCE_DIR}/tests/data)
endif()

find_package(Python REQUIRED COMPONENTS Interpreter Development)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(pykerrp2p src/Pybind.cpp ${SOURCE_FILES})
target_link_libraries(pykerrp2p PUBLIC ${LIBRARIES})

# if (WIN32)
# file(GLOB OUTPUT_FILES ${CMAKE_CURRENT_BINARY_DIR}/*.exe ${CMAKE_CURRENT_BINARY_DIR}/*.dll  ${CMAKE_CURRENT_BINARY_DIR}/*.pyd)
# file(ARCHIVE_CREATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/forward_ray_tracing.zip
#     PATHS ${OUTPUT_FILES}
#     FORMAT zip)
# endif()
