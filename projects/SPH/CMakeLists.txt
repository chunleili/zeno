option(SPH_LIBS_ONLY "Build SPH lib only, no demo" OFF)


add_subdirectory(SPlisHSPlasH)
target_link_libraries(zeno PRIVATE SPlisHSPlasH)

# # 使用ExternalProject可以解决target冲突问题
# include(ExternalProject)
# set_property(DIRECTORY PROPERTY EP_BASE ${CMAKE_BINARY_DIR}/ExternalProject)

# set(EXT_INALL_DIR ${CMAKE_BINARY_DIR}/ExternalProject/Install)
# message(STATUS "EXT_INALL_DIR: ${EXT_INALL_DIR}")

# ExternalProject_Add(Ext_SPlisHSPlasH
#   SOURCE_DIR
#     ${CMAKE_CURRENT_LIST_DIR}/SPlisHSPlasH
#   CMAKE_ARGS
#     -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
#     -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
#     -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
#     -DCMAKE_INSTALL_PREFIX:PATH=${EXT_INALL_DIR}/SPlisHSPlasH
#     -DEIGEN3_INCLUDE_DIR:PATH=${EIGEN3_INCLUDE_DIR}

# )

# ExternalProject_Get_Property(Ext_SPlisHSPlasH INSTALL_DIR)

# set(SPlisHSPlasH_INCLUDE_DIR ${INSTALL_DIR}/include)
# set(SPlisHSPlasH_DEBUG_LIB ${INSTALL_DIR}/lib/${LIB_PREFIX}SPlisHSPlasH_d${LIB_SUFFIX})
# set(SPlisHSPlasH_LIB ${INSTALL_DIR}/lib/${LIB_PREFIX}SPlisHSPlasH${LIB_SUFFIX})
# set(SPlisHSPlasH_LIBRARIES
# 	optimized ${SPlisHSPlasH_LIB}
# 	debug ${SPlisHSPlasH_DEBUG_LIB}
# )
# unset(INSTALL_DIR)

# add_library(Ext_SPlisHSPlasH1 INTERFACE)
# target_link_libraries(Ext_SPlisHSPlasH1 INTERFACE ${SPlisHSPlasH_LIBRARIES})
# target_include_directories(Ext_SPlisHSPlasH1 INTERFACE ${SPlisHSPlasH_INCLUDE_DIR})
# target_link_libraries(zeno PRIVATE Ext_SPlisHSPlasH1)


if(USE_PBD AND USE_zenoPBD AND USE_SPH)
    message(FATAL_ERROR "Now the SPH and zenoPBD are mutually exclusive, please choose one of them.")
endif()

target_include_directories(zeno
PRIVATE
    SPlisHSPlasH
)

target_sources(zeno PRIVATE
    nosys/DFSPH.cpp
)
