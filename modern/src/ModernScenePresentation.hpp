#pragma once
#include "World3DProjection.hpp"
#include "LogicalViewport.hpp"
#include "World3DRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

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
    // Unit preset directions are linearly blended by the retail controller.
    // Opposing directions shorten that blend; normalization at projection time
    // otherwise turns a low lateral travel into an extreme overhead closeup.
    // Retreat only the rendered eye along its original ground-target ray.
    inline World3DCamera preserveModernTravelFraming(const World3DCamera& original,
        float groundY) noexcept
    {
        if(!std::isfinite(groundY) || !std::isfinite(original.fieldOfView) ||
            original.fieldOfView<=0 || original.fieldOfView>=3.14159265F ||
            !std::isfinite(original.nearPlane) || original.nearPlane<=0 ||
            !std::isfinite(original.farPlane) || original.farPlane<=original.nearPlane)return original;
        for(unsigned axis=0;axis<3;++axis)
            if(!std::isfinite(original.location[axis]) || !std::isfinite(original.forward[axis]) ||
                !std::isfinite(original.up[axis]))return original;
        if(original.forward[1]>=-0.00001F)return original;
        const float magnitude=std::hypot(original.forward[0],original.forward[1],original.forward[2]);
        // Float-encoded unit presets retain their exact original bytes.
        if(!std::isfinite(magnitude) || magnitude<=0.000001F || magnitude>=1-0.00001F)return original;
        const float parameter=(groundY-original.location[1])/original.forward[1];
        if(!std::isfinite(parameter) || parameter<=0)return original;
        const float retreat=parameter*(1/magnitude-1);
        if(!std::isfinite(retreat))return original;
        auto adjusted=original;
        for(unsigned axis=0;axis<3;++axis)
        {
            adjusted.location[axis]-=original.forward[axis]*retreat;
            if(!std::isfinite(adjusted.location[axis]))return original;
        }
        return adjusted;
    }
    // Caller supplies only loaded building bounds, in the camera's world frame.
    // This value-only adjustment never feeds back into the retail camera controller.
    inline World3DCamera avoidModernPresentationBuildings(const World3DCamera& original,
        std::span<const data::MeshBounds> buildings, float groundY,
        float clearance, float influenceRadius) noexcept
    {
        if (!std::isfinite(groundY) || !std::isfinite(clearance) || clearance <= 0 ||
            !std::isfinite(influenceRadius) || influenceRadius <= 0 ||
            !std::isfinite(original.nearPlane) || original.nearPlane <= 0 ||
            !std::isfinite(original.farPlane) || original.farPlane <= original.nearPlane ||
            !std::isfinite(original.fieldOfView) || original.fieldOfView <= 0) return original;
        for (unsigned axis=0; axis<3; ++axis)
            if (!std::isfinite(original.location[axis]) || !std::isfinite(original.forward[axis]) ||
                !std::isfinite(original.up[axis])) return original;
        // Preserve the exact original board-plane aim. Horizontal/upward views
        // have no forward board intersection and intentionally retain their camera.
        if (original.forward[1] >= -0.00001F) return original;
        const float distance=(groundY-original.location[1])/original.forward[1];
        if (!std::isfinite(distance) || distance <= 0) return original;
        float lift=0;
        for (const auto& bounds : buildings)
        {
            bool valid=true;
            for (unsigned axis=0; axis<3; ++axis)
                valid=valid && std::isfinite(bounds.minimum[axis]) &&
                    std::isfinite(bounds.maximum[axis]) && bounds.minimum[axis] <= bounds.maximum[axis];
            if (!valid || bounds.maximum[1] <= groundY) continue;
            const float dx=std::max({bounds.minimum[0]-original.location[0],0.0F,
                original.location[0]-bounds.maximum[0]});
            const float dz=std::max({bounds.minimum[2]-original.location[2],0.0F,
                original.location[2]-bounds.maximum[2]});
            const float outside=std::hypot(dx,dz);
            if (!std::isfinite(outside) || outside >= influenceRadius) continue;
            const float t=1-outside/influenceRadius;
            const float weight=t*t*(3-2*t);
            const float needed=bounds.maximum[1]+std::max(clearance,original.nearPlane)-original.location[1];
            if (!std::isfinite(needed)) return original;
            lift=std::max(lift,std::max(0.0F,needed)*weight);
        }
        if (lift == 0) return original;
        auto result=original;
        result.location[1]+=lift;
        std::array<float,3> aim{};
        float lengthSquared=0;
        for (unsigned axis=0; axis<3; ++axis)
        {
            aim[axis]=original.forward[axis]*distance;
            if (axis==1) aim[axis]-=lift;
            lengthSquared+=aim[axis]*aim[axis];
        }
        if (!std::isfinite(result.location[1]) || !std::isfinite(lengthSquared) ||
            lengthSquared <= 0) return original;
        const float length=std::sqrt(lengthSquared);
        for (unsigned axis=0; axis<3; ++axis) result.forward[axis]=aim[axis]/length;
        return result;
    }
    inline float modernBoardControlReservation(const World3DRect& viewport,
        int pixelWidth, int pixelHeight, int controlsTop) noexcept
    {
        const auto ui=logicalviewport::makeTransform(pixelWidth,pixelHeight);
        const auto world=logicalviewport::makeWorld3DTransform(pixelWidth,pixelHeight);
        if(!ui.valid() || !world.valid() || viewport.empty())return 0;
        const auto pixels=logicalviewport::logicalToPixelRect(world,
            {double(viewport.left),double(viewport.top),double(viewport.right-viewport.left),
                double(viewport.bottom-viewport.top)});
        if(pixels.height<=0)return 0;
        const double uiTop=ui.offsetY+ui.scale*controlsTop;
        return float(std::clamp((pixels.y+pixels.height-uiTop)/pixels.height,0.0,0.4));
    }
    // Fit measured board bounds in the configured viewport while retaining authored roll.
    inline World3DCamera fitModernTopDownBoard(const data::MeshBounds& bounds,
        const World3DCamera& original, float aspect, float bottomReservedFraction = 0) noexcept
    {
        auto dot=[](const auto& a,const auto& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
        if(!std::isfinite(aspect) || aspect<=0 || !std::isfinite(bottomReservedFraction) || !std::isfinite(original.nearPlane) ||
            !std::isfinite(original.farPlane) || original.nearPlane<=0 ||
            original.farPlane<=original.nearPlane)return original;
        std::array<float,3> target{},forward=original.forward,up=original.up;
        for(unsigned axis=0;axis<3;++axis)
        {
            if(!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) ||
                bounds.maximum[axis]<bounds.minimum[axis] || !std::isfinite(original.location[axis]) ||
                !std::isfinite(forward[axis]) || !std::isfinite(up[axis]))return original;
            target[axis]=(bounds.minimum[axis]+bounds.maximum[axis])*.5F;
        }
        const float forwardLength=std::sqrt(dot(forward,forward));
        if(!std::isfinite(forwardLength) || forwardLength<=.000001F)return original;
        for(auto& value:forward)value/=forwardLength;
        const float parallel=dot(up,forward);
        for(unsigned axis=0;axis<3;++axis)up[axis]-=forward[axis]*parallel;
        const float upLength=std::sqrt(dot(up,up));
        if(!std::isfinite(upLength) || upLength<=.000001F)return original;
        for(auto& value:up)value/=upLength;
        const std::array<float,3> right{up[1]*forward[2]-up[2]*forward[1],
            up[2]*forward[0]-up[0]*forward[2],up[0]*forward[1]-up[1]*forward[0]};
        std::array<float,3> delta{};
        for(unsigned axis=0;axis<3;++axis)delta[axis]=target[axis]-original.location[axis];
        const float distance=dot(delta,forward);
        if(!std::isfinite(distance) || distance<=original.nearPlane)return original;
        auto camera=original;
        float fittedTan=0;
        for(unsigned iteration=0;iteration<8;++iteration)
        {
            for(unsigned axis=0;axis<3;++axis)camera.location[axis]=target[axis]-forward[axis]*distance;
            float minX=std::numeric_limits<float>::max(),minY=minX,maxX=-minX,maxY=-minX;
            for(unsigned corner=0;corner<8;++corner)
            {
                std::array<float,3> relative{};
                for(unsigned axis=0;axis<3;++axis)relative[axis]=
                    ((corner&(1U<<axis))?bounds.maximum[axis]:bounds.minimum[axis])-camera.location[axis];
                const float z=dot(relative,forward);
                if(!std::isfinite(z) || z<=original.nearPlane || z>=original.farPlane)return original;
                const float x=dot(relative,right)/z,y=dot(relative,up)/z;
                minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);
            }
            fittedTan=std::max(std::max(std::abs(minX),std::abs(maxX)),
                std::max(std::abs(minY),std::abs(maxY))*aspect);
            if(iteration<7)for(unsigned axis=0;axis<3;++axis)
                target[axis]+=distance*((minX+maxX)*.5F*right[axis]+(minY+maxY)*.5F*up[axis]);
        }
        if(!std::isfinite(fittedTan) || fittedTan<=0)return original;
        const float reserved=std::clamp(bottomReservedFraction,0.0F,0.4F);
        const float horizontalTan=fittedTan*1.08F/(1-reserved);
        camera.fieldOfView=2*std::atan(horizontalTan);
        for(unsigned axis=0;axis<3;++axis)
            camera.location[axis]-=up[axis]*distance*(horizontalTan/aspect)*reserved;
        return camera;
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
