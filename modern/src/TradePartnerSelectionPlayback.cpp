#include "TradePartnerSelectionPlayback.hpp"
#include "FontRuntime.hpp"
#include "LanguageResources.hpp"
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <sstream>
namespace monopoly::tradeui
{
    namespace
    {
        std::optional<data::LegacyBitmapRGBA8> thumbnailWell(const data::LegacyBitmapRGBA8& source)
        {
            if(source.width!=768 || source.height!=640 || source.pixels.size()!=std::size_t(768)*640*4)
                return std::nullopt;
            unsigned left=768,top=640,right=0,bottom=0;bool transparent=false,visible=false;
            for(unsigned y=0;y<640;++y)for(unsigned x=0;x<768;++x)
            {
                const auto alpha=source.pixels[(std::size_t(y)*768+x)*4+3];
                transparent|=alpha==0;
                if(!alpha)continue;
                visible=true;left=std::min(left,x);top=std::min(top,y);
                right=std::max(right,x+1);bottom=std::max(bottom,y+1);
            }
            if(!visible || !transparent)return std::nullopt;
            constexpr unsigned width=44*3,height=28*3;
            data::LegacyBitmapRGBA8 result{width,height,std::vector<std::uint8_t>(width*height*4,0)};
            const unsigned cropWidth=right-left,cropHeight=bottom-top;
            const double scale=std::min(double(width)/cropWidth,double(height)/cropHeight);
            const unsigned drawWidth=std::clamp(unsigned(std::lround(cropWidth*scale)),1u,width);
            const unsigned drawHeight=std::clamp(unsigned(std::lround(cropHeight*scale)),1u,height);
            const unsigned offsetX=(width-drawWidth)/2,offsetY=(height-drawHeight)/2;
            for(unsigned y=0;y<drawHeight;++y)for(unsigned x=0;x<drawWidth;++x)
            {
                const unsigned sx=left+std::min(cropWidth-1,unsigned((std::uint64_t(x)*cropWidth)/drawWidth));
                const unsigned sy=top+std::min(cropHeight-1,unsigned((std::uint64_t(y)*cropHeight)/drawHeight));
                std::copy_n(source.pixels.data()+(std::size_t(sy)*768+sx)*4,4,
                    result.pixels.data()+(std::size_t(y+offsetY)*width+x+offsetX)*4);
            }
            return result;
        }
    }
    void PartnerSelectionPlayback::configureTokenImages(TokenImageProvider provider)
    {
        tokenImages_=std::move(provider);key_.clear();
    }
    std::expected<void,std::string> PartnerSelectionPlayback::reset(engine::SequencePlayback& playback)
    {
        if(visible_)
            if(const auto stopped=playback.stop(surface_,PlayerSelectPriority);!stopped)return stopped;
        if(surface_!=data::EmptyDataId)(void)playback.runtimeBitmaps().remove(surface_);
        surface_=data::EmptyDataId;visible_=false;key_.clear();resources_=nullptr;bitmaps_.clear();return {};
    }
    std::expected<void,std::string> PartnerSelectionPlayback::sync(const State& state,
        const rules::GameState& game,display::Screen2D view,fonts::Runtime* font,
        engine::SequencePlayback& playback,bool modernPresentation)
    {
        const bool desired=view==display::Screen2D::Trade && state.playerSelectVisible;
        if(!desired)
        {
            if(visible_)
            { if(const auto stopped=playback.stop(surface_,PlayerSelectPriority);!stopped)return stopped;visible_=false; }
            return {};
        }
        if(!font || !font->ready())return std::unexpected("Trade partner chooser font unavailable");
        const auto resources=playback.resources();
        if(!resources || !resources->language() || !resources->language()->catalog)
            return std::unexpected("Trade partner chooser LANG unavailable");
        const bool modern=modernPresentation && resources->context().board==data::BoardEdition::Usa &&
            resources->context().language==data::LanguageId::EnglishUs;
        if(resources_!=resources){bitmaps_.clear();key_.clear();resources_=resources;}
        const auto title16=resources->language()->catalog->message(2015); // TMN_TRADE_SELECT
        if(!title16)return std::unexpected(title16.error().detail);
        const auto title=fonts::transcodeUtf8(std::u16string_view(**title16));
        if(!title)return std::unexpected(title.error().detail);
        struct Row { std::string label; rules::PlayerNumber player; unsigned token; Rect rect; };
        std::vector<Row> rows;std::ostringstream signature;
        signature<<modern<<'|'<<*title<<'|'<<font->settings().fontPath.generic_string();
        for(rules::PlayerNumber player=0;player<game.numberOfPlayers && player<rules::MaxPlayers;++player)
        {
            const auto rect=playerTokenRect(state,game,player);if(!rect)continue;
            if(game.players[player].token>10)return std::unexpected("Trade partner token outside retail catalog");
            const auto name=fonts::transcodeUtf8(std::wstring_view(game.players[player].name));
            if(!name)return std::unexpected(name.error().detail);
            std::ostringstream label;label<<std::setw(6)<<unsigned(player+1)<<": "<<*name;
            rows.push_back({label.str(),player,game.players[player].token,*rect});
            signature<<'|'<<unsigned(player)<<':'<<game.players[player].token<<':'<<rect->top<<':'<<name->size()<<':'<<*name;
        }
        if(rows.empty())return std::unexpected("Trade partner chooser has no eligible players");
        const auto nextKey=signature.str();
        if(nextKey!=key_ || !surface_)
        {
            const auto load=[&](data::DataId id)->std::expected<std::shared_ptr<const data::BitmapRuntimeAsset>,std::string>
            {
                const auto metadata=resources->data().metadata(id);const auto bytes=resources->data().load(id);
                if(!metadata)return std::unexpected(metadata.error().detail);
                if(!bytes)return std::unexpected(bytes.error().detail);
                const auto asset=bitmaps_.resolve(id,metadata->type,*bytes);
                if(!asset)return std::unexpected(asset.error().detail);return *asset;
            };
            const auto paper=load(0x0003058F); // TAB_tnfi0 exact native chooser template
            if(!paper)return std::unexpected(paper.error());
            if((*paper)->image.width!=188 || (*paper)->image.height!=209)
                return std::unexpected("Trade partner chooser template must be188x209");
            const unsigned scale=modern?3:1;
            data::LegacyBitmapRGBA8 image{188*scale,209*scale,std::vector<std::uint8_t>(188*209*scale*scale*4,255)};
            for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x)
            {
                const auto dst=(std::size_t(y)*image.width+x)*4;
                if(modern)
                {
                    const bool rim=x<3 || y<3 || x+3>=image.width || y+3>=image.height;
                    image.pixels[dst]=rim?188:22;image.pixels[dst+1]=rim?157:60;image.pixels[dst+2]=rim?94:61;
                }
                else
                { const auto src=(std::size_t(y)*188+x)*4;for(unsigned c=0;c<4;++c)image.pixels[dst+c]=(*paper)->image.pixels[src+c]; }
            }
            const auto text=[&](std::string_view value,int x,int y,bool center)->std::expected<void,std::string>
            {
                const auto raster=font->renderPresentation(value,modern?0xD3EBF5:0xDCDCDC,
                    int((modern?(center?9:10):8)*scale),400,false,false,false,modern);
                if(!raster)return std::unexpected(raster.error().detail);
                if(center)x=(int(image.width)-int(raster->width))/2;
                data::LegacyBitmapRGBA8 clipped=*raster;
                if(!center && clipped.width>116*scale)
                {
                    clipped.width=116*scale;clipped.pixels.resize(std::size_t(clipped.width)*clipped.height*4);
                    for(unsigned row=0;row<clipped.height;++row)
                        std::copy_n(raster->pixels.data()+std::size_t(row)*raster->width*4,clipped.width*4,
                            clipped.pixels.data()+std::size_t(row)*clipped.width*4);
                }
                const auto blit=data::blitStraightRGBA8(image,clipped,x,y,data::BitmapBlitMode::SourceOver);
                if(!blit)return std::unexpected(blit.error());return {};
            };
            if(const auto rendered=text(*title,0,12*scale,true);!rendered)return rendered;
            for(const auto& row:rows)
            {
                const int localTop=row.rect.top-PlayerSelectY;
                if(const auto rendered=text(row.label,5*scale,(localTop+9)*scale,false);!rendered)return rendered;
                const auto token=load(0x000201C0+row.token);if(!token)return std::unexpected(token.error());
                const auto& source=(*token)->image;
                if(!source.width || !source.height || source.width>128 || source.height>128)
                    return std::unexpected("Trade partner token dimensions invalid");
                if(modern && tokenImages_)
                {
                    std::shared_ptr<const data::LegacyBitmapRGBA8> thumbnail;
                    try { thumbnail=tokenImages_(static_cast<std::uint8_t>(row.token)); }
                    catch (...) { thumbnail.reset(); }
                    if(thumbnail)
                        if(const auto well=thumbnailWell(*thumbnail))
                        {
                            const auto blit=data::blitStraightRGBA8(image,*well,126*scale,localTop*scale,
                                data::BitmapBlitMode::SourceOver);
                            if(!blit)return std::unexpected(blit.error());
                            continue;
                        }
                }
                data::LegacyBitmapRGBA8 enlarged{source.width*scale,source.height*scale,
                    std::vector<std::uint8_t>(source.pixels.size()*scale*scale)};
                for(unsigned y=0;y<enlarged.height;++y)for(unsigned x=0;x<enlarged.width;++x)
                    for(unsigned c=0;c<4;++c)enlarged.pixels[(std::size_t(y)*enlarged.width+x)*4+c]=
                        source.pixels[(std::size_t(y/scale)*source.width+x/scale)*4+c];
                const int x=126+(44-int(source.width))/2;
                const int y=localTop+(28-int(source.height))/2;
                const auto blit=data::blitStraightRGBA8(image,enlarged,x*scale,y*scale,data::BitmapBlitMode::SourceOver);
                if(!blit)return std::unexpected(blit.error());
            }
            bool created=false;
            if(!surface_)
            { const auto id=playback.runtimeBitmaps().create(188,209,true);if(!id)return std::unexpected(id.error());surface_=*id;created=true; }
            const auto updated=playback.runtimeBitmaps().update(surface_,std::move(image),
                modern?std::optional<std::array<float,4>>{{0,0,188,209}}:std::nullopt,modern);
            if(!updated){if(created){(void)playback.runtimeBitmaps().remove(surface_);surface_=data::EmptyDataId;}return updated;}
            key_=nextKey;
        }
        if(!visible_)
        {
            const auto started=playback.startMoved(surface_,PlayerSelectPriority,sequence::translate2D(PlayerSelectX,PlayerSelectY));
            if(!started)return started;visible_=true;
        }
        return {};
    }
}
