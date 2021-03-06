cmake_minimum_required(VERSION 2.8)

set(VE_OPENMP true CACHE BOOL "VE-OPENMP: Build the OpenMP code generator.")
if(NOT VE_OPENMP)
    return()
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/include)

file(GLOB SRC *.cpp)

add_library(bh_ve_openmp SHARED ${SRC})

target_link_libraries(bh_ve_openmp bh)

install(TARGETS bh_ve_openmp DESTINATION ${LIBDIR} COMPONENT bohrium)

#
# The rest of the this file is finding the compiler and flags to write in the config file
#
include(CheckCCompilerFlag)
include(FeatureSummary)

# Import and detect OpenMP features
find_package(OpenMP)
set_package_properties(OpenMP PROPERTIES TYPE RECOMMENDED PURPOSE "Multicore processing, essential for performance of the CPU VE.")

# Check OpenMP SIMD support
if(OPENMP_FOUND OR OpenMP_C_FOUND)
    # Check for the SIMD flag
    set(OpenMP_SIMD_C_FLAGS "${OpenMP_C_FLAGS} ${OpenMP_C_FLAGS}-simd")
    check_c_compiler_flag("${OpenMP_SIMD_C_FLAGS}" OPENMP_SIMD_FLAG_FOUND)
    if(OPENMP_SIMD_FLAG_FOUND)
        # Check for SIMD support
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
        check_c_source_compiles("
        #include <omp.h>
        int main() {
          int i;
          int sum=42;
          #pragma omp parallel for simd reduction(+:sum)
          for(i=0; i<100; ++i)
            sum += i;
          return 0;
        }
        " OPENMP_SIMD_FOUND)
        unset(CMAKE_REQUIRED_FLAGS)
    endif()
endif()

# Check highly RECOMMENDED flags
check_c_compiler_flag(-O3 FLAG_03_FOUND)
check_c_compiler_flag(-march=native FLAG_MARCH_NATIVE_FOUND)
check_c_compiler_flag(-Werror FLAG_WERROR_FOUND)

# JIT-compiler capabilities: optimization, and parallelization
if(APPLE)
    set(VE_OPENMP_COMPILER_FLG "-x c -dynamiclib -arch x86_64 -Wno-expansion-to-defined")
else()
    set(VE_OPENMP_COMPILER_FLG "-x c -fPIC -shared ${C99_FLAG}")
endif()

# Optimizations
if (FLAG_03_FOUND)
    set(VE_OPENMP_COMPILER_FLG "${VE_OPENMP_COMPILER_FLG} -O3")

    # With Clang, -O3 gives a warning about not being able to vectorize loops
    # TODO: Figure out if we can actually have clang vectorize the loops
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(VE_OPENMP_COMPILER_FLG "${VE_OPENMP_COMPILER_FLG} -Wno-pass-failed")
    endif()
endif()
if (FLAG_MARCH_NATIVE_FOUND)
    set(VE_OPENMP_COMPILER_FLG "${VE_OPENMP_COMPILER_FLG} -march=native")
endif()
if (FLAG_WERROR_FOUND)
    set(VE_OPENMP_COMPILER_FLG "${VE_OPENMP_COMPILER_FLG} -Werror")
endif()

# Do the user want OpenMP?
set(VE_OPENMP_COMPILER_OPENMP      ${OPENMP_FOUND}          CACHE BOOL   "VE_OPENMP: JIT-Compiler use OpenMP")
set(VE_OPENMP_COMPILER_OPENMP_SIMD ${OPENMP_SIMD_FOUND}     CACHE BOOL   "VE_OPENMP: JIT-Compiler use OpenMP-SIMD")

# Let's set the openmp-simd flag if it is supported and wanted
if(VE_OPENMP_COMPILER_OPENMP_SIMD)
    set(OpenMP_C_FLAGS "${OpenMP_SIMD_C_FLAGS}")
endif()

# Parallelization
if(VE_OPENMP_COMPILER_OPENMP)
    set(VE_OPENMP_COMPILER_FLG "${VE_OPENMP_COMPILER_FLG} ${OpenMP_C_FLAGS}")
endif()

# Let the user overwrite the compile command
set(VE_OPENMP_COMPILER_CMD "${CMAKE_C_COMPILER}"                             CACHE STRING "JIT-Compiler")
set(VE_OPENMP_COMPILER_FLG "${VE_OPENMP_COMPILER_FLG}"                       CACHE STRING "JIT-Compiler flags")
set(VE_OPENMP_COMPILER_INC "-I${CMAKE_INSTALL_PREFIX}/share/bohrium/include" PARENT_SCOPE)

# We need to cleanup the variables for the config file
if(VE_OPENMP_COMPILER_OPENMP)
    set(_VE_OPENMP_COMPILER_OPENMP "true" CACHE INTERNAL "config version")
else()
    set(_VE_OPENMP_COMPILER_OPENMP "false" CACHE INTERNAL "config version")
endif()
if(VE_OPENMP_COMPILER_OPENMP_SIMD)
    set(_VE_OPENMP_COMPILER_OPENMP_SIMD "true" CACHE INTERNAL "config version")
else()
    set(_VE_OPENMP_COMPILER_OPENMP_SIMD "false" CACHE INTERNAL "config version")
endif()
