#pragma once
#include "World3DProjection.hpp"
#include "World3DRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace monopoly::engine
{
    inline data::MeshBounds presentationWorldBounds(const data::MeshBounds& local,
        const sequence::Matrix3D& world)
    {
        data::MeshBounds result;
        result.minimum.fill(std::numeric_limits<float>::max());
        result.maximum.fill(-std::numeric_limits<float>::max());
        for(unsigned corner=0;corner<8;++corner)
        {
            std::array<float,3> p{};
            for(unsigned axis=0;axis<3;++axis)p[axis]=(corner&(1U<<axis))?local.maximum[axis]:local.minimum[axis];
            for(unsigned axis=0;axis<3;++axis)
            {
                const float value=p[0]*world.values[axis]+p[1]*world.values[4+axis]+p[2]*world.values[8+axis]+world.values[12+axis];
                result.minimum[axis]=std::min(result.minimum[axis],value);result.maximum[axis]=std::max(result.maximum[axis],value);
            }
        }
        return result;
    }
    // Presentation only: measured immutable geometry; no sequencer/game state.
    inline World3DCamera modernBoardPresentationCamera(const data::MeshBounds& bounds,
        float aspect, float elevationDegrees = 48.0F, float yawDegrees = 8.0F,
        float bottomReservedFraction = 0.0F)
    {
        const float rad = 0.01745329252F;
        const float e = elevationDegrees * rad, a = yawDegrees * rad;
        const float span = std::max(bounds.maximum[0]-bounds.minimum[0],
            bounds.maximum[2]-bounds.minimum[2]);
        const float distance = std::max(1.0F, span * 2.65F);
        std::array<float,3> forward{-std::cos(e)*std::sin(a),-std::sin(e),std::cos(e)*std::cos(a)};
        std::array<float,3> right{std::cos(a),0,std::sin(a)};
        std::array<float,3> up{-std::sin(e)*std::sin(a),std::cos(e),std::sin(e)*std::cos(a)};
        auto dot=[](const auto& x,const auto& y){return x[0]*y[0]+x[1]*y[1]+x[2]*y[2];};
        std::array<float,3> target{};
        for(unsigned axis=0;axis<3;++axis) target[axis]=(bounds.minimum[axis]+bounds.maximum[axis])*.5F;
        World3DCamera camera;
        camera.forward=forward;
        camera.up={0,1,0};
        camera.nearPlane=std::max(.01F,span*.015F);camera.farPlane=distance+span*8;
        float fittedTan=0;
        // Correct the perspective silhouette's screen centroid, rather than
        // fitting its world centroid and leaving a large empty band above it.
        for(unsigned iteration=0;iteration<8;++iteration)
        {
            for(unsigned axis=0;axis<3;++axis)camera.location[axis]=target[axis]-forward[axis]*distance;
            float minX=std::numeric_limits<float>::max(),minY=minX,maxX=-minX,maxY=-minX;
            for(unsigned corner=0;corner<8;++corner)
            {
                std::array<float,3> rel{};
                for(unsigned axis=0;axis<3;++axis)rel[axis]=((corner&(1U<<axis))?bounds.maximum[axis]:bounds.minimum[axis])-camera.location[axis];
                const float z=dot(rel,forward),x=dot(rel,right)/z,y=dot(rel,up)/z;
                minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);
            }
            fittedTan=std::max(std::max(std::abs(minX),std::abs(maxX)),
                std::max(std::abs(minY),std::abs(maxY))*aspect);
            if(iteration<7)for(unsigned axis=0;axis<3;++axis)
                target[axis]+=distance*((minX+maxX)*.5F*right[axis]+(minY+maxY)*.5F*up[axis]);
        }
        const float reserved=std::clamp(bottomReservedFraction,0.0F,0.4F);
        const float horizontalTan=fittedTan*1.08F/(1.0F-reserved);
        camera.fieldOfView=2*std::atan(horizontalTan);
        // The retail controls occupy the bottom of the full 3D viewport.
        // Shift the framed board above that overlay without changing UI or CNK.
        for(unsigned axis=0;axis<3;++axis)
            camera.location[axis]-=up[axis]*distance*(horizontalTan/aspect)*reserved;
        return camera;
    }
    inline World3DLighting modernBoardPresentationLighting()
    {
        World3DLighting lighting;
        lighting.ambient={.24F,.28F,.33F};
        lighting.sun.enabled=true;lighting.sun.direction={.35F,-1,.25F};
        lighting.sun.color={1.25F,1.16F,1.02F};
        lighting.boardReflection.enabled=true;
        lighting.boardReflection.direction={-.5F,-.4F,-.7F};
        lighting.boardReflection.color={.13F,.19F,.25F};
        return lighting;
    }
}
