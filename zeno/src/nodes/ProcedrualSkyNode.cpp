#include <zeno/zeno.h>
#include <zeno/types/PrimitiveObject.h>
#include <zeno/types/UserData.h>

namespace zeno {
struct ProceduralSky : INode {
    virtual void apply() override {
        auto prim = std::make_shared<zeno::PrimitiveObject>();

        prim->userData().set2("isRealTimeObject", std::move(1));
        prim->userData().set2("ProceduralSky", std::move(1));
        prim->userData().set2("sunLightDir", std::move(get_input2<vec2f>("sunLightDir")));
        prim->userData().set2("sunLightSoftness", std::move(get_input2<float>("sunLightSoftness")));
        prim->userData().set2("windDir", std::move(get_input2<vec2f>("windDir")));
        prim->userData().set2("timeStart", std::move(get_input2<float>("timeStart")));
        prim->userData().set2("timeSpeed", std::move(get_input2<float>("timeSpeed")));
        set_output("ProceduralSky", std::move(prim));
    }
};

ZENDEFNODE(ProceduralSky, {
        {
                {"vec2f", "sunLightDir", "-60,45"},
                {"float", "sunLightSoftness", "1"},
                {"vec2f", "windDir", "0,0"},
                {"float", "timeStart", "0"},
                {"float", "timeSpeed", "0.1"},
        },
        {
                {"ProceduralSky"},
        },
        {
        },
        {"shader"},
});

struct HDRSky : INode {
    virtual void apply() override {
        auto prim = std::make_shared<zeno::PrimitiveObject>();
        auto path = get_input2<std::string>("path");
        if (path.empty()) {
            throw std::runtime_error("need hdr tex path");
        }
        prim->userData().set2("isRealTimeObject", std::move(1));
        prim->userData().set2("HDRSky", std::move(path));
        prim->userData().set2("evnTexRotation", std::move(get_input2<float>("rotation")));
        prim->userData().set2("evnTexStrength", std::move(get_input2<float>("strength")));
        set_output("HDRSky", std::move(prim));
    }
};

ZENDEFNODE(HDRSky, {
    {
        {"readpath", "path"},
        {"float", "rotation", "0"},
        {"float", "strength", "1"},
    },
    {
        {"HDRSky"},
    },
    {
    },
    {"shader"},
});
};
