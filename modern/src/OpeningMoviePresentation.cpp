#include "OpeningMoviePresentation.hpp"
#include "SequencePlayback.hpp"
#include <algorithm>
namespace monopoly::openingmovies
{
    std::expected<data::LegacyBitmapRGBA8,std::string> movieBackdrop(
        MovieRect movie, const HintRasterizer& rasterize)
    {
        const auto [left,top,right,bottom]=movie;
        // Frame must fit wholly outside the actual movie; full-screen/unknown layouts fall back.
        if(left<3 || top<3 || right>797 || bottom>597 || left>=right || top>=bottom)
            return std::unexpected("opening movie rectangle has no safe outside frame");
        data::LegacyBitmapRGBA8 image{800,600,std::vector<std::uint8_t>(800*600*4,0)};
        for(int y=0;y<600;++y)for(int x=0;x<800;++x)
        {
            if(x>=left && x<right && y>=top && y<bottom)continue;
            const bool rim=x>=left-3 && x<right+3 && y>=top-3 && y<bottom+3;
            const auto pixel=(std::size_t(y)*800+x)*4;
            image.pixels[pixel]=rim?188:13;
            image.pixels[pixel+1]=rim?157:35;
            image.pixels[pixel+2]=rim?94:38;image.pixels[pixel+3]=255;
        }
        if(bottom<=548)
        {
            if(!rasterize)return std::unexpected("opening skip hint font unavailable");
            auto text=rasterize("Press any key or click to skip");
            if(!text)return std::unexpected(text.error());
            if(!text->width || !text->height || text->width>752 || text->height>36 ||
                text->pixels.size()!=std::size_t(text->width)*text->height*4)
                return std::unexpected("opening skip hint raster cannot fit below movie");
            const unsigned ox=(800-text->width)/2,oy=unsigned(bottom)+12;
            for(unsigned y=0;y<text->height;++y)for(unsigned x=0;x<text->width;++x)
            {
                const auto src=(std::size_t(y)*text->width+x)*4;
                const auto dst=(std::size_t(oy+y)*800+ox+x)*4;
                const unsigned alpha=text->pixels[src+3];
                for(unsigned c=0;c<3;++c)
                    image.pixels[dst+c]=std::uint8_t((unsigned(text->pixels[src+c])*alpha+
                        unsigned(image.pixels[dst+c])*(255-alpha)+127)/255);
            }
        }
        return image;
    }

    std::expected<void,std::string> Presentation::reset(engine::SequencePlayback& playback)
    {
        if(surface_)
        {
            const auto stopped=playback.stop(*surface_,Priority);
            if(!stopped)return std::unexpected(stopped.error());
            (void)playback.runtimeBitmaps().remove(*surface_);
        }
        surface_.reset();rectangle_.reset();return {};
    }

    std::expected<void,std::string> Presentation::sync(std::optional<MovieRect> movie,
        const HintRasterizer& rasterize,engine::SequencePlayback& playback)
    {
        if(!movie)return reset(playback);
        if(surface_ && rectangle_==movie)return {};
        auto image=movieBackdrop(*movie,rasterize);
        if(!image)
        {
            if(const auto retired=reset(playback);!retired)return retired;
            return std::unexpected(image.error());
        }
        if(surface_)
        {
            const auto updated=playback.runtimeBitmaps().update(*surface_,std::move(*image),std::array<float,4>{0,0,800,600},true);
            if(!updated)return std::unexpected(updated.error());
            rectangle_=movie;return {};
        }
        const auto created=playback.runtimeBitmaps().create(800,600,true);
        if(!created)return std::unexpected(created.error());
        const auto updated=playback.runtimeBitmaps().update(*created,std::move(*image),std::array<float,4>{0,0,800,600},true);
        if(!updated){(void)playback.runtimeBitmaps().remove(*created);return std::unexpected(updated.error());}
        const auto started=playback.startMoved(*created,Priority,sequence::identity2D());
        if(!started){(void)playback.runtimeBitmaps().remove(*created);return std::unexpected(started.error());}
        surface_=*created;rectangle_=movie;return {};
    }
}
