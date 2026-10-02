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
    void testMeasuredTravel()
    {
        engine::World3DCamera raw;
        raw.location={250.416F,78.209F,300.542F};raw.forward={-.0195513F,-.358368F,0};
        raw.nearPlane=10;raw.farPlane=1540;
        const auto adjusted=engine::preserveModernTravelFraming(raw,0);
        require(adjusted.location[1]>217 && adjusted.location[1]<219,"Actual40722 cancelled blend retreats above plaza");
        const float parameter=-raw.location[1]/raw.forward[1];
        for(unsigned axis=0;axis<3;++axis)
            require(close(raw.location[axis]+raw.forward[axis]*parameter,
                adjusted.location[axis]+adjusted.forward[axis]*(-adjusted.location[1]/adjusted.forward[1])),"Measured travel preserves exact board-plane aim");
        require(adjusted.forward==raw.forward && adjusted.up==raw.up && adjusted.fieldOfView==raw.fieldOfView &&
            adjusted.nearPlane==raw.nearPlane && adjusted.farPlane==raw.farPlane,"Direction roll and projection unchanged");
        auto unit=raw;unit.forward={0,-1,0};
        require(engine::preserveModernTravelFraming(unit,0)==unit,"Unit preset closeup unchanged");
        auto outside=raw;outside.forward={0,1,0};
        require(engine::preserveModernTravelFraming(outside,0)==outside,"Upward camera unchanged");
        outside.forward={0,0,0};require(engine::preserveModernTravelFraming(outside,0)==outside,"Degenerate camera unchanged");
        require(engine::preserveModernTravelFraming(raw,std::numeric_limits<float>::infinity())==raw,"Invalid ground unchanged");
        auto nearUnit=raw;nearUnit.forward={0,-.999999F,0};
        require(engine::preserveModernTravelFraming(nearUnit,0)==nearUnit,
            "Float-noise near-unit preset remains byte-identical");
        auto boundary=raw;boundary.forward={0,-.99998F,0};
        require(std::abs(engine::preserveModernTravelFraming(boundary,0).location[1]-boundary.location[1])<.002F,
            "Tolerance boundary introduces only negligible subpixel retreat");
        const std::array<data::MeshBounds,1> buildings{{{{200,0,280},{280,250,330}}}};
        const auto cleared=engine::avoidModernPresentationBuildings(adjusted,buildings,0,20,80);
        require(cleared.location[1]>=270,"Subsequent building clearance still applies");
        require(close(cleared.location[0]-cleared.location[1]*cleared.forward[0]/cleared.forward[1],
            raw.location[0]-raw.location[1]*raw.forward[0]/raw.forward[1]),"Both adaptations retain same aim");
    }
    void testVerticalBoardFit()
    {
        engine::World3DCamera original;
        original.location={242.9F,1200,243};original.forward={.0045F,-.99999F,0};original.up={1,0,0};
        original.nearPlane=10;original.farPlane=3000;
        const data::MeshBounds board{{-20,-1,-20},{510,4,510}};
        for(const float aspect : {800.0F/450,1920.0F/810})
        {
            const auto fitted=engine::fitModernTopDownBoard(board,original,aspect);
            require(fitted.forward==original.forward && fitted.up==original.up,"Vertical fit preserves authored roll and direction");
            require(fitted.nearPlane==original.nearPlane && fitted.farPlane==original.farPlane,"Vertical fit preserves depth planes");
            const float magnitude=std::hypot(fitted.forward[0],fitted.forward[1],fitted.forward[2]);
            const float fx=fitted.forward[0]/magnitude,fy=fitted.forward[1]/magnitude;
            const float ux=-fy,uy=fx;
            const float tangent=std::tan(fitted.fieldOfView*.5F);
            for(unsigned corner=0;corner<8;++corner)
            {
                const float x=((corner&1)?board.maximum[0]:board.minimum[0])-fitted.location[0];
                const float y=((corner&2)?board.maximum[1]:board.minimum[1])-fitted.location[1];
                const float z=((corner&4)?board.maximum[2]:board.minimum[2])-fitted.location[2];
                const float depth=x*fx+y*fy;
                require(std::abs(z/depth/tangent)<.927F && std::abs((x*ux+y*uy)/depth/tangent*aspect)<.927F,
                    "Every board corner fits above controls with margin in actual Main aspect");
            }
        }
        require(close(engine::modernBoardControlReservation({0,0,800,450},1920,1080,450),.25F),
            "Independent 16:9 world and 4:3 UI transformations reserve bottom quarter at1080p");
        require(close(engine::modernBoardControlReservation({0,0,800,450},1600,1200,450),1.0F/6),
            "4:3 window computes its actual smaller overlap");
        require(close(engine::modernBoardControlReservation({0,0,800,450},4096,2160,450),.25F),
            "Fullscreen uses actual transforms rather than a guessed viewport height");
        require(engine::modernBoardControlReservation({0,0,800,450},0,0,450)==0,
            "Minimized target has no fabricated reserve");
        const auto reserved=engine::fitModernTopDownBoard(board,original,800.0F/600,.25F);
        const float reservedTan=std::tan(reserved.fieldOfView*.5F);
        const float fLength=std::hypot(reserved.forward[0],reserved.forward[1],reserved.forward[2]);
        const float fX=reserved.forward[0]/fLength,fY=reserved.forward[1]/fLength;
        for(unsigned corner=0;corner<8;++corner)
        {
            const float x=((corner&1)?board.maximum[0]:board.minimum[0])-reserved.location[0];
            const float y=((corner&2)?board.maximum[1]:board.minimum[1])-reserved.location[1];
            const float depth=x*fX+y*fY;
            const float screenY=(-x*fY+y*fX)/depth/reservedTan*(800.0F/600);
            require(screenY>=-.5F && screenY<=1,"Actual full-height CNK viewport retains all board corners above bottom quarter controls");
        }
        auto invalid=board;invalid.minimum[0]=std::numeric_limits<float>::quiet_NaN();
        require(engine::fitModernTopDownBoard(invalid,original,1.7F)==original,"Invalid geometry leaves original exact");
        require(engine::fitModernTopDownBoard(board,original,0)==original,"Invalid viewport leaves original exact");
        auto parallel=original;parallel.up=parallel.forward;
        require(engine::fitModernTopDownBoard(board,parallel,1.7F)==parallel,"Parallel up does not create invalid GPU projection");
        auto clipped=original;clipped.farPlane=20;
        require(engine::fitModernTopDownBoard(board,clipped,1.7F)==clipped,"Invalid depth fit falls back exact");
    }
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
    try { testMeasuredTravel();testBuildingAvoidance();testVerticalBoardFit(); std::cout<<"Modern presentation camera tests passed\n"; return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
