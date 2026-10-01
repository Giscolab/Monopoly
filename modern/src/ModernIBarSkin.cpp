#include "ModernIBarSkin.hpp"
#include "IBarCameraButtonPlayback.hpp"
#include <algorithm>
#include <array>
#include <sstream>
#include <cmath>

namespace monopoly::ibar
{
    namespace
    {
        constexpr std::array<std::string_view, 28> French{
            "Enchères", "Acheter", "Vue", "Terminer", "Vendre", "Taxe fixe",
            "Pourcentage", "Construire", "Accepter", "Contre-offre", "Faillite",
            "Refuser", "Hypothéquer", "Plateau", "Options", "Payer",
            "Nouvelle partie", "Lancer", "Bilan", "Échanger", "Lever hyp.",
            "Quitter", "Rejouer", "Utiliser carte", "Enchère maison",
            "Enchère hôtel", "Placer maison", "Placer hôtel"};
        constexpr std::array<std::string_view, 28> English{
            "Auction", "Buy", "View", "Done", "Sell", "Flat tax", "Percentage",
            "Build", "Accept", "Counteroffer", "Bankrupt", "Reject", "Mortgage",
            "Board", "Options", "Pay", "New game", "Roll dice", "Status", "Trade",
            "Unmortgage", "Exit", "Play again", "Use card", "Auction house",
            "Auction hotel", "Place house", "Place hotel"};
        struct Button { unsigned index{}, state{}; bool grey{}; };
        std::optional<Button> button(data::DataId root)
        {
            if (data::dataGroup(root) != data::legacyGroupValue(data::LegacyGroupId::LanguageGraphics))
                return {};
            const auto tag = data::dataTag(root);
            for (const auto base : {ButtonBaseTag, AIButtonBaseTag})
                if (tag >= base && tag < base + 28 * ButtonAnimationsPerSet)
                    return Button{unsigned(tag - base) / 4, unsigned(tag - base) % 4,
                        base == AIButtonBaseTag};
            return {};
        }
        std::optional<layout::ActionButtonSlot> buttonSlot(unsigned index)
        {
            using S = layout::ActionButtonSlot;
            // Same semantic roles as IBar::dispatchDirectRuleAction/handleLocalRuleAction.
            switch (index)
            {
            case 0: case 6: case 11: case 12: case 23: return S::General3;
            case 1: case 3: case 5: case 8: case 10: case 17: case 21: case 24: case 25: return S::Main;
            case 2: return S::Camera;
            case 4: case 9: case 15: case 16: return S::General2;
            case 7: return S::General1;
            case 13: case 18: return S::Status;
            case 14: return S::Options;
            case 19: return S::Trade;
            case 20: return S::General4;
            default: return {}; // Presentation-only/unknown roles retain retail pixels.
            }
        }
        std::optional<unsigned> scoreColour(data::DataId root)
        {
            if (data::dataGroup(root) != data::legacyGroupValue(data::LegacyGroupId::Main)) return {};
            const auto tag = data::dataTag(root);
            if (tag >= 0x01CB && tag <= 0x01D6) return unsigned(tag - 0x01CB) % 6;
            return {};
        }
        // Same six identities as the existing BoardLightingController palette.
        constexpr std::array<std::array<int, 3>, 6> PlayerColours{{
            {255, 0, 0}, {0, 0, 255}, {60, 150, 60},
            {255, 255, 0}, {255, 0, 255}, {255, 128, 0}}};
        struct Property { unsigned index{}, style{}; bool miniature{}; };
        std::optional<Property> property(data::DataId root)
        {
            if (data::dataGroup(root) == data::legacyGroupValue(data::LegacyGroupId::Patterns))
            {
                const auto tag=data::dataTag(root);
                if(tag>=0x05E6 && tag<=0x0601) return Property{unsigned(tag-0x05E6),0,true};
                if(tag>=0x05CA && tag<=0x05E5) return Property{unsigned(tag-0x05CA),2,true};
                return {};
            }
            if (data::dataGroup(root) != data::legacyGroupValue(data::LegacyGroupId::Main)) return {};
            const auto tag = data::dataTag(root);
            if (tag < 0x0163 || tag > 0x01B6) return {};
            return Property{unsigned(tag - 0x0163) % 28, unsigned(tag - 0x0163) / 28};
        }
        // Authored display-language group colours from EuropeanDeed::propertyColor.
        constexpr std::array<std::array<int,3>,10> OrdinaryGroups{{
            {134,103,86}, {35,158,206}, {221,3,109}, {253,128,61},
            {223,0,0}, {255,255,0}, {37,154,57}, {45,65,144},
            {188,157,94}, {22,69,70}}};
        constexpr std::array<std::array<int,3>,10> FrenchGroups{{
            {217,91,187}, {35,158,206}, {134,0,157}, {237,181,0},
            {223,0,0}, {255,255,0}, {0,128,52}, {45,65,144},
            {188,157,94}, {22,69,70}}};
        bool valid(const data::LegacyBitmapRGBA8& image)
        { return image.width && image.height && image.pixels.size() == std::size_t(image.width) * image.height * 4; }
        void caption(data::LegacyBitmapRGBA8& destination, const data::LegacyBitmapRGBA8& source,
            unsigned top, unsigned height, unsigned ink)
        {
            const float scale = std::min({1.0F, float(destination.width - 12) / source.width,
                float(height) / source.height});
            const unsigned w = std::max(1U, unsigned(source.width * scale));
            const unsigned h = std::max(1U, unsigned(source.height * scale));
            const unsigned ox = (destination.width - w) / 2;
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x)
                {
                    const auto src = (std::size_t(y * source.height / h) * source.width + x * source.width / w) * 4;
                    const auto dst = (std::size_t(y + top) * destination.width + x + ox) * 4;
                    const unsigned alpha = source.pixels[src + 3];
                    for (unsigned c = 0; c < 3; ++c)
                        destination.pixels[dst+c] = std::uint8_t((ink * alpha + destination.pixels[dst+c] * (255-alpha) + 127) / 255);
                }
        }
        std::optional<unsigned> scoreToken(data::DataId root)
        {
            if (data::dataGroup(root) == data::legacyGroupValue(data::LegacyGroupId::Main) &&
                data::dataTag(root) >= 0x01C0 && data::dataTag(root) <= 0x01CA)
                return unsigned(data::dataTag(root) - 0x01C0);
            return {};
        }
        constexpr std::array<std::array<unsigned,2>,11> ScoreTokenSizes{{
            {53,29},{52,22},{39,29},{39,26},{39,26},{22,29},
            {54,32},{47,28},{22,29},{49,20},{24,31}}};
        std::optional<std::array<unsigned,4>> deedArtworkRect(data::DataId root,
            ModernIBarSkin::DeedArtwork kind)
        {
            if(data::dataGroup(root)!=data::legacyGroupValue(data::LegacyGroupId::LanguageGraphics)) return {};
            const auto tag=data::dataTag(root);
            if(kind==ModernIBarSkin::DeedArtwork::Railroad &&
                (tag==0x0CD2 || tag==0x0CDA || tag==0x0CE1 || tag==0x0CE9)) return std::array<unsigned,4>{68,18,125,61};
            if(kind==ModernIBarSkin::DeedArtwork::Electric && tag==0x0CD7) return std::array<unsigned,4>{74,16,114,63};
            if(kind==ModernIBarSkin::DeedArtwork::Water && tag==0x0CE4) return std::array<unsigned,4>{73,18,127,63};
            return {};
        }
        bool backdrop(data::DataId root)
        {
            return data::dataGroup(root) == data::legacyGroupValue(data::LegacyGroupId::Main) &&
                (data::dataTag(root) >= 0x015B && data::dataTag(root) <= 0x0160 ||
                 data::dataTag(root) == 0x0162);
        }
    }

    bool ModernIBarSkin::supports(data::DataId root) const noexcept
    {
        if(const auto p=property(root);p && p->miniature)
            return language_==data::LanguageId::EnglishUs && properties_ && propertyText_ && presentationContext_;
        const bool language = language_ == data::LanguageId::French ||
            language_ == data::LanguageId::EnglishUs || language_ == data::LanguageId::EnglishUk;
        if (scoreToken(root)) return language_ == data::LanguageId::EnglishUs && tokenImages_ && presentationContext_;
        if (deedText_ && deeds_.contains(root)) return true;
        if (drawText_ && drawCards_.contains(root)) return true;
        return language && text_ && (backdrop(root) || (button(root).has_value() && layout_) || scoreColour(root).has_value() ||
            (property(root).has_value() && properties_ && propertyText_));
    }

    std::shared_ptr<const data::BitmapRuntimeAsset> ModernIBarSkin::substitute(
        data::DataId root, std::shared_ptr<const data::BitmapRuntimeAsset> original, bool principal,
        std::optional<sequence::Matrix2D> rasterToWorld, std::optional<std::uint16_t> priority)
    {
        if (!original || !supports(root)) return original;
        const auto w = original->image.width, h = original->image.height;
        if (!w || !h || w > 1600 || h > 600) return original;
        const auto miniature=property(root);
        if(miniature && miniature->miniature &&
            (!principal || original->dataId!=root || original->sourceType!=data::LegacyDataType::Uap ||
             w!=36 || h!=42 || original->image.pixels.size()!=std::size_t(w)*h*4 ||
             !presentationContext_ || !presentationContext_())) return original;
        if (const auto token = scoreToken(root))
        {
            if (!principal || original->dataId != root || w != ScoreTokenSizes[*token][0] ||
                h != ScoreTokenSizes[*token][1] || !presentationContext_ || !presentationContext_() ||
                original->image.pixels.size() != std::size_t(w)*h*4) return original;
            const Key key{root,w,h,true,std::array<float,9>{},0,{},nullptr};
            if (const auto found=cache_.find(key);found!=cache_.end())return found->second.replacement;
            const auto source=tokenImages_(std::uint8_t(*token));
            if (!source || source->width!=768 || source->height!=640 || source->pixels.size()!=std::size_t(768)*640*4)
                return original;
            unsigned left=768,top=640,right=0,bottom=0;
            bool transparent=false;
            for(unsigned y=0;y<640;++y)for(unsigned x=0;x<768;++x)
            {
                if(source->pixels[(std::size_t(y)*768+x)*4+3])
                {left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}
                else transparent=true;
            }
            if(!transparent || right<=left || bottom<=top)return original;
            auto result=std::make_shared<data::BitmapRuntimeAsset>();
            result->dataId=original->dataId;result->sourceType=data::LegacyDataType::Native;
            result->preferLinearFiltering=true;
            result->presentationRect=std::array<float,4>{0,0,float(w),float(h)};
            auto& image=result->image;
            image={w*3,h*3,std::vector<std::uint8_t>(std::size_t(w)*h*36,0)};
            const double fit=std::min(double(image.width-6)/(right-left),double(image.height-6)/(bottom-top));
            const unsigned iw=std::max(1U,unsigned((right-left)*fit)),ih=std::max(1U,unsigned((bottom-top)*fit));
            const unsigned ox=(image.width-iw)/2,oy=(image.height-ih)/2;
            for(unsigned y=0;y<ih;++y)for(unsigned x=0;x<iw;++x)
            {
                const auto src=(std::size_t(top+y*(bottom-top)/ih)*768+left+x*(right-left)/iw)*4;
                const auto dst=(std::size_t(y+oy)*image.width+x+ox)*4;
                std::copy_n(source->pixels.data()+src,4,image.pixels.data()+dst);
            }
            if(cache_.size()>=128)cache_.clear();
            cache_.emplace(key,CachedArtwork{result,{}});return result;
        }
        const auto deed = deeds_.find(root);
        const auto draw = drawCards_.find(root);
        const bool fullCard = deed != deeds_.end() || draw != drawCards_.end();
        if ((fullCard || property(root).has_value()) && presentationContext_ && !presentationContext_()) return original;
        if (fullCard && presentationContext_ && !presentationContext_()) return original;
        if (deed != deeds_.end() && principal && (w != 199 || h != 227)) return original;
        std::optional<std::array<unsigned,4>> artwork;
        if(deed!=deeds_.end() && deed->second.artwork)
        {
            artwork=deedArtworkRect(root,*deed->second.artwork);
            if(!artwork || language_!=data::LanguageId::EnglishUs) return original;
            if(principal)
            {
                if(original->dataId!=root || !valid(original->image)) return original;
                bool ink=false;
                for(unsigned y=(*artwork)[1];y<(*artwork)[3];++y)
                    for(unsigned x=(*artwork)[0];x<(*artwork)[2];++x)
                    {
                        const auto i=(std::size_t(y)*w+x)*4;const auto& pixels=original->image.pixels;
                        if(pixels[i]!=pixels[i+1] || pixels[i]!=pixels[i+2] || pixels[i+3]!=255) return original;
                        ink|=pixels[i]<250;
                    }
                if(!ink) return original;
            }
        }
        if (draw != drawCards_.end() && principal &&
            (w != draw->second.nativeWidth || h != draw->second.nativeHeight || w < 199 || h < 150 || w > 600 || h > 400 ||
             draw->second.title.empty() || draw->second.title.size()>128 || draw->second.body.empty())) return original;
        std::optional<std::array<float,4>> deedPlacement;
        if (deed != deeds_.end() && principal && priority == 1002 && rasterToWorld && deedPlacement_)
        {
            if (!std::ranges::all_of(rasterToWorld->values, [](float value){return std::isfinite(value);})) return original;
            deedPlacement = deedPlacement_(root,*priority,*rasterToWorld);
            if (deedPlacement)
            {
                const auto& rect = *deedPlacement;
                if (!std::ranges::all_of(rect, [](float value){return std::isfinite(value);}) ||
                    rect[2] <= rect[0] || rect[3] <= rect[1] || rect[2]-rect[0] > 800 || rect[3]-rect[1] > 600)
                    return original;
            }
        }
        const auto b = !fullCard ? button(root) : std::optional<Button>{};
        if (b && presentationContext_ && !presentationContext_()) return original;
        layout::Rect band{0,0,int(w),int(h)};
        std::optional<layout::Rect> hit;
        const int activeLayout = b && layout_ ? int(layout_()) : 0;
        if (b && principal)
        {
            const auto slot = buttonSlot(b->index);
            if (!slot || !rasterToWorld) return original;
            hit = layout::actionButtonRect(*slot, layout::ActionButtonLayout(activeLayout));
            const auto& m = rasterToWorld->values;
            const float det = m[0]*m[4] - m[1]*m[3];
            if (!std::isfinite(det) || std::abs(det) < 1e-8F) return original;
            auto local = [&](float x, float y)
            {
                x -= m[6]; y -= m[7];
                return std::array<float,2>{(x*m[4]-y*m[3])/det, (y*m[0]-x*m[1])/det};
            };
            const auto center = local((hit->left+hit->right)*.5F,(hit->top+hit->bottom)*.5F);
            if (center[0] < 0 || center[0] >= w || center[1] < 0 || center[1] >= h) return original;
            const std::array corners{local(float(hit->left),float(hit->top)),
                local(float(hit->right),float(hit->top)), local(float(hit->left),float(hit->bottom)),
                local(float(hit->right),float(hit->bottom))};
            float left = float(w), top = float(h), right = 0, bottom = 0;
            for (const auto& c : corners)
            { left=std::min(left,c[0]); top=std::min(top,c[1]); right=std::max(right,c[0]); bottom=std::max(bottom,c[1]); }
            // A moving CNK that cannot carry its whole interactive band retains
            // its actual retail transition; idle artwork never centres in a glow.
            // Actual USA Trade idle UAP is60x32 at70,454, while its legacy
            // interactive rectangle ends at132: two clickable pixels are outside
            // the authored bitmap ending at130. Clip only this proven settled
            // payload; moving/pressed frames keep their original transition.
            const bool settledTrade = b->index == 19 && b->state < 3 && w == 60 && h == 32 &&
                original->dataId == data::packDataId(data::LegacyGroupId::LanguageGraphics,
                    b->grey ? 0x1628 : 0x140E) &&
                m == std::array<float,9>{1,0,0,0,1,0,70,454,1} &&
                left == 0 && top == 1 && right == 62 && bottom == 29;
            if (!settledTrade && (left < -.01F || top < -.01F || right > w+.01F || bottom > h+.01F)) return original;
            band = {std::clamp(int(std::floor(left)),0,int(w)),std::clamp(int(std::floor(top)),0,int(h)),
                std::clamp(int(std::ceil(right)),0,int(w)),std::clamp(int(std::ceil(bottom)),0,int(h))};
            if (band.right-band.left < 8 || band.bottom-band.top < 8) return original;
        }
        const Key key{root, w, h, principal, b && rasterToWorld ? rasterToWorld->values : std::array<float,9>{},
            activeLayout, deedPlacement, miniature && miniature->miniature ? original.get() : nullptr};
        if (const auto it = cache_.find(key); it != cache_.end()) return it->second.replacement;
        const auto p = property(root);
        std::optional<PropertyDescriptor> descriptor;
        if (p && principal)
        {
            descriptor = properties_(p->index);
            if (!descriptor || descriptor->name.empty() || descriptor->name.size() > 512 ||
                descriptor->purchaseText.size() > 128 || descriptor->group >= 10 ||
                w < 16 || h < 20 || w > 128 || h > 128) return original;
        }
        auto result = std::make_shared<data::BitmapRuntimeAsset>();
        result->dataId = original->dataId;
        result->sourceType = data::LegacyDataType::Native;
        result->preferLinearFiltering = true;
        result->presentationRect = deedPlacement;
        auto& image = result->image;
        image = {w, h, std::vector<std::uint8_t>(std::size_t(w) * h * 4)};
        if (fullCard)
        {
            DeedDescriptor drawPlan;
            const DeedDescriptor* content = deed != deeds_.end() ? &deed->second : &drawPlan;
            const auto& rasterizer = deed != deeds_.end() ? deedText_ : drawText_;
            if (draw != drawCards_.end())
            {
                const bool community = data::dataGroup(root)==data::legacyGroupValue(data::LegacyGroupId::LanguageGraphics) &&
                    data::dataTag(root)>=0x0059;
                drawPlan.fills={{14,10,int(w)-28,48,community ? (22U|(69U<<8)|(70U<<16)) : (162U|(98U<<8)|(33U<<16))}};
                drawPlan.text={{draw->second.title,17,32,1,2,22,0x00EFEFEF,true,false,true},
                    {draw->second.body,70,int(h)-91,1,0,16,0x00191919,false,false,false}};
            }
            image = {w*3,h*3,std::vector<std::uint8_t>(std::size_t(w)*h*36)};
            if (principal)
            {
                if (content->text.empty() || content->text.size() > 64 || content->fills.size() > 32) return original;
                for (unsigned y=0; y<image.height; ++y)
                    for (unsigned x=0; x<image.width; ++x)
                    {
                        const bool border = x<6 || y<6 || x+6>=image.width || y+6>=image.height;
                        const bool brass = !border && (x==8 || y==8 || x+9==image.width || y+9==image.height);
                        const std::array<unsigned,3> color = border ? std::array<unsigned,3>{22,69,70} :
                            brass ? std::array<unsigned,3>{188,157,94} : std::array<unsigned,3>{237,232,215};
                        const auto i=(std::size_t(y)*image.width+x)*4;
                        for (unsigned c=0;c<3;++c) image.pixels[i+c]=std::uint8_t(color[c]);
                        image.pixels[i+3]=255;
                    }
                for (const auto& fill : content->fills)
                {
                    if (fill.x<0 || fill.y<0 || fill.width<0 || fill.height<0 ||
                        fill.x>w || fill.y>h || fill.width>int(w)-fill.x || fill.height>int(h)-fill.y) return original;
                    for (int y=fill.y*3; y<(fill.y+fill.height)*3; ++y)
                        for (int x=fill.x*3; x<(fill.x+fill.width)*3; ++x)
                        {
                            const auto i=(std::size_t(y)*image.width+unsigned(x))*4;
                            for (unsigned c=0;c<3;++c) image.pixels[i+c]=std::uint8_t(fill.color>>(c*8));
                        }
                }
                if(artwork)
                    for(unsigned y=(*artwork)[1]*3;y<(*artwork)[3]*3;++y)
                        for(unsigned x=(*artwork)[0]*3;x<(*artwork)[2]*3;++x)
                        {
                            const auto source=(std::size_t(y/3)*w+x/3)*4;
                            const auto destination=(std::size_t(y)*image.width+x)*4;
                            // Original opaque white-backed grayscale yields
                            // exact ink coverage255-gray; no generated icon.
                            const unsigned gray=original->image.pixels[source];
                            for(unsigned c=0;c<3;++c)
                                image.pixels[destination+c]=std::uint8_t((image.pixels[destination+c]*gray+127)/255);
                        }
                for(std::size_t textIndex=0;textIndex<content->text.size();++textIndex)
                {
                    auto region=content->text[textIndex];
                    const bool artTitle=artwork && textIndex==0;
                    if(artTitle)
                    {region.y=70;region.height=20;region.fontSize=16;region.bold=true;region.verticalLeeway=0;region.verticalCenter=true;}
                    const unsigned textInset=artTitle?12U:21U;
                    if (region.text.empty()) continue;
                    if (region.text.size()>2048 || region.y<0 || region.y>=int(h) || region.height<=0 ||
                        region.height>int(h)-region.y || region.verticalLeeway<0 || region.verticalLeeway>12 ||
                        region.fontSize<=0 || region.fontSize>32 || region.justification<0 || region.justification>2) return original;
                    bool fitted=false;
                    for (int size=region.fontSize; size>=1 && !fitted; --size)
                    {
                        std::vector<data::LegacyBitmapRGBA8> lines;
                        std::istringstream words(region.text);
                        std::string word,line;
                        bool tooWide=false;
                        while (words>>word)
                        {
                            const auto candidate=line.empty()?word:line+" "+word;
                            auto measure=rasterizer(candidate,size*3,region.bold,region.italic);
                            if (!measure || !valid(*measure)) return original;
                            if (measure->width>(w-textInset*2)*3)
                            {
                                if (line.empty()) { tooWide=true; break; }
                                auto previous=rasterizer(line,size*3,region.bold,region.italic);
                                if (!previous || !valid(*previous)) return original;
                                lines.push_back(std::move(*previous)); line=word;
                                auto one=rasterizer(word,size*3,region.bold,region.italic);
                                if (!one || !valid(*one)) return original;
                                if (one->width>(w-textInset*2)*3) { tooWide=true; break; }
                            }
                            else line=candidate;
                        }
                        if (tooWide) continue;
                        if (line.empty()) return original;
                        auto last=rasterizer(line,size*3,region.bold,region.italic);
                        if (!last || !valid(*last)) return original;
                        lines.push_back(std::move(*last));
                        unsigned total=0;
                        for (const auto& rendered:lines) total+=rendered.height;
                        if (total>unsigned(region.height+region.verticalLeeway)*3) continue;
                        int y=region.y*3;
                        const int overflow=std::max(0,int(total)-region.height*3);
                        if (overflow<=region.verticalLeeway*3) y+=overflow;
                        if (region.verticalCenter && lines.size()==1) y+=(region.height*3-int(total))/2;
                        for (const auto& rendered:lines)
                        {
                            const int x=region.justification==0?int(textInset*3):region.justification==1?
                                (int(image.width)-int(rendered.width))/2:int(image.width)-int(textInset*3)-int(rendered.width);
                            if (x<0 || y<0 || x+int(rendered.width)>int(image.width) || y+int(rendered.height)>int(image.height)) return original;
                            for (unsigned py=0;py<rendered.height;++py)
                                for (unsigned px=0;px<rendered.width;++px)
                                {
                                    const auto src=(std::size_t(py)*rendered.width+px)*4;
                                    const auto dst=(std::size_t(y+int(py))*image.width+x+px)*4;
                                    const unsigned alpha=rendered.pixels[src+3];
                                    std::uint32_t inkColor=region.color;
                                    // Modern title contrast changes ink only; the exact
                                    // title/canonical group colour remain unchanged.
                                    if (region.bold && region.verticalCenter)
                                        for (const auto& fill:content->fills)
                                            if (region.y>=fill.y && region.y<fill.y+fill.height &&
                                                0.2126*(fill.color&255)+0.7152*((fill.color>>8)&255)+0.0722*((fill.color>>16)&255)<125)
                                                inkColor=0x00EFEFEF;
                                    for (unsigned c=0;c<3;++c)
                                    {
                                        const unsigned ink=(inkColor>>(c*8))&255;
                                        image.pixels[dst+c]=std::uint8_t((ink*alpha+image.pixels[dst+c]*(255-alpha)+127)/255);
                                    }
                                }
                            y+=int(rendered.height);
                        }
                        fitted=true;
                    }
                    if (!fitted) return original;
                }
            }
            if (cache_.size()>=128) cache_.clear();
            cache_.emplace(key,CachedArtwork{result,{}});
            return result;
        }
        if (p)
        {
            image = {w * 3, h * 3, std::vector<std::uint8_t>(std::size_t(w) * h * 36)};
            if (principal)
            {
                const auto group = (language_ == data::LanguageId::French ? FrenchGroups : OrdinaryGroups)[descriptor->group];
                for (unsigned y = 0; y < image.height; ++y)
                    for (unsigned x = 0; x < image.width; ++x)
                    {
                        const auto i = (std::size_t(y) * image.width + x) * 4;
                        const bool border = x < 3 || x + 3 >= image.width || y < 3 || y + 3 >= image.height;
                        const bool groupBand = !border && y < 22;
                        const bool mortgage = p->style == 2;
                        for (unsigned c = 0; c < 3; ++c)
                        {
                            int colour = border ? (mortgage ? std::array<int,3>{155,57,54}[c] : std::array<int,3>{22,69,70}[c]) :
                                groupBand ? group[c] : (mortgage ? std::array<int,3>{242,222,216}[c] : std::array<int,3>{237,232,215}[c]);
                            if (p->style == 1 && groupBand) colour = (colour + 2 * 155) / 3;
                            // A red diagonal across the header marks the mortgaged
                            // state without replacing or inventing translated text.
                            if (mortgage && groupBand && (x + y) % image.width < 4) colour = std::array<int,3>{155,57,54}[c];
                            image.pixels[i+c] = std::uint8_t(colour);
                        }
                        image.pixels[i+3] = 255;
                    }
                std::vector<data::LegacyBitmapRGBA8> lines;
                std::istringstream words(descriptor->name);
                std::string word, line;
                while (words >> word)
                {
                    const auto candidate = line.empty() ? word : line + " " + word;
                    auto measured = propertyText_(candidate);
                    if (!measured || !valid(*measured)) return original;
                    if (!line.empty() && measured->width > image.width - 12)
                    {
                        auto previous = propertyText_(line);
                        if (!previous || !valid(*previous)) return original;
                        lines.push_back(std::move(*previous));
                        line = word;
                    }
                    else line = candidate;
                }
                if (line.empty()) return original;
                auto last = propertyText_(line);
                if (!last || !valid(*last)) return original;
                lines.push_back(std::move(*last));
                const unsigned textHeight = image.height - 54;
                if (lines.size() > textHeight) return original;
                const unsigned lineHeight = std::max(1U, textHeight / unsigned(lines.size()));
                unsigned y = 28;
                for (const auto& name : lines)
                {
                    caption(image, name, y, lineHeight, p->style == 1 ? 115 : 25);
                    y += lineHeight;
                }
                if (!descriptor->purchaseText.empty())
                {
                    auto price = propertyText_(descriptor->purchaseText);
                    if (!price || !valid(*price)) return original;
                    caption(image, *price, image.height - 23, 17, p->style == 1 ? 115 : 25);
                }
            }
            if(p->miniature)
                for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x)
                    image.pixels[(std::size_t(y)*image.width+x)*4+3]=original->image.pixels[(std::size_t(y/3)*w+x/3)*4+3];
            if (cache_.size() >= 128) cache_.clear();
            cache_.emplace(key, CachedArtwork{result,p->miniature?original:nullptr});
            return result;
        }
        const auto playerColour = scoreColour(root);
        if (principal)
        {
            for (unsigned y = unsigned(band.top); y < unsigned(band.bottom); ++y)
                for (unsigned x = unsigned(band.left); x < unsigned(band.right); ++x)
                {
                    const auto i = (std::size_t(y) * w + x) * 4;
                    const bool edge = x == unsigned(band.left) || y == unsigned(band.top) ||
                        x + 1 == unsigned(band.right) || y + 1 == unsigned(band.bottom);
                    const bool corner = b && ((x < unsigned(band.left+3) || x+3 >= unsigned(band.right)) &&
                        (y < unsigned(band.top+3) || y+3 >= unsigned(band.bottom)));
                    if (corner) continue;
                    const int gradient = int(8 * (h - y) / h);
                    const bool pressed = b && b->state == 3;
                    const bool grey = b && b->grey;
                    const std::array<int, 3> fill = playerColour ? std::array<int, 3>{237, 232, 215} : grey ? std::array<int, 3>{57, 64, 65} :
                        pressed ? std::array<int, 3>{13, 47, 48} : std::array<int, 3>{22, 69, 70};
                    const std::array<int, 3> trim = playerColour ? std::array<int, 3>{22, 69, 70} : grey ? std::array<int, 3>{104, 111, 111} :
                        std::array<int, 3>{188, 157, 94};
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        // Black name/cash overlays require a light interior. The
                        // left player stripe does not enter their authored x>=57 region.
                        const bool stripe = playerColour && x >= 2 && x < 5 && y >= 2 && y + 2 < h;
                        const bool brass = playerColour && !edge && y == 1;
                        image.pixels[i + c] = std::uint8_t(stripe ? PlayerColours[*playerColour][c] :
                            brass ? std::array<int, 3>{188, 157, 94}[c] :
                            edge ? trim[c] : fill[c] + (playerColour ? 0 : gradient));
                    }
                    image.pixels[i + 3] = b || playerColour ? 255 : 244;
                }
            if (b)
            {
                auto label = text_((language_ == data::LanguageId::French ? French : English)[b->index]);
                if (!label || !label->width || !label->height ||
                    label->pixels.size() != std::size_t(label->width) * label->height * 4 || w < 8 || h < 8)
                    return original;
                const float scale = std::min({1.0F, float(band.right-band.left-8) / label->width, float(band.bottom-band.top-6) / label->height});
                const unsigned tw = std::max(1U, unsigned(label->width * scale));
                const unsigned th = std::max(1U, unsigned(label->height * scale));
                const unsigned ox = unsigned(band.left) + (unsigned(band.right-band.left) - tw) / 2,
                    oy = unsigned(band.top) + (unsigned(band.bottom-band.top) - th) / 2;
                for (unsigned y = 0; y < th; ++y)
                    for (unsigned x = 0; x < tw; ++x)
                    {
                        const auto src = (std::size_t(y * label->height / th) * label->width + x * label->width / tw) * 4;
                        const auto dst = (std::size_t(y + oy) * w + x + ox) * 4;
                        const unsigned a = label->pixels[src + 3];
                        for (unsigned c = 0; c < 3; ++c)
                        {
                            const unsigned ink = b->grey ? 180 : (c == 2 ? 214 : 239);
                            image.pixels[dst + c] = std::uint8_t((ink * a + image.pixels[dst + c] * (255 - a) + 127) / 255);
                        }
                    }
            }
        }
        if (b && principal && hit && rasterToWorld)
        {
            const auto& m = rasterToWorld->values;
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x)
                {
                    const float wx = (x+.5F)*m[0] + (y+.5F)*m[3] + m[6];
                    const float wy = (x+.5F)*m[1] + (y+.5F)*m[4] + m[7];
                    if (wx < hit->left || wx >= hit->right || wy < hit->top || wy >= hit->bottom)
                        image.pixels[(std::size_t(y)*w+x)*4+3] = 0;
                }
        }
        if (cache_.size() >= 128) cache_.clear();
        cache_.emplace(key, CachedArtwork{result,{}});
        return result;
    }
}
