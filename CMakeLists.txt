cmake_minimum_required(VERSION 3.10)
project(WordCounter LANGUAGES CXX)

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable multithreading
find_package(Threads REQUIRED)

# Source files
file(GLOB SRC_FILES src/*.cpp)

# Define executable
add_executable(wordcount ${SRC_FILES})

# Link pthreads
target_link_libraries(wordcount PRIVATE Threads::Threads)
