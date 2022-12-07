#include <zeno/funcs/PrimitiveTools.h>
#include <zenovis/Camera.h>
#include <zenovis/Scene.h>
#include <zenovis/bate/IGraphic.h>
#include <zenovis/ShaderManager.h>
#include <zenovis/ObjectsManager.h>
#include <zenovis/opengl/buffer.h>
#include <zenovis/opengl/shader.h>
#include <zenovis/opengl/texture.h>
#include <zenovis/opengl/vao.h>

#include <unordered_map>
#include <fstream>
#include <random>

namespace zenovis {
namespace {

using opengl::FBO;
using opengl::VAO;
using opengl::Buffer;
using opengl ::Texture;
using opengl::Program;

using zeno::vec2i;
using zeno::vec3i;
using zeno::vec3f;
using zeno::PrimitiveObject;

using std::unique_ptr;
using std::make_unique;
using std::string;
using std::vector;
using std::unordered_map;
using std::unordered_set;

static const char * obj_vert_code = R"(
    # version 330
    layout (location = 0) in vec3 position;

    uniform mat4 mVP;
    uniform mat4 mInvVP;
    uniform mat4 mView;
    uniform mat4 mProj;
    uniform mat4 mInvView;
    uniform mat4 mInvProj;

    void main()
    {
        gl_Position = mVP * vec4(position, 1.0);
    }
)";

static const char * obj_frag_code = R"(
    # version 330
    out uvec3 FragColor;

    uniform uint gObjectIndex;

    void main()
    {
        FragColor = uvec3(gObjectIndex, 0, 0);
    }
)";

static const char * vert_vert_code = R"(
    # version 330
    layout (location = 0) in vec3 position;
    flat out uint gVertexIndex;

    uniform mat4 mVP;
    uniform mat4 mInvVP;
    uniform mat4 mView;
    uniform mat4 mProj;
    uniform mat4 mInvView;
    uniform mat4 mInvProj;

    uniform sampler2D depthTexture;

    void main()
    {
        gVertexIndex = uint(gl_VertexID);
        gl_Position = mVP * vec4(position, 1.0);
    }
)";

static const char* vert_frag_code = R"(
    # version 330
    flat in uint gVertexIndex;
    out uvec3 FragColor;

    uniform uint gObjectIndex;

    void main()
    {
        FragColor = uvec3(gObjectIndex, gVertexIndex + 1u, 0);
    }
)";

static const char* prim_frag_code = R"(
    # version 330
    out uvec3 FragColor;

    uniform uint gObjectIndex;

    void main()
    {
        FragColor = uvec3(gObjectIndex, gl_PrimitiveID + 1, 0);
    }
)";

static const char* empty_frag_code = R"(
    # version 330
    out uvec3 FragColor;

    void main()
    {
        FragColor = uvec3(0, 0, 0);
    }
)";

static const char* empty_and_offset_frag_code = R"(
    # version 330
    out uvec3 FragColor;

    uniform float offset;

    void main()
    {
        gl_FragDepth = gl_FragCoord.z + offset;
        FragColor = uvec3(0, 0, 0);
    }
)";



static void load_buffer_to_image(unsigned int* ids, int w, int h, const std::string& file_name = "output.ppm") {
    unordered_map<unsigned int, vec3i> color_set;
    color_set[0] = {20, 20, 20};
    color_set[1] = {90, 20, 20};
    color_set[1047233823] = {10, 10, 10};

    auto random_color = [](std::default_random_engine &e) -> vec3i{
        std::uniform_int_distribution<int> u(0, 255);
        auto r = u(e);
        auto g = u(e);
        auto b = u(e);
        return {r, g, b};
    };

    unordered_map<unsigned int, int> obj_count;

    std::ofstream os;
    os.open(file_name, std::ios::out);
    os << "P3\n" << w << " " << h << "\n255\n";
    for (int j = h - 1; j >= 0; --j) {
        for (int i = 0; i < w; ++i) {
            auto id = ids[w * j + i];
            vec3i color;
            if (color_set.find(id) != color_set.end()) {
                color = color_set[id];
                obj_count[id]++;
            }
            else {
                printf("found obj id : %u\n", id);
                std::default_random_engine e(id);
                color = random_color(e);
                color_set[id] = color;
            }
            os << color[0] << " " << color[1] << " " << color[2] << "\t";
        }
        os << "\n";
    }
    for (auto [key, value] : obj_count)
        printf("obj id: %u, count: %d, color: (%d, %d, %d)\n", key, value, color_set[key][0],
               color_set[key][1], color_set[key][2]);
    printf("load done.\n");
}

// framebuffer picker referring to https://doc.yonyoucloud.com/doc/wiki/project/modern-opengl-tutorial/tutorial29.html
struct FrameBufferPicker : IPicker {
    Scene* scene;
    vector<string> prim_set;

    unique_ptr<FBO> fbo;
    unique_ptr<Texture> picking_texture;
    unique_ptr<Texture> depth_texture;

    unique_ptr<Buffer> vbo;
    unique_ptr<Buffer> ebo;
    unique_ptr<VAO> vao;

    Program* obj_shader;
    Program* vert_shader;
    Program* prim_shader;
    Program* empty_shader;
    Program* empty_and_offset_shader;

    int w, h;
    unordered_map<unsigned int, string> id_table;

    struct PixelInfo {
        unsigned int obj_id;
        unsigned int elem_id;
        unsigned int blank;

        PixelInfo() {
            obj_id = 0;
            elem_id = 0;
            blank = 0;
        }

        bool has_object() const {
            return obj_id != blank;
        }

        bool has_element() const {
            return elem_id != blank;
        }
    };

    explicit FrameBufferPicker(Scene* s) : scene(s) {
        // generate framebuffer
        fbo = make_unique<FBO>();
        CHECK_GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo->fbo));

        // get viewport size
        w = scene->camera->m_nx;
        h = scene->camera->m_ny;

        // generate picking texture
        picking_texture = make_unique<Texture>();
        CHECK_GL(glBindTexture(picking_texture->target, picking_texture->tex));
        CHECK_GL(glTexImage2D(picking_texture->target, 0, GL_RGB32UI, w, h,
                              0, GL_RGB_INTEGER, GL_UNSIGNED_INT, NULL));
        CHECK_GL(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                        GL_TEXTURE_2D, picking_texture->tex, 0));

        // generate depth texture
        depth_texture = make_unique<Texture>();
        CHECK_GL(glBindTexture(depth_texture->target, depth_texture->tex));
        CHECK_GL(glTexImage2D(depth_texture->target, 0, GL_DEPTH_COMPONENT, w, h,
                              0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL));
        CHECK_GL(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                        GL_TEXTURE_2D, depth_texture->tex, 0));

        // check fbo
        if(!fbo->complete()) printf("fbo error\n");

        // generate draw buffer
        vbo = make_unique<Buffer>(GL_ARRAY_BUFFER);
        ebo = make_unique<Buffer>(GL_ELEMENT_ARRAY_BUFFER);
        vao = make_unique<VAO>();

        // unbind fbo & texture
        CHECK_GL(glBindTexture(GL_TEXTURE_2D, 0));
        fbo->unbind();

        // prepare shaders
        obj_shader = scene->shaderMan->compile_program(obj_vert_code, obj_frag_code);
        vert_shader = scene->shaderMan->compile_program(vert_vert_code, vert_frag_code);
        prim_shader = scene->shaderMan->compile_program(obj_vert_code, prim_frag_code);
        empty_shader = scene->shaderMan->compile_program(obj_vert_code, empty_frag_code);
        empty_and_offset_shader = scene->shaderMan->compile_program(obj_vert_code, empty_and_offset_frag_code);
    }

    ~FrameBufferPicker() {
        if (fbo->fbo) CHECK_GL(glDeleteFramebuffers(1, &fbo->fbo));
        if (picking_texture->tex) CHECK_GL(glDeleteTextures(1, &picking_texture->tex));
        if (depth_texture->tex) CHECK_GL(glDeleteTextures(1, &depth_texture->tex));
    }

    virtual void draw() override {
        // enable framebuffer writing
        CHECK_GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo->fbo));
        CHECK_GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

        // construct prim set
        vector<std::pair<string, PrimitiveObject*>> prims;
        auto prims_shared = scene->objectsMan->pairsShared();
        for (const auto& prim_name : prim_set) {
            PrimitiveObject* prim = nullptr;
            auto optional_prim = scene->objectsMan->get(prim_name);
            if (optional_prim.has_value())
                prim = dynamic_cast<PrimitiveObject*>(scene->objectsMan->get(prim_name).value());
            else {
                auto node_id = prim_name.substr(0, prim_name.find_first_of(':'));
                for (const auto& [n, p] : scene->objectsMan->pairsShared()) {
                    if (n.find(node_id) != std::string::npos) {
                        prim = dynamic_cast<PrimitiveObject*>(p.get());
                        break;
                    }
                }
            }
            prims.emplace_back(std::make_pair(prim_name, prim));
        }
        if (prims.empty()) {
            for (const auto& [prim_name, prim] : prims_shared) {
                auto p = dynamic_cast<PrimitiveObject*>(prim.get());
                prims.emplace_back(std::make_pair(prim_name, p));
            }
        }

        // shading primitive objects
        for (unsigned int id = 0; id < prims.size(); id++) {
            auto it = prims.begin() + id;
            auto prim = it->second;
            if (prim->has_attr("pos")) {
                // prepare vertices data
                auto const &pos = prim->attr<zeno::vec3f>("pos");
                auto vertex_count = prim->size();
                vector<vec3f> mem(vertex_count);
                for (int i = 0; i < vertex_count; i++)
                    mem[i] = pos[i];
                vao->bind();
                vbo->bind_data(mem.data(), mem.size() * sizeof(mem[0]));
                vbo->attribute(0, sizeof(float) * 0, sizeof(float) * 3, GL_FLOAT, 3);

                if (scene->select_mode == zenovis::PICK_OBJECT) {
                    CHECK_GL(glEnable(GL_DEPTH_TEST));
                    // shader uniform
                    obj_shader->use();
                    scene->camera->set_program_uniforms(obj_shader);
                    CHECK_GL(glUniform1ui(glGetUniformLocation(obj_shader->pro, "gObjectIndex"), id + 1));
                    // draw prim
                    ebo->bind_data(prim->tris.data(), prim->tris.size() * sizeof(prim->tris[0]));
                    CHECK_GL(glDrawElements(GL_TRIANGLES, prim->tris.size() * 3, GL_UNSIGNED_INT, 0));
                    ebo->unbind();
                    CHECK_GL(glDisable(GL_DEPTH_TEST));
                }

                if (scene->select_mode == zenovis::PICK_VERTEX) {
                    // ----- enable depth test -----
                    CHECK_GL(glEnable(GL_DEPTH_TEST));
                    CHECK_GL(glDepthFunc(GL_LEQUAL));
                    // CHECK_GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

                    // ----- draw points -----
                    vert_shader->use();
                    scene->camera->set_program_uniforms(vert_shader);
                    CHECK_GL(glUniform1ui(glGetUniformLocation(vert_shader->pro, "gObjectIndex"), id + 1));
                    CHECK_GL(glDrawArrays(GL_POINTS, 0, mem.size()));

                    // ----- draw object to cover invisible points -----
                    empty_and_offset_shader->use();
                    empty_and_offset_shader->set_uniform("offset", 0.001f);
                    scene->camera->set_program_uniforms(empty_and_offset_shader);

                    auto tri_count = prim->tris.size();
                    ebo->bind_data(prim->tris.data(), tri_count * sizeof(prim->tris[0]));
                    CHECK_GL(glDrawElements(GL_TRIANGLES, tri_count * 3, GL_UNSIGNED_INT, 0));
                    ebo->unbind();

                    // ----- disable depth test -----
                    CHECK_GL(glDisable(GL_DEPTH_TEST));
                }

                if (scene->select_mode == zenovis::PICK_LINE) {
                    // ----- enable depth test -----
                    CHECK_GL(glEnable(GL_DEPTH_TEST));
                    CHECK_GL(glDepthFunc(GL_LESS));
                    // CHECK_GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
                    // ----- draw lines -----
                    prim_shader->use();
                    scene->camera->set_program_uniforms(prim_shader);
                    CHECK_GL(glUniform1ui(glGetUniformLocation(prim_shader->pro, "gObjectIndex"), id + 1));
                    auto line_count = prim->lines.size();
                    if (!line_count) {
                        // compute lines' indices
                        struct cmp_line {
                            bool operator()(vec2i v1, vec2i v2) const {
                                return (v1[0] == v2[0] && v1[1] == v2[1]) || (v1[0] == v2[1] && v1[1] == v2[0]);
                            }
                        };
                        struct hash_line {
                            size_t operator()(const vec2i& v) const {
                                return std::hash<int>()(v[0]) ^ std::hash<int>()(v[1]);
                            }
                        };
                        unordered_set<zeno::vec2i, hash_line, cmp_line> lines;
                        for (auto & tri : prim->tris) {
                            auto& a = tri[0];
                            auto& b = tri[1];
                            auto& c = tri[2];
                            lines.insert(vec2i{a, b});
                            lines.insert(vec2i{b, c});
                            lines.insert(vec2i{c, a});
                        }
                        for (auto l : lines) prim->lines.push_back(l);
                        line_count = prim->lines.size();
                    }
                    ebo->bind_data(prim->lines.data(), line_count * sizeof(prim->lines[0]));
                    CHECK_GL(glDrawElements(GL_LINES, line_count * 2, GL_UNSIGNED_INT, 0));
                    ebo->unbind();

                    // ----- draw object to cover invisible lines -----
                    empty_shader->use();
                    scene->camera->set_program_uniforms(empty_shader);
                    auto tri_count = prim->tris.size();
                    ebo->bind_data(prim->tris.data(), tri_count * sizeof(prim->tris[0]));
                    CHECK_GL(glDrawElements(GL_TRIANGLES, tri_count * 3, GL_UNSIGNED_INT, 0));
                    ebo->unbind();
                    // ----- disable depth test -----
                    CHECK_GL(glDisable(GL_DEPTH_TEST));
                }

                if (scene->select_mode == zenovis::PICK_MESH) {
                    // ----- enable depth test -----
                    CHECK_GL(glEnable(GL_DEPTH_TEST));
                    CHECK_GL(glDepthFunc(GL_LESS));
                    // CHECK_GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
                    // ----- draw triangles -----
                    prim_shader->use();
                    scene->camera->set_program_uniforms(prim_shader);
                    CHECK_GL(glUniform1ui(glGetUniformLocation(prim_shader->pro, "gObjectIndex"), id + 1));
                    auto tri_count = prim->tris.size();
                    ebo->bind_data(prim->tris.data(), tri_count * sizeof(prim->tris[0]));
                    CHECK_GL(glDrawElements(GL_TRIANGLES, tri_count * 3, GL_UNSIGNED_INT, 0));
                    ebo->unbind();
                    // ----- disable depth test -----
                    CHECK_GL(glDisable(GL_DEPTH_TEST));
                }

                // unbind vbo
                vbo->disable_attribute(0);
                vbo->unbind();
                vao->unbind();

                // store object's name
                id_table[id + 1] = it->first;
            }
        }
        fbo->unbind();
    }

    virtual string getPicked(int x, int y) override {
        draw();
        if (!fbo->complete()) return "";
        CHECK_GL(glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo->fbo));
        CHECK_GL(glReadBuffer(GL_COLOR_ATTACHMENT0));

        PixelInfo pixel;
        // qt coordinate is from left up to right bottom
        //  (x, y)------> w
        //    | \
        //    |  \
        //    |   .
        //    v h
        // read pixel from left bottom to right up
        //    ^ h
        //    |   .
        //    |  /
        //    | /
        //  (x, y)------> w

        CHECK_GL(glReadPixels(x, h - y - 1, 1, 1, GL_RGB_INTEGER, GL_UNSIGNED_INT, &pixel));

        // output buffer to image
//        auto* pixels = new PixelInfo[w * h];
//        CHECK_GL(glReadPixels(0, 0, w, h, GL_RGB_INTEGER, GL_UNSIGNED_INT, pixels));
//        auto* ids = new unsigned int[w * h];
//        for (int i=0; i<w*h; i++)
//            ids[i] = pixels[i].obj_id;
//        load_buffer_to_image(ids, w, h);

        CHECK_GL(glReadBuffer(GL_NONE));
        fbo->unbind();

        string result;
        if (scene->select_mode == zenovis::PICK_OBJECT) {
            if (!pixel.has_object()) return "";
            result = id_table[pixel.obj_id];
        }
        else {
            if (!pixel.has_object() || !pixel.has_element()) return "";
            result = id_table[pixel.obj_id] + ":" + std::to_string(pixel.elem_id - 1);
        }

        return result;
    }

    virtual string getPicked(int x0, int y0, int x1, int y1) override {
        draw();
        if (!fbo->complete()) return "";

        // prepare fbo
        CHECK_GL(glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo->fbo));
        CHECK_GL(glReadBuffer(GL_COLOR_ATTACHMENT0));

        // convert coordinates
        int start_x = x0 < x1 ? x0 : x1;
        int start_y = y0 > y1 ? y0 : y1;
        start_y = h - start_y - 1;
        int rect_w = abs(x0 - x1);
        int rect_h = abs(y0 - y1);

        // read pixels
        int pixel_count = rect_w * rect_h;
        auto* pixels = new PixelInfo[pixel_count];
        CHECK_GL(glReadPixels(start_x, start_y, rect_w, rect_h, GL_RGB_INTEGER, GL_UNSIGNED_INT, pixels));

        // output buffer to image
//        auto* img_pixels = new PixelInfo[w * h];
//        CHECK_GL(glReadPixels(0, 0, w, h, GL_RGB_INTEGER, GL_UNSIGNED_INT, img_pixels));
//        auto* ids = new unsigned int[w * h];
//        for (int i=0; i<w*h; i++)
//            ids[i] = img_pixels[i].obj_id;
//        load_buffer_to_image(ids, w, h);

        // unbind fbo
        CHECK_GL(glReadBuffer(GL_NONE));
        fbo->unbind();

        string result;
        if (scene->select_mode == zenovis::PICK_OBJECT) {
            unordered_set<unsigned int> selected_obj;
            // fetch selected objects' ids
            for (int i = 0; i < pixel_count; i++) {
                if (pixels[i].has_object())
                    selected_obj.insert(pixels[i].obj_id);
            }
            // generate select result
            for (auto id: selected_obj) {
                if (id_table.find(id) != id_table.end())
                    result += id_table[id] + " ";
            }
        }
        else {
            unordered_map<unsigned int, unordered_set<unsigned int>> selected_elem;
            for (int i = 0; i < pixel_count; i++) {
                if (pixels[i].has_object() && pixels[i].has_element()) {
                    if (selected_elem.find(pixels[i].obj_id) != selected_elem.end())
                        selected_elem[pixels[i].obj_id].insert(pixels[i].elem_id);
                    else selected_elem[pixels[i].obj_id] = {pixels[i].elem_id};
                }
            }
            // generate select result
            for (auto &[obj_id, elem_ids] : selected_elem) {
                if (id_table.find(obj_id) != id_table.end()) {
                    for (auto &elem_id : elem_ids)
                        result += id_table[obj_id] + ":" + std::to_string(elem_id - 1) + " ";
                }
            }
        }
        return result;
    }

    virtual void setPrimSet(const std::vector<std::string>& prims) override {
        prim_set.clear();
        prim_set.assign(prims.begin(), prims.end());
    }
};

}

unique_ptr<IPicker> makeFrameBufferPicker(Scene *scene) {
    return make_unique<FrameBufferPicker>(scene);
}

}