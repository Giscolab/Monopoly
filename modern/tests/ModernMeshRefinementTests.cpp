#include "ModernMeshRefinement.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    int failures{};
    void expect(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) ++failures;
    }
}

int main()
{
    using namespace monopoly::data;

    MeshRenderData source;
    source.vertices.resize(4);
    source.vertices[0].position={-1,0,-1};
    source.vertices[1].position={ 1,0,-1};
    source.vertices[2].position={ 1,0, 1};
    source.vertices[3].position={-1,0, 1};
    source.indices={0,1,2,0,2,3};
    MeshMaterial material;
    source.batches.push_back({0,source.indices.size(),material,{}});
    source.bounds={{-1,0,-1},{1,0,1}};

    auto one=refineTriangleMesh(source,{1,false,false});
    expect(bool(one), "one topology subdivision succeeds");
    expect(one && one->indices.size()==24, "two source triangles become eight triangles");
    expect(one && one->vertices.size()==9, "shared edge midpoints are reused globally");
    expect(one && one->batches.size()==1 && one->batches[0].indexCount==24,
        "material batch ownership survives subdivision");

    auto classic=refineClassicTokenMesh(source);
    expect(bool(classic), "classic token two-pass refinement succeeds");
    expect(classic && classic->indices.size()==96, "classic preset creates sixteen triangles per input triangle");
    expect(classic && classic->batches[0].material.model==MeshMaterialModel::MetallicRoughness &&
        std::abs(classic->batches[0].material.metallic-1.0F)<.001F &&
        std::abs(classic->batches[0].material.roughness-.25F)<.001F &&
        classic->batches[0].material.proceduralSurface[0]>0.5F &&
        std::abs(classic->batches[0].material.proceduralSurface[1]-2.0F)<.001F &&
        std::abs(classic->batches[0].material.proceduralSurface[2]-.10F)<.001F,
        "classic preset publishes Blender-reference metal and micro-surface factors");
    expect(classic && std::all_of(classic->vertices.begin(),classic->vertices.end(),
        [](const auto& v){return std::isfinite(v.normal[0])&&std::isfinite(v.normal[1])&&std::isfinite(v.normal[2]);}),
        "refined normals remain finite");

    MeshRenderData malformed=source;
    malformed.indices.push_back(0);
    expect(!refineClassicTokenMesh(malformed), "malformed triangle list is rejected");

    return failures ? 1 : 0;
}
