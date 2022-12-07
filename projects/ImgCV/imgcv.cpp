#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <zeno/zeno.h>
#include <zeno/utils/arrayindex.h>
#include <zeno/types/PrimitiveObject.h>
#include <zeno/types/NumericObject.h>
#include <zeno/utils/zeno_p.h>

namespace zeno {

struct CVImageObject : IObjectClone<CVImageObject> {
    cv::Mat image;

    CVImageObject() = default;
    explicit CVImageObject(cv::Mat image) : image(std::move(image)) {}

    CVImageObject(CVImageObject &&) = default;
    CVImageObject &operator=(CVImageObject &&) = default;

    CVImageObject(CVImageObject const &img) : image(img.image.clone()) {
    }

    CVImageObject &operator=(CVImageObject const &img) {
        // notice that cv::Mat is shallow-copy, only .clone() will deep-copy
        image = img.image.clone();
        return *this;
    }
};

namespace {

struct CVINode : INode {
    template <class To = double, class T>
    static auto tocvvec(T const &val) {
        if constexpr (is_vec_n<T> == 4) {
            return cv::Scalar_<To>(val[3], val[2], val[1], val[0]);
        } else if constexpr (is_vec_n<T> == 3) {
            return cv::Scalar_<To>(val[2], val[1], val[0]);
        } else if constexpr (is_vec_n<T> == 2) {
            return cv::Scalar_<To>(val[1], val[0]);
        } else {
            return To(val);
        }
    }

    cv::_InputArray get_input_array(std::string const &name) {
        if (has_input<NumericObject>(name)) {
            auto num = get_input<NumericObject>(name);
            bool is255 = has_input<NumericObject>("is255") && get_input2<bool>("is255");
            return std::visit([&] (auto const &val) -> cv::_InputArray {
                auto ret = tocvvec(val);
                return is255 ? ret * 255 : ret;
            }, num->value);
        } else {
            return get_input<CVImageObject>(name)->image;
        }
    }
};

struct CVImageRead : CVINode {
    void apply() override {
        auto path = get_input2<std::string>("path");
        auto mode = get_input2<std::string>("mode");
        cv::ImreadModes flags = array_lookup({cv::IMREAD_COLOR, cv::IMREAD_GRAYSCALE, cv::IMREAD_UNCHANGED},
            array_index_safe({"COLOR", "GRAYSCALE", "UNCHANGED"}, mode, "mode"));
        auto image = std::make_shared<CVImageObject>(cv::imread(path, flags));
        set_output("image", std::move(image));
    }
};

ZENDEFNODE(CVImageRead, {
    {
        {"readpath", "path", ""},
        {"enum COLOR GRAYSCALE UNCHANGED", "mode", "COLOR"},
    },
    {
        {"CVImageObject", "image"},
    },
    {},
    {"opencv"},
});

struct CVImageShow : CVINode {
    void apply() override {
        auto image = get_input_array("image");
        auto title = get_input2<std::string>("title");
        cv::imshow(title, image);
        if (get_input2<bool>("waitKey"))
            cv::waitKey();
    }
};

ZENDEFNODE(CVImageShow, {
    {
        {"CVImageObject", "image"},
        {"string", "title", "imshow"},
        {"bool", "waitKey", "1"},
    },
    {
    },
    {},
    {"opencv"},
});

struct CVImageAdd : CVINode {
    void apply() override {
        auto image1 = get_input_array("image1");
        auto image2 = get_input_array("image2");
        auto weight1 = get_input2<float>("weight1");
        auto weight2 = get_input2<float>("weight2");
        auto constant = get_input2<float>("constant");
        auto resimage = std::make_shared<CVImageObject>();
        if (weight1 == 1 && weight2 == 1 && constant == 0) {
            cv::add(image1, image2, resimage->image);
        } else {
            cv::addWeighted(image1, weight1, image2, weight2, constant, resimage->image);
        }
        set_output("resimage", std::move(resimage));
    }
};

ZENDEFNODE(CVImageAdd, {
    {
        {"CVImageObject", "image1"},
        {"CVImageObject", "image2"},
        {"float", "weight1", "1"},
        {"float", "weight2", "1"},
        {"float", "constant", "0"},
    },
    {
        {"CVImageObject", "resimage"},
    },
    {},
    {"opencv"},
});

struct CVImageMultiply : CVINode {
    void apply() override {
        auto image1 = get_input_array("image1");
        auto image2 = get_input_array("image2");
        auto is255 = get_input2<bool>("is255");
        auto resimage = std::make_shared<CVImageObject>();
        cv::multiply(image1, image2, resimage->image, is255 ? 1.f / 255.f : 1.f);
        set_output("resimage", std::move(resimage));
    }
};

ZENDEFNODE(CVImageMultiply, {
    {
        {"CVImageObject", "image1"},
        {"CVImageObject", "image2"},
        {"bool", "is255", "1"},
    },
    {
        {"CVImageObject", "resimage"},
    },
    {},
    {"opencv"},
});

struct CVImageBlend : CVINode {
    void apply() override {
        auto image1 = get_input_array("image1");
        auto image2 = get_input_array("image2");
        auto is255 = get_input2<bool>("is255");
        auto resimage = std::make_shared<CVImageObject>();
        if (has_input<NumericObject>("factor")) {
            auto factor = get_input2<float>("factor");
            cv::addWeighted(image1, 1 - factor, image2, factor, 0, resimage->image);
        } else {
            auto factor = get_input_array("factor");
            cv::Mat factorinv, tmp1, tmp2;
            if (is255) {
                cv::bitwise_not(factor, factorinv);
            } else {
                cv::invert(factor, factorinv);
            }
            cv::multiply(image1, factorinv, tmp1, is255 ? 1.f / 255.f : 1.f);
            cv::multiply(image2, factor, tmp2, is255 ? 1.f / 255.f : 1.f);
            cv::add(tmp1, tmp2, resimage->image);
        }
        set_output("resimage", std::move(resimage));
    }
};

ZENDEFNODE(CVImageBlend, {
    {
        {"CVImageObject", "image1"},
        {"CVImageObject", "image2"},
        {"float", "factor", "0.5"},
        {"bool", "is255", "1"},
    },
    {
        {"CVImageObject", "resimage"},
    },
    {},
    {"opencv"},
});

struct CVImageInvert : CVINode {
    void apply() override {
        auto image = get_input_array("image");
        auto is255 = get_input2<bool>("is255");
        auto resimage = std::make_shared<CVImageObject>();
        if (is255) {
            cv::bitwise_not(image, resimage->image);
        } else {
            cv::invert(image, resimage->image);
        }
        set_output("resimage", std::move(resimage));
    }
};

ZENDEFNODE(CVImageInvert, {
    {
        {"CVImageObject", "image"},
        {"bool", "is255", "1"},
    },
    {
        {"CVImageObject", "resimage"},
    },
    {},
    {"opencv"},
});

struct CVImageCvtColor : CVINode {
    void apply() override {
        auto image = get_input_array("image");
        auto mode = get_input2<std::string>("mode");
        cv::ColorConversionCodes code = array_lookup({
            cv::COLOR_BGR2GRAY,
            cv::COLOR_GRAY2BGR,
            cv::COLOR_BGR2RGB,
            cv::COLOR_BGR2BGRA,
            cv::COLOR_BGRA2BGR,
            cv::COLOR_BGR2HSV,
            cv::COLOR_HSV2BGR,
        }, array_index_safe({
            "BGR2GRAY",
            "GRAY2BGR",
            "BGR2RGB",
            "BGR2BGRA",
            "BGRA2BGR",
            "BGR2HSV",
            "HSV2BGR",
        }, mode, "mode"));
        auto resimage = std::make_shared<CVImageObject>();
        cv::cvtColor(image, resimage->image, code);
        set_output("resimage", std::move(resimage));
    }
};

ZENDEFNODE(CVImageCvtColor, {
    {
        {"CVImageObject", "image"},
        {
            "enum "
            "BGR2GRAY "
            "GRAY2BGR "
            "BGR2RGB "
            "BGR2BGRA "
            "BGRA2BGR "
            "BGR2HSV "
            "HSV2BGR "
            , "mode", "BGR2GRAY"},
    },
    {
        {"CVImageObject", "resimage"},
    },
    {},
    {"opencv"},
});

struct CVImageGrayscale : CVINode {
    void apply() override {
        auto image = get_input_array("image");
        auto resimage = std::make_shared<CVImageObject>();
        cv::Mat tmp;
        cv::cvtColor(image, tmp, cv::COLOR_BGR2GRAY);
        cv::cvtColor(tmp, resimage->image, cv::COLOR_GRAY2BGR);
        set_output("resimage", std::move(resimage));
    }
};

ZENDEFNODE(CVImageGrayscale, {
    {
        {"CVImageObject", "image"},
    },
    {
        {"CVImageObject", "resimage"},
    },
    {},
    {"opencv"},
});

struct CVImageMonoColor : CVINode {
    void apply() override {
        auto likeimage = get_input<CVImageObject>("likeimage");
        auto is255 = get_input2<bool>("is255");
        auto color = tocvvec<float>(get_input2<vec3f>("color"));
        auto image = std::make_shared<CVImageObject>(likeimage->image.clone());
        if (is255) {
            cv::Point3_<unsigned char> cval;
            cval.x = (unsigned char)std::clamp(color[0] * 255.f, 0.f, 255.f);
            cval.y = (unsigned char)std::clamp(color[1] * 255.f, 0.f, 255.f);
            cval.z = (unsigned char)std::clamp(color[2] * 255.f, 0.f, 255.f);
            image->image.setTo(cv::Scalar_<unsigned char>(cval.x, cval.y, cval.z));
        } else {
            image->image.setTo(cv::Scalar(color[0], color[1], color[2]));
        }
        set_output("image", std::move(image));
    }
};

ZENDEFNODE(CVImageMonoColor, {
    {
        {"CVImageObject", "likeimage"},
        {"bool", "is255", "1"},
        {"vec3f", "color", "1,1,1"},
    },
    {
        {"CVImageObject", "image"},
    },
    {},
    {"opencv"},
});

struct CVImageGradColor : CVINode {
    void apply() override {
        auto likeimage = get_input<CVImageObject>("likeimage"); // TODO: if no likeimage, create Mat::zeros from custom shape
        auto angle = get_input2<float>("angle");
        auto scale = get_input2<float>("scale");
        auto offset = get_input2<float>("offset");
        auto is255 = get_input2<bool>("is255");
        auto color1 = tocvvec<float>(get_input2<vec3f>("color1"));
        auto color2 = tocvvec<float>(get_input2<vec3f>("color2"));
        auto image = std::make_shared<CVImageObject>(likeimage->image.clone());
        vec2i shape(image->image.size[1], image->image.size[0]);
        vec2f invshape = 1.f / shape;
        angle *= (std::atan(1.f) * 4) / 180;
        vec2f dir(std::cos(angle), std::sin(angle));
        auto invscale = 0.5f / scale;
        auto neoffset = 0.5f - (offset * 2 - 1) * invscale;
        if (is255) {
            image->image.forEach<cv::Point3_<unsigned char>>([&] (cv::Point3_<unsigned char> &val, const int *pos) {
                vec2i posv(pos[1], pos[0]);
                float f = dot(posv * invshape * 2 - 1, dir) * invscale + neoffset, omf = 1 - f;
                val.x = (unsigned char)std::clamp((omf * color1[0] + f * color2[2]) * 255.f, 0.f, 255.f);
                val.y = (unsigned char)std::clamp((omf * color1[1] + f * color2[2]) * 255.f, 0.f, 255.f);
                val.z = (unsigned char)std::clamp((omf * color1[2] + f * color2[2]) * 255.f, 0.f, 255.f);
            });
        } else {
            image->image.forEach<cv::Point3_<float>>([&] (cv::Point3_<float> &val, const int *pos) {
                vec2i posv(pos[1], pos[0]);
                float f = dot(posv * invshape * 2 - 1, dir) * invscale + neoffset, omf = 1 - f;
                val.x = omf * color1[0] + f * color2[2];
                val.y = omf * color1[1] + f * color2[2];
                val.z = omf * color1[2] + f * color2[2];
            });
        }
        set_output("image", std::move(image));
    }
};

ZENDEFNODE(CVImageGradColor, {
    {
        {"CVImageObject", "likeimage"},
        {"float", "angle", "0"},     // rotation clock-wise
        {"float", "scale", "1"},     // thickness of gradient
        {"float", "offset", "0.5"},  // 0 to 1
        {"bool", "is255", "1"},
        {"vec3f", "color1", "0,0,0"},
        {"vec3f", "color2", "1,1,1"},
    },
    {
        {"CVImageObject", "image"},
    },
    {},
    {"opencv"},
});

struct CVImageDrawPoly : CVINode {
    void apply() override {
        auto image = get_input<CVImageObject>("image");
        auto color = tocvvec<float>(get_input2<vec3f>("color"));
        if (!get_input2<bool>("inplace"))
            image = std::make_shared<CVImageObject>(*image);
        auto prim = get_input<PrimitiveObject>("prim");
        auto linewidth = get_input2<int>("linewidth");
        auto batched = get_input2<bool>("batched");
        auto antialias = get_input2<bool>("antialias");
        auto is255 = get_input2<bool>("is255");
        if (is255) color *= 255.f;
            //image->image.setTo(cv::Scalar::all(0));
        vec2i shape(image->image.size[1], image->image.size[0]);

        std::vector<std::vector<cv::Point>> vpts(prim->polys.size());
        for (int i = 0; i < prim->polys.size(); i++) {
            auto [base, len] = prim->polys[i];
            auto &pt = vpts[i];
            pt.resize(len);
            for (int k = 0; k < len; k++) {
                auto v = prim->verts[prim->loops[base + k]];
                pt[k].x = int((v[0] * 0.5f + 0.5f) * shape[0]);
                pt[k].y = int((v[1] * -0.5f + 0.5f) * shape[1]);
            }
        }
        std::vector<const cv::Point *> pts(vpts.size());
        std::vector<int> npts(vpts.size());
        for (int i = 0; i < vpts.size(); i++) {
            pts[i] = vpts[i].data();
            npts[i] = vpts[i].size();
        }

        cv::LineTypes linemode = antialias ? cv::LINE_AA : cv::LINE_4;
        if (linewidth > 0) {
            if (batched) {
                cv::polylines(image->image, pts.data(), npts.data(), npts.size(), 0, color, linewidth, linemode);
            } else {
                for (int i = 0; i < npts.size(); i++) {
                    cv::polylines(image->image, pts.data() + i, npts.data() + i, 1, 0, color, linewidth, linemode);
                }
            }
        } else {
            if (batched) {
                cv::fillPoly(image->image, pts.data(), npts.data(), npts.size(), color, linemode);
            } else {
                for (int i = 0; i < npts.size(); i++) {
                    cv::fillPoly(image->image, pts.data() + i, npts.data() + i, 1, color, linemode);
                }
            }
        }
        set_output("image", std::move(image));
    }
};

ZENDEFNODE(CVImageDrawPoly, {
    {
        {"CVImageObject", "image"},
        {"PrimitiveObject", "prim"},
        {"vec3f", "color", "1,1,1"},
        {"PrimitiveObject", "points"},
        {"int", "linewidth", "0"},
        {"bool", "inplace", "0"},
        {"bool", "batched", "0"},
        {"bool", "antialias", "0"},
        {"bool", "is255", "1"},
    },
    {
        {"CVImageObject", "image"},
    },
    {},
    {"opencv"},
});

}

}
