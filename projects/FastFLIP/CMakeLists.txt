cmake_minimum_required(VERSION 3.16)
add_definitions(-D__TBB_CPP20_COMPARISONS_PRESENT=0)  

if (NOT ZENO_WITH_zenvdb)
    message(FATAL_ERROR "zenvdb must be ON when FastFLIP is ON! Please specify -DZENO_WITH_zenvdb:BOOL=ON")
endif()

####################################################
# FUN FACT: DID U KNOW THAT MICROSOFT LOVES LINUX? #
####################################################
if (MSVC)  # must use this to make msvc linker happy
    target_compile_options(zeno PRIVATE
            $<$<CONFIG:>:$<IF:$<COMPILE_LANGUAGE:CXX>, /bigobj, >>
            $<$<CONFIG:Debug>:$<IF:$<COMPILE_LANGUAGE:CXX>, /bigobj, >>
            $<$<CONFIG:Release>:$<IF:$<COMPILE_LANGUAGE:CXX>, /bigobj, >>
    )  # 节数超过对象文件格式限制: 请使用 /bigobj 进行编译 
endif()
####################################################
# FUN FACT: DID U KNOW THAT MICROSOFT BUYS GITHUB? #
####################################################

# add cmake file
#include (common)

find_package(TBB REQUIRED COMPONENTS tbb tbbmalloc)
#find_package(IlmBase REQUIRED COMPONENTS Half REQUIRED)
find_package(Eigen3 REQUIRED)
# if (WIN32)
#   include_directories(C:/Users/archibate/packages/openvdb_x64-windows/include/)
# endif()
find_package(OpenMP)
if (OPENMP_FOUND)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
    if(WIN32)
        target_compile_options(zeno # MSVC, GNU, Clang, Intel
            PUBLIC $<$<COMPILE_LANGUAGE:CXX>: /openmp:llvm /openmp:experimental>
        )
    else()
        target_compile_options(zeno # MSVC, GNU, Clang, Intel
            PUBLIC $<$<COMPILE_LANGUAGE:CXX>: $<IF:$<CXX_COMPILER_ID:AppleClang>, -Xpreprocessor -fopenmp, -fopenmp> >
        )
    endif()
endif()

set(FLIP_SOURCE levelset_util.cpp FLIP_vdb.h FLIP_vdb.cpp simd_vdb_poisson.h simd_vdb_poisson.cpp simd_vdb_poisson_uaamg.h simd_vdb_poisson_uaamg.cpp simd_viscosity3d.h simd_viscosity3d.cpp vdb_velocity_extrapolator.h vdb_velocity_extrapolator.cpp volume_fractions.h volume_fractions.cpp whitewater.cpp)
file(GLOB NOSYS_SOURCE nosys/*.cpp nosys/*.h)

target_sources(zeno PRIVATE ${FLIP_SOURCE} ${NOSYS_SOURCE})

#simd compiler flags
if (MSVC)
    target_compile_options(zeno PRIVATE $<IF:$<COMPILE_LANGUAGE:CXX>, /arch:AVX, >)
else()
    target_compile_options(zeno PRIVATE $<IF:$<COMPILE_LANGUAGE:CXX>, -mavx -mfma, >)
endif()


target_include_directories(zeno PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(zeno PRIVATE TBB::tbb TBB::tbbmalloc)
if (UNIX)
    target_link_libraries(zeno PRIVATE pthread)
endif()

#disable default malloc options
if (WIN32)
  target_compile_options(zeno PRIVATE -DNOMINMAX -D_USE_MATH_DEFINES)
endif()
# if (UNIX)
#   target_link_directories(zeno PRIVATE /usr/lib/x86_64-linux-gnu:/usr/local/lib)
# endif()
#target_link_libraries(zeno PRIVATE OpenVDB::openvdb)
target_link_libraries(zeno PRIVATE Eigen3::Eigen)

add_subdirectory(partio)
# IF (NOT WIN32)
#     INSTALL(TARGETS partio DESTINATION lib)
# ENDIF()
target_link_libraries(zeno PRIVATE partio)


target_include_directories(zeno PRIVATE ../oldzenbase/include)
target_include_directories(zeno PRIVATE ../zenvdb/include)
