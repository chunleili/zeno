if(NOT ZENO_WITH_ZenoFX)
  message(FATAL_ERROR "ZenoFX must be ON when USE_legacyPBD and ZENO_WITH_PBD is ON! "
    "Please specify: -DZENO_WITH_ZenoFX:BOOL=ON")
endif()

add_subdirectory(Cloth)
add_subdirectory(Fluid)

target_sources(zeno PRIVATE
${CMAKE_CURRENT_LIST_DIR}/Soft/PBDSoftBody.cpp
${CMAKE_CURRENT_LIST_DIR}/Soft/PBDSoftBodyInit.cpp
${CMAKE_CURRENT_LIST_DIR}/Soft/BunnyMesh/BunnyMesh.cpp
${CMAKE_CURRENT_LIST_DIR}/Soft/ReadVtkTet/ReadVtkTet.cpp
)

target_sources(zeno PRIVATE
${CMAKE_CURRENT_LIST_DIR}/Constraints/PBDSolveDistanceConstraint.cpp
${CMAKE_CURRENT_LIST_DIR}/Constraints/PBDSolveVolumeConstraint.cpp
)

target_include_directories(zeno PRIVATE .)


option(PBD_TEST "Build the PBD test" OFF)
if(PBD_TEST)
    add_subdirectory(tests)
endif()
