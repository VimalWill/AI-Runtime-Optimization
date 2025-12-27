cmake_minimum_required(VERSION 3.15)
project(RuntimeProject C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER clang++-20)

# Generate assembly + keep normal build products
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -fvectorize -march=native -flto -O3 -march=native -mtune=native -fno-exceptions -fno-rtti -fopenmp-simd")

include_directories(include)

find_package(LLVM REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
#pkg_check_modules(GPERFTOOLS REQUIRED libtcmalloc)
include_directories(${LLVM_INCLUDE_DIR})

add_executable(runtime
    main.cpp
)

#target_include_directories(runtime PRIVATE ${GPERFTOOLS_INCLUDE_DIRS})
#target_link_directories(runtime PRIVATE ${GPERFTOOLS_LIBRARY_DIRS})

target_link_libraries(runtime 
    PRIVATE 
    LLVMSupport
 #   ${GPERFTOOLS_LIBRARIES}
)

