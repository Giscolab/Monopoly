#include "ModernScenePresentation.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
    bool close(float a,float b) { return std::abs(a-b)<0.001F; }
    void testBuildingAvoidance()
    {
        engine::World3DCamera camera;
        camera.location={0,20,-20}; camera.forward={0,-.5F,1};
        const std::array<data::MeshBounds,1> buildings{{{{-10,0,-30},{10,100,10}}}};
        const auto raised=engine::avoidModernPresentationBuildings(camera,buildings,0,10,40);
        require(close(raised.location[1],110),"Eye clears loaded roof plus near-plane clearance");
        require(raised.location[0]==camera.location[0] && raised.location[2]==camera.location[2],"No horizontal relocation");
        const float targetZ=camera.location[2]-camera.location[1]*camera.forward[2]/camera.forward[1];
        require(close(raised.location[2]-raised.location[1]*raised.forward[2]/raised.forward[1],targetZ),"Original board-plane target remains fixed");
        require(raised.up==camera.up && raised.fieldOfView==camera.fieldOfView &&
            raised.nearPlane==camera.nearPlane && raised.farPlane==camera.farPlane,"Projection and roll remain unchanged");
        auto outside=camera;outside.location[0]=51;
        require(engine::avoidModernPresentationBuildings(outside,buildings,0,10,40)==outside,"Outside halo returns exact original");
        auto edge=camera;edge.location[0]=49.99F;
        const auto edgeResult=engine::avoidModernPresentationBuildings(edge,buildings,0,10,40);
        require(std::abs(edgeResult.location[1]-edge.location[1])<.001F,"Smooth influence tends to zero at halo boundary");
        auto high=camera;high.location[1]=120;
        require(engine::avoidModernPresentationBuildings(high,buildings,0,10,40)==high,"Already clear camera unchanged");
        auto horizontal=camera;horizontal.forward[1]=0;
        require(engine::avoidModernPresentationBuildings(horizontal,buildings,0,10,40)==horizontal,"No guessed target for horizontal camera");
        auto invalid=buildings;invalid[0].maximum[0]=std::numeric_limits<float>::infinity();
        require(engine::avoidModernPresentationBuildings(camera,invalid,0,10,40)==camera,"Invalid bounds ignored");
        require(engine::avoidModernPresentationBuildings(camera,buildings,0,10,0)==camera,"Invalid influence leaves camera unchanged");
        const std::array<data::MeshBounds,2> overlap{{buildings[0],{{-10,0,-30},{10,140,10}}}};
        require(close(engine::avoidModernPresentationBuildings(camera,overlap,0,10,40).location[1],150),"Overlaps use largest local clearance, no accumulated lift");
        require(camera.location[1]==20 && camera.forward[1]==-.5F,"Caller camera never mutated");
    }
}
int main()
{
    try { testBuildingAvoidance(); std::cout<<"Modern presentation camera tests passed\n"; return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
