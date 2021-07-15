#include <zeno/zeno.h>
#include <zeno/DictObject.h>
#include <zeno/StringObject.h>
#include <zeno/NumericObject.h>
#include <zeno/safe_at.h>


struct DictSize : zeno::INode {
    virtual void apply() override {
        auto dict = get_input<zeno::DictObject>("dict");
        auto ret = std::make_shared<zeno::NumericObject>();
        ret->set<int>(dict->lut.size());
        set_output("size", std::move(ret));
    }
};

ZENDEFNODE(DictSize, {
    {"dict"},
    {"size"},
    {},
    {"dict"},
});


struct ExtractDict : zeno::INode {
    virtual void apply() override {
        auto dict = get_input<zeno::DictObject>("dict");
        auto key = get_input<zeno::StringObject>("key")->get();
        auto obj = dict->lut.at(key);
        set_output("object", std::move(obj));
    }
};

ZENDEFNODE(ExtractDict, {
    {"dict", "key"},
    {"object"},
    {},
    {"dict"},
});


struct EmptyDict : zeno::INode {
    virtual void apply() override {
        auto dict = std::make_shared<zeno::DictObject>();
        set_output("dict", std::move(dict));
    }
};

ZENDEFNODE(EmptyDict, {
    {},
    {"dict"},
    {},
    {"dict"},
});


struct UpdateDict : zeno::INode {
    virtual void apply() override {
        auto dict = get_input<zeno::DictObject>("dict");
        auto key = get_input<zeno::StringObject>("key")->get();
        auto obj = get_input("object");
        dict->lut[key] = std::move(obj);
        set_output("dict", get_input("dict"));
    }
};

ZENDEFNODE(UpdateDict, {
    {"dict", "key", "object"},
    {"dict"},
    {},
    {"dict"},
});


struct MakeSmallDict : zeno::INode {
    virtual void apply() override {
        auto dict = std::make_shared<zeno::DictObject>();
        for (int i = 0; i < 4; i++) {
            std::stringstream namess;
            namess << "obj" << i;
            auto name = namess.str();
            if (!has_input(name)) break;
            auto obj = get_input(name);
            std::stringstream namess2;
            namess2 << "name" << i;
            name = namess2.str();
            name = get_param<std::string>(name);
            dict->lut[name] = std::move(obj);
        }
        set_output("dict", std::move(dict));
    }
};

ZENDEFNODE(MakeSmallDict, {
    { "obj0"
    , "obj1"
    , "obj2"
    , "obj3"
    },
    {"dict"},
    { {"string", "name0", "obj0"}
    , {"string", "name1", "obj1"}
    , {"string", "name2", "obj2"}
    , {"string", "name3", "obj3"}
    },
    {"dict"},
});


struct ExtractSmallDict : zeno::INode {
    virtual void apply() override {
        auto dict = get_input<zeno::DictObject>("dict");
        for (int i = 0; i < 4; i++) {
            std::stringstream namess;
            namess << "name" << i;
            auto name = namess.str();
            auto key = get_param<std::string>(name);
            if (key.size() == 0) break;
            auto obj = safe_at(dict->lut, key, "ExtractSmallDict key");
            std::stringstream namess2;
            namess2 << "obj" << i;
            name = namess2.str();
            set_output(name, std::move(obj));
        }
        set_output("dict", std::move(dict));
    }
};

ZENDEFNODE(ExtractSmallDict, {
    {"dict"},
    { "obj0"
    , "obj1"
    , "obj2"
    , "obj3"
    },
    { {"string", "name0", ""}
    , {"string", "name1", ""}
    , {"string", "name2", ""}
    , {"string", "name3", ""}
    },
    {"dict"},
});
