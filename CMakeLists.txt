cmake_minimum_required(VERSION 3.5.1)

project(openauto CXX)

message(STATUS "OpenAuto")

option(NOPI "Build for Non Raspberry Pi" OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Set Compile Versions
set(OPENAUTO_BUILD_DATE 20241124)       # Binary Release Build Date
set(OPENAUTO_BUILD_MAJOR_RELEASE 4)     # Binary Release Build Major Number
set(OPENAUTO_BUILD_MINOR_RELEASE 1)     # Binary Release Build Minor Number
set(OPENAUTO_BUILD_INCREMENTAL 0)       # Binary Build Version - Increment for Each Build

# Configure CMAKE
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_INIT} -Wall -pedantic -fPIC")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3")

# Default to Release mode unless overridden with -DCMAKE_BUILD_TYPE=Debug on cmake command
if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Forcing Release build type")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,/usr/local/qt5/lib")

    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Paths
set(resources_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(sources_directory ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(include_directory ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake_modules/")

set(AAP_PROTOBUF_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/OpenAutoSDK")
set(AASDK_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/OpenAutoSDK/include/aasdk")


# Configure Boost
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

add_definitions(-DBOOST_ALL_DYN_LINK)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Disabling Boost DEBUG logs")
    add_definitions(-DNDEBUG)
endif()


find_package(Protobuf REQUIRED)


add_subdirectory(OpenAutoSDK/protobuf)
add_subdirectory(OpenAutoSDK)



find_package(Boost REQUIRED COMPONENTS system log_setup log OPTIONAL_COMPONENTS unit_test_framework)
#find_package(libusb-1.0 REQUIRED)
find_package(Qt5 COMPONENTS DBus Multimedia MultimediaWidgets Bluetooth Network)
find_package(OpenSSL REQUIRED)
find_package(taglib REQUIRED)
find_package(blkid REQUIRED)
find_package(gps REQUIRED)

# Add GStreamer, is likely needed for qt5multimedia to work
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GSTAPP REQUIRED gstreamer-app-1.0)

include_directories(${CMAKE_CURRENT_BINARY_DIR}
        ${Qt5Multimedia_INCLUDE_DIRS}
        ${Qt5MultimediaWidgets_INCLUDE_DIRS}
        ${Qt5Widgets_INCLUDE_DIRS}
        ${Qt5Bluetooth_INCLUDE_DIRS}
        ${Qt5Network_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
        ${TAGLIB_INCLUDE_DIRS}
        ${BLKID_INCLUDE_DIRS}
        ${BCM_HOST_INCLUDE_DIRS}
        ${ILCLIENT_INCLUDE_DIRS}
        ${include_directory}
        ${PROTOBUF_INCLUDE_DIR}
        ${AAP_PROTOBUF_INCLUDE_DIR}
        ${AASDK_INCLUDE_DIR})

link_directories(${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

set(common_include_directory ${include_directory}/f1x/openauto/Common)

set(autoapp_sources_directory ${sources_directory}/autoapp)
set(autoapp_include_directory ${include_directory}/f1x/openauto/autoapp)

file(GLOB_RECURSE autoapp_source_files ${autoapp_sources_directory}/*.ui ${autoapp_sources_directory}/*.cpp ${autoapp_include_directory}/*.hpp ${autoapp_include_directory}/*.h ${common_include_directory}/*.hpp ${resources_directory}/*.qrc)

add_executable(autoapp ${autoapp_source_files})

add_dependencies(autoapp aasdk)
target_include_directories(autoapp PUBLIC ${AAP_PROTOBUF_INCLUDE_DIR} ${AASDK_INCLUDE_DIR})

target_link_libraries(autoapp PUBLIC
        libusb
        aasdk
        aap_protobuf
        ${Boost_LIBRARIES}
        ${Qt5DBus_LIBRARIES}
        ${Qt5Multimedia_LIBRARIES}
        ${Qt5MultimediaWidgets_LIBRARIES}
        ${Qt5Bluetooth_LIBRARIES}
        ${Qt5Network_LIBRARIES}
        ${BCM_HOST_LIBRARIES}
        ${ILCLIENT_LIBRARIES}
        ${WINSOCK2_LIBRARIES}
        ${TAGLIB_LIBRARIES}
        ${BLKID_LIBRARIES}
        ${GPS_LIBRARIES}
        ${PROTOBUF_LIBRARIES}
        ${GST_LIBRARIES}      # Link against GStreamer libraries
        ${GSTAPP_LIBRARIES})   # Link against GStreamer App libraries

set(PROGRAM_VERSION_STRING "${OPENAUTO_BUILD_MAJOR_RELEASE}.${OPENAUTO_BUILD_MINOR_RELEASE}.${OPENAUTO_BUILD_INCREMENTAL}+${OPENAUTO_BUILD_DATE}")

message(STATUS "Project Version: ${PROGRAM_VERSION_STRING}")

set(btservice_sources_directory ${sources_directory}/btservice)

set(btservice_include_directory ${include_directory}/f1x/openauto/btservice)
file(GLOB_RECURSE btservice_source_files ${btservice_sources_directory}/*.cpp ${btservice_include_directory}/*.hpp ${autoapp_sources_directory}/Configuration/*.cpp ${autoapp_includes_directory}/Configuration/*.hpp ${common_include_directory}/*.hpp)

add_executable(btservice ${btservice_source_files}
        src/btservice/BluetoothHandler.cpp)

target_link_libraries(btservice PUBLIC
        aap_protobuf
        ${Boost_LIBRARIES}
        ${Qt5Bluetooth_LIBRARIES}
        ${Qt5Network_LIBRARIES}
        ${Qt5MultimediaWidgets_LIBRARIES}
        ${PROTOBUF_LIBRARIES})

set_target_properties(autoapp
        PROPERTIES VERSION ${PROGRAM_VERSION_STRING} SOVERSION ${OPENAUTO_BUILD_INCREMENTAL})

install(TARGETS autoapp
        RUNTIME DESTINATION bin)
