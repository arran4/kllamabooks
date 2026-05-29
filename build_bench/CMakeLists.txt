cmake_minimum_required(VERSION 3.16)
project(QueueBench)

set(CMAKE_CXX_STANDARD 17)

find_package(Qt6 COMPONENTS Core Sql Network REQUIRED)

# SQLCipher setup
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLCIPHER REQUIRED sqlcipher)
add_definitions(-DSQLITE_HAS_CODEC)

# Stubs for missing dependencies
file(WRITE ${CMAKE_BINARY_DIR}/stub.cpp "
#include <QString>
class OllamaClient {
public:
    OllamaClient() {}
};
")

add_executable(bench benchmark_queue.cpp src/BookDatabase.cpp src/QueueManager.cpp src/OllamaClient.cpp)
target_link_libraries(bench Qt6::Core Qt6::Sql Qt6::Network ${SQLCIPHER_LIBRARIES})
target_include_directories(bench PRIVATE src ${SQLCIPHER_INCLUDE_DIRS})
