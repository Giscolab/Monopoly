#include "ModernMeshRefinement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace monopoly::data
{
    namespace
    {
        struct Edge
        {
            std::uint32_t a{}, b{};
            Edge(std::uint32_t x, std::uint32_t y) noexcept
                : a(std::min(x,y)), b(std::max(x,y)) {}
            auto operator<=>(const Edge&) const = default;
        };

        struct EdgeInfo
        {
            std::vector<std::uint32_t> opposites;
            std::uint32_t midpoint{};
        };

        MeshRuntimeError invalid(std::string detail)
        {
            return {MeshRuntimeErrorCode::ModernAssetInvalid, std::move(detail)};
        }

        std::array<float,3> add(const std::array<float,3>& a,
            const std::array<float,3>& b) noexcept
        { return {a[0]+b[0],a[1]+b[1],a[2]+b[2]}; }

        std::array<float,3> sub(const std::array<float,3>& a,
            const std::array<float,3>& b) noexcept
        { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }

        std::array<float,3> mul(const std::array<float,3>& a, float s) noexcept
        { return {a[0]*s,a[1]*s,a[2]*s}; }

        float dot(const std::array<float,3>& a, const std::array<float,3>& b) noexcept
        { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

        std::array<float,3> cross(const std::array<float,3>& a,
            const std::array<float,3>& b) noexcept
        {
            return {a[1]*b[2]-a[2]*b[1],
                    a[2]*b[0]-a[0]*b[2],
                    a[0]*b[1]-a[1]*b[0]};
        }

        std::array<float,3> normalized(std::array<float,3> v) noexcept
        {
            const float l=std::sqrt(dot(v,v));
            if (!std::isfinite(l) || l < 1e-8F) return {0,1,0};
            return mul(v,1.0F/l);
        }

        bool finite(const MeshVertex& v) noexcept
        {
            return std::all_of(v.position.begin(),v.position.end(),
                [](float x){return std::isfinite(x);});
        }

        void rebuildNormalsAndBounds(MeshRenderData& mesh)
        {
            for (auto& v:mesh.vertices) v.normal={0,0,0};
            for (std::size_t i=0;i+2<mesh.indices.size();i+=3)
            {
                const auto ia=mesh.indices[i], ib=mesh.indices[i+1], ic=mesh.indices[i+2];
                const auto n=cross(sub(mesh.vertices[ib].position,mesh.vertices[ia].position),
                                   sub(mesh.vertices[ic].position,mesh.vertices[ia].position));
                for (const auto index:{ia,ib,ic})
                    mesh.vertices[index].normal=add(mesh.vertices[index].normal,n);
            }
            for (auto& v:mesh.vertices) v.normal=normalized(v.normal);
            if (mesh.vertices.empty()) return;
            mesh.bounds.minimum=mesh.bounds.maximum=mesh.vertices.front().position;
            for (const auto& v:mesh.vertices)
                for (int axis=0;axis<3;++axis)
                {
                    mesh.bounds.minimum[axis]=std::min(mesh.bounds.minimum[axis],v.position[axis]);
                    mesh.bounds.maximum[axis]=std::max(mesh.bounds.maximum[axis],v.position[axis]);
                }
        }

        std::expected<MeshRenderData,MeshRuntimeError> refineOnce(const MeshRenderData& source,
            bool smoothControlVertices)
        {
            if (source.indices.empty() || source.indices.size()%3)
                return std::unexpected(invalid("triangle refinement requires complete triangle indices"));
            if (source.vertices.empty())
                return std::unexpected(invalid("triangle refinement requires vertices"));
            if (std::any_of(source.indices.begin(),source.indices.end(),
                [&](auto i){return i>=source.vertices.size();}))
                return std::unexpected(invalid("triangle refinement index exceeds vertex count"));
            if (std::any_of(source.vertices.begin(),source.vertices.end(),
                [](const auto& v){return !finite(v);}))
                return std::unexpected(invalid("triangle refinement requires finite positions"));

            std::map<Edge,EdgeInfo> edges;
            std::vector<std::set<std::uint32_t>> neighbours(source.vertices.size());
            for (std::size_t i=0;i<source.indices.size();i+=3)
            {
                const std::array<std::uint32_t,3> t{source.indices[i],source.indices[i+1],source.indices[i+2]};
                for (int e=0;e<3;++e)
                {
                    const auto a=t[e], b=t[(e+1)%3], opposite=t[(e+2)%3];
                    edges[Edge(a,b)].opposites.push_back(opposite);
                    neighbours[a].insert(b); neighbours[b].insert(a);
                }
            }

            MeshRenderData result;
            result.vertices=source.vertices;
            if (smoothControlVertices)
            {
                std::vector<std::vector<std::uint32_t>> boundary(source.vertices.size());
                for (const auto& [edge,info]:edges)
                    if (info.opposites.size()==1)
                    {
                        boundary[edge.a].push_back(edge.b);
                        boundary[edge.b].push_back(edge.a);
                    }
                for (std::size_t i=0;i<source.vertices.size();++i)
                {
                    const auto p=source.vertices[i].position;
                    if (boundary[i].size()>=2)
                    {
                        const auto a=source.vertices[boundary[i][0]].position;
                        const auto b=source.vertices[boundary[i][1]].position;
                        result.vertices[i].position=add(mul(p,.75F),mul(add(a,b),.125F));
                    }
                    else if (!neighbours[i].empty())
                    {
                        const float n=static_cast<float>(neighbours[i].size());
                        const float beta=n==3.0F ? 3.0F/16.0F : 3.0F/(8.0F*n);
                        std::array<float,3> sum{};
                        for (const auto j:neighbours[i]) sum=add(sum,source.vertices[j].position);
                        result.vertices[i].position=add(mul(p,1.0F-n*beta),mul(sum,beta));
                    }
                }
            }

            for (auto& [edge,info]:edges)
            {
                MeshVertex midpoint=source.vertices[edge.a];
                const auto pa=source.vertices[edge.a].position;
                const auto pb=source.vertices[edge.b].position;
                if (info.opposites.size()==2)
                {
                    const auto pc=source.vertices[info.opposites[0]].position;
                    const auto pd=source.vertices[info.opposites[1]].position;
                    midpoint.position=add(mul(add(pa,pb),.375F),mul(add(pc,pd),.125F));
                }
                else midpoint.position=mul(add(pa,pb),.5F);
                midpoint.uv={(source.vertices[edge.a].uv[0]+source.vertices[edge.b].uv[0])*.5F,
                             (source.vertices[edge.a].uv[1]+source.vertices[edge.b].uv[1])*.5F};
                midpoint.tangent={};
                info.midpoint=static_cast<std::uint32_t>(result.vertices.size());
                result.vertices.push_back(midpoint);
            }

            result.indices.reserve(source.indices.size()*4);
            result.batches.reserve(source.batches.size());
            for (const auto& batch:source.batches)
            {
                if (batch.firstIndex+batch.indexCount>source.indices.size() || batch.indexCount%3)
                    return std::unexpected(invalid("triangle refinement batch range is invalid"));
                const auto first=result.indices.size();
                for (std::size_t i=batch.firstIndex;i<batch.firstIndex+batch.indexCount;i+=3)
                {
                    const auto a=source.indices[i], b=source.indices[i+1], c=source.indices[i+2];
                    const auto ab=edges.at(Edge(a,b)).midpoint;
                    const auto bc=edges.at(Edge(b,c)).midpoint;
                    const auto ca=edges.at(Edge(c,a)).midpoint;
                    const std::array<std::uint32_t,12> refined{
                        a,ab,ca, b,bc,ab, c,ca,bc, ab,bc,ca};
                    result.indices.insert(result.indices.end(),refined.begin(),refined.end());
                }
                auto material=batch.material;
                result.batches.push_back({first,result.indices.size()-first,std::move(material),batch.texture});
            }
            rebuildNormalsAndBounds(result);
            return result;
        }
    }

    std::expected<MeshRenderData,MeshRuntimeError>
    refineTriangleMesh(const MeshRenderData& source, MeshRefinementOptions options)
    {
        if (options.levels>3)
            return std::unexpected(invalid("triangle refinement is capped at three levels"));
        MeshRenderData result=source;
        for (unsigned level=0;level<options.levels;++level)
        {
            auto refined=refineOnce(result,options.smoothControlVertices);
            if (!refined) return std::unexpected(refined.error());
            result=std::move(*refined);
        }
        if (options.modernMetalMaterial)
            for (auto& batch:result.batches)
            {
                batch.material.model=MeshMaterialModel::MetallicRoughness;
                batch.material.diffuse={0.82F,0.84F,0.87F,1.0F};
                batch.material.metallic=std::clamp(options.metallic,0.0F,1.0F);
                batch.material.roughness=std::clamp(options.roughness,0.04F,1.0F);
                batch.material.emissive={};
                batch.material.baseColorTexture.reset();
                batch.material.metallicRoughnessTexture.reset();
                batch.material.normalTexture.reset();
                batch.material.emissiveTexture.reset();
                batch.material.occlusionTexture.reset();
                batch.texture.reset();
            }
        return result;
    }

    std::expected<MeshRenderData,MeshRuntimeError>
    refineClassicTokenMesh(const MeshRenderData& source)
    {
        MeshRefinementOptions first;
        first.levels=1;
        first.smoothControlVertices=true;
        first.modernMetalMaterial=false;
        auto smoothed=refineTriangleMesh(source,first);
        if (!smoothed) return std::unexpected(smoothed.error());

        MeshRefinementOptions second;
        second.levels=1;
        second.smoothControlVertices=false;
        second.modernMetalMaterial=true;
        // Bright cast nickel/chrome. For a metal workflow the base colour is
        // the specular tint itself, so a mid-grey albedo produces a dark token
        // even with a valid HDR environment. Keep the surface reflective but
        // not mirror-perfect so the small 1999 silhouette remains readable.
        second.metallic=1.0F;
        second.roughness=.25F;
        auto refined=refineTriangleMesh(*smoothed,second);
        if (refined)
            for (auto& batch:refined->batches)
                batch.material.proceduralSurface={1.0F,2.0F,.10F,.18F};
        return refined;
    }
}
