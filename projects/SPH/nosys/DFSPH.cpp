#include <iostream>
#include <zeno/zeno.h>
#include <zeno/types/PrimitiveObject.h>

#include "SPlisHSPlasH/DFSPH/TimeStepDFSPH.h"
#include "SPlisHSPlasH/Common.h"
#include "Simulator/SimulatorBase.h"
#include "Simulator/GUI/OpenGL/Simulator_OpenGL.h"
#include "PositionBasedDynamicsWrapper/PBDBoundarySimulator.h"

using namespace SPH;
using namespace std;

SimulatorBase *base = nullptr;
Simulator_GUI_Base *gui = nullptr;

namespace zeno
{
struct DFSPH : INode
{
    
    virtual void apply() override
    {
        std::cout<<"DFSPH!!!!!!!!!!!!!!!!!!!!!!!!"<<std::endl;
        std::cout<<"DFSPH!!!!!!!!!!!!!!!!!!!!!!!!"<<std::endl;
        std::cout<<"DFSPH!!!!!!!!!!!!!!!!!!!!!!!!"<<std::endl;
        std::cout<<"DFSPH!!!!!!!!!!!!!!!!!!!!!!!!"<<std::endl;

        base = new SimulatorBase();
        int argc = 1;
        const char** argv = "SPlisHSPlasH";
        base->init(argc, argv, "SPlisHSPlasH");
        base->run();

        delete base;
    }
};
    


ZENDEFNODE(DFSPH, {   
                    {
                        {"PrimitiveObject", "prim"},
                        {"float", "dx", "2.51"},
                        {"vec3f", "bounds_max", "40, 40, 40"},
                        {"vec3f", "bounds_min", "0,0,0"},
                        {"int", "numSubsteps", "5"},
                        {"float", "particle_radius", "3.0"},
                        {"float", "dt", "0.05"},
                        {"vec3f", "gravity", "0, -10, 0"},
                        {"float", "mass", "1.0"},
                        {"float", "rho0", "1.0"},
                    },
                    {   {"PrimitiveObject", "outPrim"} },
                    {},
                    {"SPH"},
                });

}//namespace zeno