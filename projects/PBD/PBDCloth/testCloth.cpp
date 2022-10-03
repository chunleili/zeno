#include<iostream>
#include<vector>
#include<zeno/zeno.h>
#include <zeno/types/PrimitiveObject.h>
#include <zeno/types/NumericObject.h>

using namespace zeno;

/**
 * @brief this node just for test, do not use in production!
 * 
 */
struct testCloth : zeno::INode
{
    void init(
        AttrVector<zeno::vec3f> &pos,
        std::vector<vec3f> &vel,
        int nx,
        int ny,
        float dx,
        float dy)
    {
        pos.resize(nx*ny);
        vel.resize(nx*ny);

        for (int i = 0; i < nx; i++)
            for (int j = 0; j < ny; j++)
            {
                pos[i * nx + j][0] = i * dx - 0.5;
                pos[i * nx + j][1] = 0.6;
                pos[i * nx + j][2] = j * dy - 0.5;
            }
    }

    inline int ij2i(int i, int j, int nx)
    {
        return i*nx + j;
    }

	virtual void apply() override 
    {
        auto prim = std::make_shared<PrimitiveObject>();
        auto & pos = prim->verts;
        std::vector<vec3f> vel;

        auto nx = get_input<NumericObject>("nx")->get<int>();
        auto ny = get_input<NumericObject>("ny")->get<int>();
        auto dx = get_input<NumericObject>("dx")->get<float>();
        auto dy = get_input<NumericObject>("dy")->get<float>();
        // int nx=128;
        // int ny =128;
        // float dx = 1.0/nx;
        // float dy = 1.0/ny;

        init(pos,vel,nx,ny,dx, dy);

        set_output("pos", std::move(prim));
        set_output("nx", std::make_shared<NumericObject>(nx));
        set_output("ny", std::make_shared<NumericObject>(ny));
        set_output("dx", std::make_shared<NumericObject>(dx));
        set_output("dy", std::make_shared<NumericObject>(dy));
    }
};
ZENDEFNODE(testCloth, {
    {
        {"int","nx","128"},
        {"int","ny","128"},
        {"float","dx","0.0078125"},
        {"float","dy","0.0078125"}
    },
    {   
        {"pos"},
        {"int","nx"},
        {"int","ny"},
        {"float","dx"},
        {"float","dy"}
    },
    {},
    {"PBD"},
});