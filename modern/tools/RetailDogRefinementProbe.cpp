#include "ModernMeshRefinement.hpp"
#include "ResourceRuntime.hpp"
#include "LegacyMeshData.hpp"

#include <filesystem>
#include <iostream>

int main(int argc,char** argv)
{
    using namespace monopoly::data;
    if(argc!=2){std::cerr<<"usage: MonopolyRetailDogRefinementProbe <runtime-data>\n";return 2;}
    const std::array<std::filesystem::path,1> roots{std::filesystem::path(argv[1])};
    auto paths=ResourcePaths::create(roots);
    if(!paths){std::cerr<<"resource paths: "<<paths.error().detail<<"\n";return 3;}
    ResourceRuntime resources;
    auto initialized=resources.initialize(*paths,{BoardEdition::Usa,LanguageId::EnglishUs});
    if(!initialized){std::cerr<<"resources: "<<initialized.error().detail<<"\n";return 4;}
    const auto snapshot=resources.snapshot();
    constexpr auto Dog=packDataId(LegacyGroupId::ThreeD,0x0038);
    auto source=openLegacyMeshData(snapshot->data(),Dog);
    if(!source){std::cerr<<"HMD dog: "<<source.error().detail<<"\n";return 5;}
    auto runtime=MeshXRuntime::build(std::make_shared<const LegacyMeshData>(std::move(*source)));
    if(!runtime){std::cerr<<"MESHX dog: "<<runtime.error().detail<<"\n";return 6;}
    const auto base=makeMeshRenderData(*runtime);
    auto refined=refineClassicTokenMesh(base);
    if(!refined){std::cerr<<"refine dog: "<<refined.error().detail<<"\n";return 7;}
    std::cout<<"RETAIL_DOG_HMD=0x00080038\n"
             <<"SOURCE_VERTICES="<<base.vertices.size()<<"\n"
             <<"SOURCE_TRIANGLES="<<base.indices.size()/3<<"\n"
             <<"REFINED_VERTICES="<<refined->vertices.size()<<"\n"
             <<"REFINED_TRIANGLES="<<refined->indices.size()/3<<"\n"
             <<"BATCHES="<<refined->batches.size()<<"\n"
             <<"MATERIAL_MODEL="<<(refined->batches.empty()? -1:
                static_cast<int>(refined->batches.front().material.model))<<"\n"
             <<"METALLIC="<<(refined->batches.empty()?0:refined->batches.front().material.metallic)<<"\n"
             <<"ROUGHNESS="<<(refined->batches.empty()?0:refined->batches.front().material.roughness)<<"\n";
    return 0;
}
