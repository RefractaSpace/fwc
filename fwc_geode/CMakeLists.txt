cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(FwcWindowCounter VERSION 1.0.0)

add_library(${PROJECT_NAME} SHARED src/main.cpp)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK 환경 변수가 없어요. build.bat 안내대로 지오드 SDK를 먼저 설치하세요.")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# 지메 2.209x(지오드 6) 용 코드 고르기
if (DEFINED GEODE_GD_VERSION AND GEODE_GD_VERSION VERSION_GREATER_EQUAL 2.209)
    target_compile_definitions(${PROJECT_NAME} PRIVATE FWC_GD2209)
    message(STATUS "FWC: GD ${GEODE_GD_VERSION} 용 코드")
endif()

setup_geode_mod(${PROJECT_NAME})
