#include "Structures.hpp"
#include "zensim/cuda/execution/ExecutionPolicy.cuh"
#include "zensim/geometry/LevelSetUtils.tpp"
#include "zensim/geometry/SparseGrid.hpp"
#include "zensim/geometry/VdbLevelSet.h"
#include "zensim/omp/execution/ExecutionPolicy.hpp"
#include "zensim/zpc_tpls/fmt/color.h"
#include "zensim/zpc_tpls/fmt/format.h"

#include <zeno/types/ListObject.h>
#include <zeno/types/NumericObject.h>
#include <zeno/types/PrimitiveObject.h>

#include <zeno/VDBGrid.h>

#include "../utils.cuh"

namespace zeno {

struct ZSMakeSparseGrid : INode {
    void apply() override {
        auto attr = get_input2<std::string>("Attribute");
        auto dx = get_input2<float>("Dx");
        auto bg = get_input2<float>("background");
        auto type = get_input2<std::string>("type");
        auto structure = get_input2<std::string>("structure");

        auto zsSPG = std::make_shared<ZenoSparseGrid>();
        auto &spg = zsSPG->spg;

        int nc = 1;
        if (type == "scalar")
            nc = 1;
        else if (type == "vector3")
            nc = 3;

        spg = ZenoSparseGrid::spg_t{{{attr, nc}}, 0, zs::memsrc_e::device, 0};
        spg.scale(dx);
        spg._background = bg;

        if (structure == "vertex-centered") {
            auto trans = zs::vec<float, 3>::uniform(-dx / 2);
            // zs::vec<float, 3> trans{-dx / 2.f, -dx / 2.f, -dx / 2.f};

            spg.translate(trans);
        }

        set_output("Grid", zsSPG);
    }
};

ZENDEFNODE(ZSMakeSparseGrid, {/* inputs: */
                              {{"string", "Attribute", ""},
                               {"float", "Dx", "1.0"},
                               {"float", "background", "0"},
                               {"enum scalar vector3", "type", "scalar"},
                               {"enum cell-centered vertex-centered", "structure", "cell-centered "}},
                              /* outputs: */
                              {"Grid"},
                              /* params: */
                              {},
                              /* category: */
                              {"Eulerian"}});

struct ZSGridTopoCopy : INode {
    void apply() override {
        auto zs_grid = get_input<ZenoSparseGrid>("Grid");
        auto zs_topo = get_input<ZenoSparseGrid>("TopologyGrid");

        auto &grid = zs_grid->spg;
        auto &topo = zs_topo->spg;

        // topo copy
        grid._table = topo._table;
        grid._transform = topo._transform;
        grid._grid.resize(topo.numBlocks() * topo.block_size);

        set_output("Grid", zs_grid);
    }
};

ZENDEFNODE(ZSGridTopoCopy, {/* inputs: */
                            {"Grid", "TopologyGrid"},
                            /* outputs: */
                            {"Grid"},
                            /* params: */
                            {},
                            /* category: */
                            {"Eulerian"}});

struct ZSSparseGridToVDB : INode {
    void apply() override {
        auto zs_grid = get_input<ZenoSparseGrid>("SparseGrid");
        auto attr = get_input2<std::string>("Attribute");
        auto VDBGridClass = get_input2<std::string>("VDBGridClass");

        if (attr.empty())
            attr = "sdf";

        auto &spg = zs_grid->spg;

        auto attrTag = src_tag(zs_grid, attr);

        if (attr == "v") {
            auto vdb_ = zs::convert_sparse_grid_to_float3grid(spg, attrTag);
            auto vdb_grid = std::make_shared<VDBFloat3Grid>();
            vdb_grid->m_grid = vdb_.as<openvdb::Vec3fGrid::Ptr>();

            set_output("VDB", vdb_grid);
        } else {
            zs::u32 gridClass = 0;
            if (VDBGridClass == "UNKNOWN")
                gridClass = 0;
            else if (VDBGridClass == "LEVEL_SET")
                gridClass = 1;
            else if (VDBGridClass == "FOG_VOLUME")
                gridClass = 2;
            else if (VDBGridClass == "STAGGERED")
                gridClass = 3;

            auto vdb_ = zs::convert_sparse_grid_to_floatgrid(spg, attrTag, gridClass);

            auto vdb_grid = std::make_shared<VDBFloatGrid>();
            vdb_grid->m_grid = vdb_.as<openvdb::FloatGrid::Ptr>();

            set_output("VDB", vdb_grid);
        }
    }
};

ZENDEFNODE(ZSSparseGridToVDB, {/* inputs: */
                               {"SparseGrid",
                                {"string", "Attribute", ""},
                                {"enum UNKNOWN LEVEL_SET FOG_VOLUME STAGGERED", "VDBGridClass", "LEVEL_SET"}},
                               /* outputs: */
                               {"VDB"},
                               /* params: */
                               {},
                               /* category: */
                               {"Eulerian"}});

struct ZSVDBToSparseGrid : INode {
    void apply() override {
        auto vdb = get_input<VDBGrid>("VDB");
        auto attr = get_input2<std::string>("Attribute");
        if (attr.empty())
            attr = "sdf";

        if (has_input("SparseGrid")) {
            auto zs_grid = get_input<ZenoSparseGrid>("SparseGrid");
            auto &spg = zs_grid->spg;

            int num_ch;
            if (vdb->getType() == "FloatGrid")
                num_ch = 1;
            else if (vdb->getType() == "Vec3fGrid")
                num_ch = 3;
            else
                throw std::runtime_error("Input VDB must be a FloatGrid or Vec3fGrid!");

            auto attrTag = src_tag(zs_grid, attr);
            if (spg.hasProperty(attrTag)) {
                if (num_ch != spg.getPropertySize(attrTag)) {
                    throw std::runtime_error(fmt::format("The channel number of [{}] doesn't match!", attr));
                }
            } else {
                spg.append_channels(zs::cuda_exec(), {{attrTag, num_ch}});
            }

            if (num_ch == 1) {
                auto vdb_ = std::dynamic_pointer_cast<VDBFloatGrid>(vdb);
                zs::assign_floatgrid_to_sparse_grid(vdb_->m_grid, spg, attrTag);
            } else {
                auto vdb_ = std::dynamic_pointer_cast<VDBFloat3Grid>(vdb);
                zs::assign_float3grid_to_sparse_grid(vdb_->m_grid, spg, attrTag);
            }

            set_output("SparseGrid", zs_grid);
        } else {
            ZenoSparseGrid::spg_t spg;

            auto vdbType = vdb->getType();
            if (vdbType == "FloatGrid") {
                auto vdb_ = std::dynamic_pointer_cast<VDBFloatGrid>(vdb);
                spg =
                    zs::convert_floatgrid_to_sparse_grid(vdb_->m_grid, zs::MemoryHandle{zs::memsrc_e::device, 0}, attr);
            } else if (vdbType == "Vec3fGrid") {
                auto vdb_ = std::dynamic_pointer_cast<VDBFloat3Grid>(vdb);
                spg = zs::convert_float3grid_to_sparse_grid(vdb_->m_grid, zs::MemoryHandle{zs::memsrc_e::device, 0},
                                                            attr);
            } else {
                throw std::runtime_error("Input VDB must be a FloatGrid or Vec3fGrid!");
            }

            auto zsSPG = std::make_shared<ZenoSparseGrid>();
            zsSPG->spg = std::move(spg);

            set_output("SparseGrid", zsSPG);
        }
    }
};

ZENDEFNODE(ZSVDBToSparseGrid, {/* inputs: */
                               {"VDB", "SparseGrid", {"string", "Attribute", ""}},
                               /* outputs: */
                               {"SparseGrid"},
                               /* params: */
                               {},
                               /* category: */
                               {"Eulerian"}});

struct ZSGridVoxelSize : INode {
    void apply() override {
        auto zs_grid = get_input<ZenoSparseGrid>("SparseGrid");

        float dx = zs_grid->getSparseGrid().voxelSize()[0];

        set_output("dx", std::make_shared<NumericObject>(dx));
    }
};

ZENDEFNODE(ZSGridVoxelSize, {/* inputs: */
                             {"SparseGrid"},
                             /* outputs: */
                             {"dx"},
                             /* params: */
                             {},
                             /* category: */
                             {"Eulerian"}});

struct ZSMakeDenseSDF : INode {
    void apply() override {
        float dx = get_input2<float>("dx");
        int nx = get_input2<int>("nx");
        int ny = get_input2<int>("ny");
        int nz = get_input2<int>("nz");

        int nbx = float(nx + 7) / 8.f;
        int nby = float(ny + 7) / 8.f;
        int nbz = float(nz + 7) / 8.f;

        size_t numExpectedBlocks = nbx * nby * nbz;

        auto zsSPG = std::make_shared<ZenoSparseGrid>();
        auto &spg = zsSPG->spg;
        spg = ZenoSparseGrid::spg_t{{{"sdf", 1}}, numExpectedBlocks, zs::memsrc_e::device, 0};
        spg.scale(dx);
        spg._background = dx;

        auto pol = zs::cuda_exec();
        constexpr auto space = zs::execspace_e::cuda;
        using ivec3 = zs::vec<int, 3>;

        pol(zs::range(numExpectedBlocks),
            [table = zs::proxy<space>(spg._table), nbx, nby, nbz] __device__(int nb) mutable {
                int i = nb / (nby * nbz);
                nb -= i * (nby * nbz);
                int j = nb / nbz;
                int k = nb - j * nbz;
                table.insert(ivec3{int(i - nbx / 2) * 8, int(j - nby / 2) * 8, int(k - nbz / 2) * 8});
            });

        ivec3 sphere_c{0, 0, 0};
        int sphere_r = 10; // 10*dx

        auto bcnt = spg.numBlocks();
        pol(zs::range(bcnt * 512), [spgv = zs::proxy<space>(spg), sphere_c, sphere_r] __device__(int cellno) mutable {
#if 0            
			int bno = cellno / 512;
            int cno = cellno & 511;
            auto bcoord = spgv._table._activeKeys[bno];
            auto cellid = RM_CVREF_T(spgv)::local_offset_to_coord(cno);
            auto ccoord = bcoord + cellid;
#endif
            auto icoord = spgv.iCoord(cellno);
            auto dx = spgv.voxelSize()[0]; // spgv._transform(0, 0);

            float dist2c = zs::sqrt(float(zs::sqr(icoord[0] - sphere_c[0]) + zs::sqr(icoord[1] - sphere_c[1]) +
                                          zs::sqr(icoord[2] - sphere_c[2])));
            float dist2s = dist2c - sphere_r;

            float init_sdf = dist2s;
            if (dist2s > 2. * dx)
                init_sdf = 2. * dx;
            else if (dist2s < -2. * dx)
                init_sdf = -2. * dx;

            //spgv("sdf", bno, cno) = ;
            spgv("sdf", icoord) = init_sdf;
        });

        // spg.resize(numExpectedBlocks);

        spg.append_channels(pol, {{"v", 3}});

        set_output("Grid", zsSPG);
    }
};

ZENDEFNODE(ZSMakeDenseSDF, {/* inputs: */
                            {{"float", "dx", "1.0"}, {"int", "nx", "128"}, {"int", "ny", "128"}, {"int", "nz", "128"}},
                            /* outputs: */
                            {"Grid"},
                            /* params: */
                            {},
                            /* category: */
                            {"deprecated"}});

} // namespace zeno