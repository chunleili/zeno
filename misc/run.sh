#!/bin/bash
set -e

#
# This is a fking script to make zhxx very happy.
#
# Run 'misc/run.sh' to build and run this smart project in one command to increase jiafangbaba happiness!
#
# For yin ♂ wei ♂ da users, please run 'misc/run.sh ywd' for CUDA support!
#
# HINT: Ninja (a parallel build system) can be installed via either `apt-get install ninja` or `pip install ninja`.
#

cmake -G Ninja -B /tmp/zeno-build -Wno-dev -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/tmp/zeno-dist -DZENO_WITH_ZenoFX:BOOL=ON -DZENOFX_ENABLE_OPENVDB:BOOL=ON -DZENOFX_ENABLE_LBVH:BOOL=ON -DZENO_WITH_zenvdb:BOOL=ON -DZENO_WITH_FastFLIP:BOOL=ON -DZENO_WITH_FEM:BOOL=ON -DZENO_WITH_Rigid:BOOL=ON -DZENO_WITH_cgmesh:BOOL=ON -DZENO_WITH_oldzenbase:BOOL=ON -DZENO_WITH_TreeSketch:BOOL=ON -DZENO_WITH_Skinning:BOOL=ON -DZENO_WITH_Euler:BOOL=ON -DZENO_WITH_Functional:BOOL=ON -DZENO_WITH_LSystem:BOOL=ON -DZENO_WITH_mesher:BOOL=ON -DZENO_WITH_Alembic:BOOL=ON -DZENO_WITH_FBX:BOOL=ON -DZENO_WITH_DemBones:BOOL=ON -DZENO_WITH_SampleModel:BOOL=ON -DZENO_WITH_CalcGeometryUV:BOOL=ON -DZENO_WITH_MeshSubdiv:BOOL=ON -DZENO_WITH_Audio:BOOL=ON -DZENO_WITH_PBD:BOOL=ON -DZENO_WITH_GUI:BOOL=ON -DZENO_WITH_ImgCV:BOOL=ON -DZENO_WITH_TOOL_FLIPtools:BOOL=ON -DZENO_WITH_TOOL_cgmeshTools:BOOL=ON -DZENO_WITH_TOOL_BulletTools:BOOL=ON -DZENO_WITH_TOOL_HerculesTools:BOOL=ON -DZENO_WITH_Python:BOOL=ON ${1+-DZENO_WITH_CUDA:BOOL=ON -DZENO_ENABLE_OPTIX:BOOL=ON}
cmake --build /tmp/zeno-build --parallel
/tmp/zeno-build/bin/zenoedit
