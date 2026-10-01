#include "OpeningMoviePresentation.hpp"
#include "SequencePlayback.hpp"
#include "SyntheticSequenceResources.hpp"
#include <iostream>
#include <stdexcept>
namespace
{
    using namespace monopoly;
    void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
    data::LegacyBitmapRGBA8 hint(){return {240,24,std::vector<std::uint8_t>(240*24*4,255)};}
    void testPixels()
    {
        unsigned calls=0;
        const auto raster=[&](std::string_view text)->std::expected<data::LegacyBitmapRGBA8,std::string>
        {require(text=="Press any key or click to skip","Skip hint matches actual input contract");++calls;return hint();};
        const auto image=openingmovies::movieBackdrop({200,150,600,450},raster);
        require(image && image->width==800 && image->height==600 && calls==1,"Retail400x300 movie gets native backdrop and hint");
        for(unsigned y=150;y<450;++y)for(unsigned x=200;x<600;++x)
            for(unsigned c=0;c<4;++c)require(image->pixels[(std::size_t(y)*800+x)*4+c]==0,
                "Every movie aperture channel stays transparent: no altered pixels or overlay");
        const auto rim=(std::size_t(149)*800+400)*4;
        require(image->pixels[rim]==188 && image->pixels[rim+1]==157 && image->pixels[rim+2]==94 &&
            image->pixels[rim+3]==255,"Brass frame is strictly outside movie");
        require(image->pixels[0]==13 && image->pixels[1]==35 && image->pixels[2]==38 &&
            image->pixels[(std::size_t(474)*800+400)*4]==255,"Teal background and readable below-film hint");
        require(!openingmovies::movieBackdrop({0,0,800,600},raster) &&
            !openingmovies::movieBackdrop({600,150,200,450},raster),"Fullviewport and unknown rectangles retain fallback");
        require(openingmovies::movieBackdrop({10,10,790,580},raster).has_value() && calls==1,
            "No room below larger film omits hint without touching movie");
        require(!openingmovies::movieBackdrop({200,150,600,450},{}),"Missing hint font fails closed");
    }
    void testLifetime()
    {
        SyntheticSequenceResources fixture;
        engine::SequencePlayback playback(fixture.service.snapshot());
        const auto film=playback.runtimeBitmaps().create(400,300,false);
        require(film.has_value(),"Create independent native movie surface fixture");
        data::LegacyBitmapRGBA8 movie{400,300,std::vector<std::uint8_t>(400*300*4,93)};
        // Production prepareVideoFrame(drawSolid=true) publishes opaque movie alpha.
        for(std::size_t i=3;i<movie.pixels.size();i+=4)movie.pixels[i]=255;
        require(playback.runtimeBitmaps().update(*film,movie).has_value() &&
            playback.startMoved(*film,1100,sequence::translate2D(200,150)).has_value() && playback.update(0).has_value(),
            "Independent movie owner has authored transform/priority");
        const auto roots=playback.runtime().roots();require(roots.size()==1,"Movie fixture root exists");
        const auto before=*playback.runtime().inspect(roots[0]);
        const auto filmAsset=playback.runtimeBitmaps().asset(*film);
        openingmovies::Presentation decor;unsigned calls=0;
        const auto raster=[&](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>{++calls;return hint();};
        const auto firstSync=decor.sync(openingmovies::MovieRect{200,150,600,450},raster,playback);
        if(!firstSync)throw std::runtime_error("Opening decoration publish: "+firstSync.error());
        require(decor.active() &&
            playback.runtimeBitmaps().size()==2,"Exactly one owned decoration surface");
        require(decor.sync(openingmovies::MovieRect{200,150,600,450},raster,playback).has_value() && calls==1,
            "Unchanged film geometry reuses backdrop without per-frame font work");
        const auto after=*playback.runtime().inspect(roots[0]);
        require(after.clock==before.clock && after.priority==1100 &&
            playback.runtimeBitmaps().asset(*film)==filmAsset && filmAsset->image.pixels==movie.pixels,
            "Decoration sync leaves movie clock/priority/immutable pixels untouched");
        require(playback.update(0).has_value(),"Publish decoration through existing overlay slot");
        require(decor.sync({},raster,playback).has_value() && !decor.active() && playback.update(0).has_value() &&
            playback.runtimeBitmaps().size()==1 && playback.runtime().inspect(roots[0]).has_value(),
            "Skip/EOF/context removal retires only decoration owner");
        require(decor.sync(openingmovies::MovieRect{200,150,600,450},raster,playback).has_value(),"Restart movie decoration");
        require(!decor.sync(openingmovies::MovieRect{0,0,800,600},raster,playback) && !decor.active() &&
            playback.runtimeBitmaps().size()==1,"Unsupported new layout removes stale decoration and falls back");
        require(decor.reset(playback).has_value(),"Retirement is idempotent");
    }
}
int main()
{
    try{testPixels();testLifetime();std::cout<<"[PASS] opening movie decoration pixels/cache/owned lifetime (CPU only)\n";return 0;}
    catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}
}
