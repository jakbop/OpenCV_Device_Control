# cmake needs this line
cmake_minimum_required(VERSION 3.1)

# Define project name
project(opencv_demo1)

# Find OpenCV, you may need to set OpenCV_DIR variable
# to the absolute path to the directory containing OpenCVConfig.cmake file
# via the command line or GUI
find_package(OpenCV REQUIRED)

# If the package has been found, several variables will
# be set, you can find the full list with descriptions
# in the OpenCVConfig.cmake file.
# Print some message showing some of them
message(STATUS "OpenCV library status:")
message(STATUS "    config: ${OpenCV_DIR}")
message(STATUS "    version: ${OpenCV_VERSION}")
message(STATUS "    libraries: ${OpenCV_LIBS}")
message(STATUS "    include path: ${OpenCV_INCLUDE_DIRS}")
find_library(PAHO_MQTT_LIB NAMES paho-mqtt3c paho-mqtt3a)
if(NOT PAHO_MQTT_LIB)
    message(FATAL_ERROR "paho-mqtt-c library not found. 请安装 paho.mqtt.c 并重试。")
endif()

find_library(MYSQL_CLIENT_LIB NAMES mysqlclient)
if(NOT MYSQL_CLIENT_LIB)
    message(FATAL_ERROR "MySQL client library not found. 请安装 libmysqlclient-dev 并重试。")
endif()

find_path(MYSQL_INCLUDE_DIR NAMES mysql/mysql.h)
if(NOT MYSQL_INCLUDE_DIR)
    message(FATAL_ERROR "MySQL header files not found. 请安装 libmysqlclient-dev 并重试。")
endif()


# Declare the executable target built from your sources
add_executable(demo1 main.cpp)

target_include_directories(demo1 PRIVATE ${MYSQL_INCLUDE_DIR})

# Link your application with OpenCV libraries
target_link_libraries(demo1 PRIVATE ${OpenCV_LIBS} ${PAHO_MQTT_LIB} ${MYSQL_CLIENT_LIB} pthread)
