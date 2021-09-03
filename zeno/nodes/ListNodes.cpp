#include <zeno/zeno.h>
#include <zeno/types/ListObject.h>
#include <zeno/types/NumericObject.h>
#include <zeno/utils/string.h>
#include <sstream>

namespace zeno {

struct ListLength : zeno::INode {
    virtual void apply() override {
        auto list = get_input<zeno::ListObject>("list");
        auto ret = std::make_shared<zeno::NumericObject>();
        ret->set<int>(list->arr.size());
        set_output("length", std::move(ret));
    }
};

ZENDEFNODE(ListLength, {
    {"list"},
    {"length"},
    {},
    {"list"},
});


struct ListGetItem : zeno::INode {
    virtual void apply() override {
        auto list = get_input<zeno::ListObject>("list");
        auto index = get_input<zeno::NumericObject>("index")->get<int>();
        auto obj = list->arr.at(index);
        set_output2("object", std::move(obj));
    }
};

ZENDEFNODE(ListGetItem, {
    {"list", {"int", "index"}},
    {"object"},
    {},
    {"list"},
});

struct ExtractList : zeno::INode {
    virtual void apply() override {
        auto inkeys = get_param<std::string>("_KEYS");
        auto keys = zeno::split_str(inkeys, '\n');
        auto list = get_input<zeno::ListObject>("list");
        for (auto const& key : keys) {
            int index = std::stoi(key);
            if (list->arr.size() > index) {
                auto obj = list->arr[index];
                set_output2(key, std::move(obj));
            }
        }
    }
};

ZENDEFNODE(ExtractList, {
    {"list"},
    {},
    {},
    {"list"},
    });

struct EmptyList : zeno::INode {
    virtual void apply() override {
        auto list = std::make_shared<zeno::ListObject>();
        set_output("list", std::move(list));
    }
};

ZENDEFNODE(EmptyList, {
    {},
    {"list"},
    {},
    {"list"},
});


struct AppendList : zeno::INode {
    virtual void apply() override {
        auto list = get_input<zeno::ListObject>("list");
        auto obj = get_input("object");
        list->arr.push_back(std::move(obj));
        set_output("list", get_input("list"));
    }
};

ZENDEFNODE(AppendList, {
    {"list", "object"},
    {"list"},
    {},
    {"list"},
});

struct ExtendList : zeno::INode {
    virtual void apply() override {
        auto list1 = get_input<zeno::ListObject>("list1");
        auto list2 = get_input<zeno::ListObject>("list2");
        for (auto const &ptr: list2->arr) {
            list1->arr.push_back(ptr);
        }
        set_output("list1", std::move(list1));
    }
};

ZENDEFNODE(ExtendList, {
    {"list1", "list2"},
    {"list1"},
    {},
    {"list"},
});


struct ResizeList : zeno::INode {
    virtual void apply() override {
        auto list = get_input<zeno::ListObject>("list");
        auto newSize = get_input<zeno::NumericObject>("newSize")->get<int>();
        list->arr.resize(newSize);
        set_output("list", std::move(list));
    }
};

ZENDEFNODE(ResizeList, {
    {"list", {"int", "newSize"}},
    {"list"},
    {},
    {"list"},
});


struct MakeSmallList : zeno::INode {
    virtual void apply() override {
        auto list = std::make_shared<zeno::ListObject>();
        for (int i = 0; i < 6; i++) {
            std::stringstream namess;
            namess << "obj" << i;
            auto name = namess.str();
            if (!has_input(name)) break;
            auto obj = get_input(name);
            list->arr.push_back(std::move(obj));
        }
        set_output("list", std::move(list));
    }
};

ZENDEFNODE(MakeSmallList, {
    {"obj0", "obj1", "obj2", "obj3", "obj4", "obj5"},
    {"list"},
    {},
    {"list"},
});

struct MakeList : zeno::INode {
    virtual void apply() override {
        auto list = std::make_shared<zeno::ListObject>();

        int max_input_index = 0;
        for (auto& pair : inputs) {
            if (std::isdigit(pair.first.back())) {
                max_input_index = std::max<int>(max_input_index, std::stoi(pair.first.substr(3)));
            }
        }
        for (int i = 0; i <= max_input_index; ++i) {
            std::stringstream namess;
            namess << "obj" << i;
            auto name = namess.str();
            if (!has_input(name)) continue;
            auto obj = get_input(name);
            list->arr.push_back(std::move(obj));
        }
        set_output("list", std::move(list));
    }
};

ZENDEFNODE(MakeList, {
    {},
    {"list"},
    {},
    {"list"},
    });


#ifdef ZENO_VISUALIZATION
struct dumpfile_ListObject : zeno::INode {
    virtual void apply() override {
        auto list = get_input<ListObject>("overload_0");
        auto path = get_param<std::string>("path");
        for (int i = 0; i < list->arr.size(); i++) {
            auto const &obj = list->arr[i];
            std::stringstream ss;
            ss << path << "." << i;
            if (auto o = silent_any_cast<std::shared_ptr<IObject>>(obj); o.has_value()) {
                auto node = graph->scene->sess->getOverloadNode("dumpfile", {o.value()});
                node->inputs["path:"] = ss.str();
                node->doApply();
            }
        }
    }
};

ZENO_DEFOVERLOADNODE(dumpfile, _ListObject, typeid(ListObject).name())({});
#endif

}
