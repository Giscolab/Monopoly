#include "PlayerSelectionPlayback.hpp"
#include "PlayerSelectionCatalog.hpp"
#include "MoneyFormat.hpp"
#include "FontRuntime.hpp"

#include <algorithm>
#include <SDL3/SDL.h>

namespace monopoly::playerselection
{
    namespace detail
    {
        std::expected<std::vector<std::string>, std::string> wrapRuleDescription(
            fonts::Runtime& font, std::string_view value, int maxPixelWidth, std::size_t maxLines)
        {
            if (maxPixelWidth <= 0)
                return std::unexpected("Player selection rule wrap width must be positive");

            auto nextBoundary = [](std::string_view text, std::size_t offset) noexcept
            {
                if (offset >= text.size()) return text.size();
                ++offset;
                while (offset < text.size() &&
                       (static_cast<unsigned char>(text[offset]) & 0xC0U) == 0x80U)
                    ++offset;
                return offset;
            };

            std::vector<std::string> lines;
            std::string remaining(value);
            while (!remaining.empty() && lines.size() < maxLines)
            {
                const auto whole = font.measure(remaining);
                if (!whole) return std::unexpected(whole.error().detail);
                if (whole->width <= maxPixelWidth)
                {
                    lines.push_back(std::move(remaining));
                    break;
                }

                std::size_t cut = std::string::npos;
                std::size_t search = remaining.size();
                while (search != 0)
                {
                    const auto space = remaining.rfind(' ', search - 1);
                    if (space == std::string::npos) break;
                    const auto prefix = font.measure(std::string_view(remaining).substr(0, space));
                    if (!prefix) return std::unexpected(prefix.error().detail);
                    if (prefix->width <= maxPixelWidth)
                    {
                        cut = space;
                        break;
                    }
                    search = space;
                }

                if (cut != std::string::npos)
                {
                    lines.push_back(remaining.substr(0, cut));
                    remaining.erase(0, cut + 1);
                    continue;
                }

                // udpsel_WordWrap hard-breaks the first word when no space-delimited
                // prefix fits. Consume at least one whole UTF-8 code point.
                auto wordEnd = remaining.find(' ');
                if (wordEnd == std::string::npos) wordEnd = remaining.size();
                std::size_t best{};
                for (std::size_t end = nextBoundary(remaining, 0);
                     end != 0 && end <= wordEnd;)
                {
                    const auto prefix = font.measure(std::string_view(remaining).substr(0, end));
                    if (!prefix) return std::unexpected(prefix.error().detail);
                    if (prefix->width > maxPixelWidth)
                    {
                        if (best == 0) best = end;
                        break;
                    }
                    best = end;
                    if (end == wordEnd) break;
                    const auto next = nextBoundary(remaining, end);
                    if (next == end) break;
                    end = next;
                }
                if (best == 0) best = nextBoundary(remaining, 0);
                lines.push_back(remaining.substr(0, best));
                remaining.erase(0, best);
            }
            return lines;
        }
    }

    namespace
    {
        using P = ui::playersetup::Phase;
        using O = detail::Object;
        using Spec = detail::ObjectSpec;
        constexpr std::array<int, 8> CardX{35,141,241,344,36,141,242,345};
        constexpr std::array<int, 8> CardY{253,253,253,253,370,371,371,372};
        constexpr std::array<std::array<int,4>,16> Values{{
            {{4,5,0,0}}, {{12,32,60,88}}, {{4,12,16,22}}, {{0,250,500,750}},
            {{500,1000,1500,2000}}, {{0,100,200,400}}, {{0,5,10,15}},
            {{0,100,200,400}}, {{0,75,150,300}}, {{1,2,3,4}}, {{0,50,100,200}},
            {{0,1,6,12}}, {{0,1,3,6}}, {{0,5,10,20}}, {{3,4,5,10}}, {{0,2,4,28}}
        }};

        template<class Char> std::string utf8(std::basic_string_view<Char> value)
        {
            const char* encoding = sizeof(Char) == 2 ? "UTF-16LE" : "UTF-32LE";
            const std::basic_string<Char> terminated(value);
            char* converted = SDL_iconv_string("UTF-8", encoding,
                reinterpret_cast<const char*>(terminated.c_str()), (value.size()+1)*sizeof(Char));
            if (!converted) return {};
            std::string result(converted);
            SDL_free(converted);
            return result;
        }

        std::expected<std::string,std::string> text(
            const engine::SequencePlayback& playback, std::uint32_t id)
        {
            const auto resources = playback.resources();
            if (!resources || !resources->language() || !resources->language()->catalog)
                return std::unexpected("Player selection language catalog unavailable");
            auto result = resources->language()->catalog->message(id);
            if (!result) return std::unexpected(result.error().detail);
            return utf8<char16_t>(**result);
        }

        struct RestoreFont
        {
            fonts::Runtime& font;
            ~RestoreFont() { (void)font.restoreSettings(9); }
        };

        bool enabled(O object, const RenderState& s)
        {
            switch(object)
            {
                case O::ENTERNAME_BUTTON_NEXT: return s.setup.name.size()>1;
                case O::SELECTPLAYER_BUTTON_MORE: return s.setup.playerLogCount>8;
                case O::START_BUTTON_ADDHUMAN:
                case O::START_BUTTON_ADDCOMPUTER: return s.game.numberOfPlayers<rules::MaxPlayers;
                case O::START_BUTTON_REMOVEPLAYER: return s.localPlayers>0;
                case O::START_BUTTON_STARTGAME:
                    return s.setup.serverMode && s.game.numberOfPlayers>=2 &&
                        std::any_of(s.game.players.begin(), s.game.players.begin()+
                            std::min<std::size_t>(s.game.numberOfPlayers,rules::MaxPlayers),
                            [](const auto& p){return p.aiPlayerLevel==0;});
                case O::RULES_BUTTON_OKAY: return s.setup.serverMode;
                default: break;
            }
            constexpr std::array<O,11> buttons{O::PICKTOKEN_BUTTON_CANNON,
                O::PICKTOKEN_BUTTON_RACECAR,O::PICKTOKEN_BUTTON_DOG,O::PICKTOKEN_BUTTON_TOPHAT,
                O::PICKTOKEN_BUTTON_IRON,O::PICKTOKEN_BUTTON_HORSE,O::PICKTOKEN_BUTTON_BATTLESHIP,
                O::PICKTOKEN_BUTTON_SHOE,O::PICKTOKEN_BUTTON_THIMBLE,
                O::PICKTOKEN_BUTTON_WHEELBARROW,O::PICKTOKEN_BUTTON_SACKOFMONEY};
            constexpr std::array<O,11> rotating{O::PICKTOKEN_ROTATING_CANNON,
                O::PICKTOKEN_ROTATING_RACECAR,O::PICKTOKEN_ROTATING_DOG,O::PICKTOKEN_ROTATING_TOPHAT,
                O::PICKTOKEN_ROTATING_IRON,O::PICKTOKEN_ROTATING_HORSE,O::PICKTOKEN_ROTATING_BATTLESHIP,
                O::PICKTOKEN_ROTATING_SHOE,O::PICKTOKEN_ROTATING_THIMBLE,
                O::PICKTOKEN_ROTATING_WHEELBARROW,O::PICKTOKEN_ROTATING_SACKOFMONEY};
            for (std::uint8_t i=0;i<11;++i)
            {
                if (object==buttons[i]) return ui::playersetup::tokenAvailable(s.game,i);
                if (object==rotating[i]) return s.setup.token==i;
            }
            return true;
        }
    }

    std::expected<void,std::string> PlayerSelectionPlayback::sync(
        const RenderState& s, fonts::Runtime* font, engine::SequencePlayback& playback)
    {
        const bool visible = s.view==display::Screen2D::PlayerSelect ||
            s.view==display::Screen2D::PlayerSelectRules;
        const P target = visible ? s.setup.phase : P::None;
        const bool europe = s.setup.boardEdition==data::BoardEdition::Europe;
        auto next = live_;
        std::vector<sequence::SequenceCommand> commands;
        auto change = [&](Live& object, data::DataId id) -> std::expected<void,std::string>
        {
            if (object.id==id) return {};
            std::shared_ptr<const sequence::SequenceProgram> program;
            if (id)
            {
                auto loaded = data::isRuntimeBitmapDataId(id)
                    ? sequence::SequenceProgram::rawBitmap(id,data::LegacyDataType::Native)
                    : sequence::SequenceProgram::load(playback.resources(),id);
                if (!loaded) return std::unexpected(loaded.error().detail);
                program=*loaded;
            }
            if (object.id) commands.emplace_back(sequence::StopSequenceCommand{object.id,object.priority,false});
            if (id)
            {
                sequence::ClockStartOptions options;
                options.endingAction=object.loop ? 3 : 2;
                commands.emplace_back(sequence::StartSequenceCommand{program,object.priority,options,
                    sequence::moveXYTransform(object.x,object.y)});
            }
            object.id=id;
            return {};
        };
        // Only runtime clock completion may promote an animation to idle.
        for(auto& [key, object]:next)
        {
            if (!visible)
            {
                if (auto r=change(object,0);!r) return r;
                continue;
            }
            if (!object.animating) continue;
            const auto info=playback.runtime().info(object.id,object.priority);
            if (!info || info->sequenceClock<info->endTime) continue;
            if(auto r=change(object,object.leaving ? 0 : object.idle);!r) return r;
            object.animating=false;
        }
        std::erase_if(next,[](const auto& item){return item.second.id==0;});
        const bool phaseChange=phase_!=target;
        if(phaseChange)
        {
            for(auto& [key,object]:next)
            {
                if(object.leaving) continue;
                if(auto r=change(object,object.out);!r) return r;
                object.leaving=true;
                object.animating=object.id!=0;
            }
            std::erase_if(next,[](const auto& item){return item.second.id==0;});
        }

        std::map<int,Spec> desired;
        auto nextHits=ruleHits_;
        auto nextRestore=restoreRect_, nextShort=shortRect_;
        if(target!=P::None && (!phaseChange || next.empty()))
        {
            const bool incomingSettled=!phaseChange && std::none_of(next.begin(),next.end(),
                [](const auto& item){return item.second.animating;});
            for(auto spec:detail::catalog(europe))
            {
                if(spec.phase!=target || !spec.idle || !enabled(spec.object,s)) continue;
                if(spec.object>=O::PICKTOKEN_ROTATING_CANNON && spec.object<=O::PICKTOKEN_ROTATING_SHOE &&
                    !incomingSettled && !next.contains(static_cast<int>(spec.object))) continue;
                if(europe && (spec.object==O::CITY_STATIC || spec.object==O::CITY_BUTTON_LEFT || spec.object==O::CITY_BUTTON_RIGHT)) continue;
                if(!europe && (spec.object==O::COUNTRY_STATIC || spec.object==O::COUNTRY_BUTTON_LEFT || spec.object==O::COUNTRY_BUTTON_RIGHT || spec.object==O::CURRENCY_STATIC || spec.object==O::CURRENCY_BUTTON_LEFT || spec.object==O::CURRENCY_BUTTON_RIGHT)) continue;
                desired.emplace(static_cast<int>(spec.object),spec);
            }
            const bool textNeeded=target==P::HiScore || target==P::EnterName || target==P::SelectPlayer ||
                target==P::RemovePlayer || target==P::SelectCity || target==P::CustomizeRules;
            if(textNeeded && (!font || !font->ready()))
                return std::unexpected("Player selection font runtime unavailable");
            if(textNeeded)
            {
                if(auto r=font->saveSettings(9);!r) return std::unexpected(r.error().detail);
                RestoreFont restore{*font};
                auto setFont=[&](int size,int weight)->std::expected<void,std::string>
                {
                    auto r=font->setSize(size); if(!r) return std::unexpected(r.error().detail);
                    font->setWeight(weight); return {};
                };
                auto draw=[&](data::LegacyBitmapRGBA8& image,const std::string& value,int x,int y,bool center,std::uint32_t color=0xFFFFFFU)->std::expected<void,std::string>
                {
                    if(value.empty()) return {};
                    auto metrics=font->measure(value); if(!metrics) return std::unexpected(metrics.error().detail);
                    if(center){x-=metrics->width/2;y-=metrics->height/2;}
                    auto rendered=font->blitText(image,value,x,y,color);
                    if(!rendered) return std::unexpected(rendered.error().detail);
                    return {};
                };
                auto blank=[](int w,int h){data::LegacyBitmapRGBA8 image{static_cast<std::uint32_t>(w),static_cast<std::uint32_t>(h),{}};image.pixels.assign(static_cast<std::size_t>(w)*h*4,0);return image;};
                auto surface=[&](int key,data::LegacyBitmapRGBA8 image,int x,int y,int priority)->std::expected<void,std::string>
                {
                    auto found=surfaces_.find(key);
                    if(found==surfaces_.end())
                    {
                        auto id=playback.runtimeBitmaps().create(image.width,image.height,true);
                        if(!id) return std::unexpected(id.error());
                        found=surfaces_.emplace(key,*id).first;
                    }
                    const auto previous=playback.runtimeBitmaps().asset(found->second);
                    const bool changed=!previous || previous->image.pixels!=image.pixels;
                    if(changed)
                    {
                        auto r=playback.runtimeBitmaps().update(found->second,std::move(image));
                        if(!r) return r;
                        const auto live=next.find(key);
                        if(!phaseChange && live!=next.end() &&
                            live->second.id==found->second && !live->second.leaving)
                        {
                            auto redraw=playback.forceRedraw(
                                found->second,static_cast<std::uint16_t>(priority));
                            if(!redraw) return redraw;
                        }
                    }
                    desired.emplace(key,Spec{O{},target,found->second,0,0,x,y,static_cast<std::uint16_t>(priority)});
                    return {};
                };
                if(target==P::HiScore)
                {
                    if(auto r=setFont(16,700);!r)return r;
                    auto image=blank(779,279);
                    for(std::size_t i=0;i<std::min<std::size_t>(8,s.highScores.size());++i)
                    {
                        const auto& entry=s.highScores[i];
                        const auto name=utf8<wchar_t>(entry.name);
                        const auto wins=std::to_string(entry.wins);
                        auto worth=money::format(entry.greatestNetWorth,s.monetarySystem,true,s.setup.boardEdition);
                        if(!worth)return std::unexpected(worth.error());
                        const int y=100+static_cast<int>(i)*21;
                        for(int column=0;column<3;++column)
                        {
                            const auto& value=column==0?name:column==1?wins:*worth;
                            auto metrics=font->measure(value);if(!metrics)return std::unexpected(metrics.error().detail);
                            const int x=column==0?125-metrics->width/2:column==1?365-metrics->width/2:660-metrics->width;
                            if(auto r=draw(image,value,x,y,false);!r)return r;
                        }
                    }
                    if(auto r=surface(202,std::move(image),9,170,1500);!r)return r;
                }
                if(target==P::EnterName)
                {
                    auto image=blank(496,54);
                    if(auto r=draw(image,utf8<wchar_t>(s.setup.name),248,27,true);!r)return r;
                    if(auto r=surface(200,std::move(image),153,361,1500);!r)return r;
                }
                if(target==P::RemovePlayer)
                {
                    if(auto r=setFont(16,700);!r)return r;
                    auto image=blank(600,100);
                    for(int i=0;i<2;++i)
                    {
                        auto value=text(playback,i==0?3174:3173);if(!value)return std::unexpected(value.error());
                        auto metrics=font->measure(*value);if(!metrics)return std::unexpected(metrics.error().detail);
                        if(auto r=draw(image,*value,(600-metrics->width)/2,i*60,false);!r)return r;
                    }
                    if(auto r=surface(201,std::move(image),100,281,1500);!r)return r;
                }
                if(target==P::SelectPlayer)
                {
                    for(std::size_t i=0;i<8 && s.setup.playerLogPageStart+i<s.setup.playerLogCount;++i)
                    {
                        auto value=utf8<wchar_t>(s.setup.playerLog[s.setup.playerLogPageStart+i]);
                        if(auto r=setFont(8,700);!r)return r;
                        for(int size=8;size>1;--size)
                        {
                            auto metrics=font->measure(value);if(!metrics)return std::unexpected(metrics.error().detail);
                            if(metrics->width<67)break;
                            if(auto r=setFont(size-1,700);!r)return r;
                        }
                        auto image=blank(67,14);
                        if(auto r=draw(image,value,33,7,true,0);!r)return r;
                        const auto priority=static_cast<std::uint16_t>(1500+i);
                        desired.emplace(210+static_cast<int>(i),Spec{O{},target,0x00030026U,0x00030025U,0x00030027U,CardX[i]-358,CardY[i]-255,priority});
                        if(incomingSettled || next.contains(220+static_cast<int>(i)))
                            if(auto r=surface(220+static_cast<int>(i),std::move(image),CardX[i]+13,CardY[i]+16,priority+1);!r)return r;
                    }
                }
                if(target==P::SelectCity)
                {
                    constexpr std::array<const char*,11> Cities{"Classic","Atlanta","Boston","Chicago","Dallas","Los Angeles","New York","San Francisco","Seattle","Toronto","Washington"};
                    if(auto r=setFont(8,400);!r)return r;
                    for(int field=0;field<(europe?2:1);++field)
                    {
                        const int selected=field==0?s.setup.citySelected:s.setup.currencySelection[std::clamp(s.setup.currencySelectionIndex,0,2)];
                        if(selected<0 || selected>=(europe?(field==0?12:13):11))return std::unexpected("Player selection city/currency out of range");
                        auto value=europe?text(playback,static_cast<std::uint32_t>((field==0?3144:3156)+selected)):std::expected<std::string,std::string>{Cities[selected]};
                        if(!value)return std::unexpected(value.error());
                        const int width=europe?(field==0?113:155):130;
                        if(auto r=setFont(8,400);!r)return r;
                        auto metrics=font->measure(*value);if(!metrics)return std::unexpected(metrics.error().detail);
                        if(field==1 && metrics->width>=width){if(auto r=setFont(7,400);!r)return r;}
                        auto image=blank(width,13);
                        if(auto r=draw(image,*value,width/2,6,true);!r)return r;
                        if(incomingSettled || next.contains(230+field))
                            if(auto r=surface(230+field,std::move(image),europe?(field==0?168:508):335,europe?442:408,1500);!r)return r;
                    }
                }
                if(target==P::CustomizeRules)
                {
                    nextHits.clear();
                    auto image=blank(793,375);
                    int x=7,y=100;
                    const auto& o=s.game.options;
                    const std::array<int,21> selected{o.housesPerHotel,o.maximumHouses,o.maximumHotels,o.freeParkingSeed,o.initialCash,o.passingGoAmount,o.taxRate,o.flatTaxFee,o.luxuryTaxAmount,o.maximumTurnsInJail,o.getOutOfJailFee,o.houseShortageLevel,o.hotelShortageLevel,o.interestRate,o.auctionGoingTimeDelay,o.dealNPropertiesAtStartup,o.evenBuildRule,o.doubleSalaryOnGo,o.freeParkingPot,o.futureRentTradingAllowed&&o.immunitiesTradingAllowed,o.dealFreePropertiesAtStartup};
                    constexpr std::array<int,16> DefaultChoice{1,3,3,2,2,2,2,2,1,2,1,2,2,2,2,0};
                    for(int row=0;row<21;++row)
                    {
                        auto label=text(playback,3119+row);if(!label)return std::unexpected(label.error());
                        if(auto r=setFont(8,400);!r)return r;
                        auto metrics=font->measure(*label);if(!metrics)return std::unexpected(metrics.error().detail);
                        const int advance=(1+metrics->width/211)*22;
                        if(auto r=setFont(8,700);!r)return r;
                        auto wrapped=detail::wrapRuleDescription(*font,*label,211,2);if(!wrapped)return std::unexpected(wrapped.error());
                        for(std::size_t line=0;line<wrapped->size();++line)
                            if(auto r=draw(image,(*wrapped)[line],x+175,y-100+static_cast<int>(line)*18,false);!r)return r;
                        const int count=row==0?2:row<16?4:1;
                        for(int choice=0;choice<count;++choice)
                        {
                            const int bx=x+(row==0?choice+2:row<16?choice:3)*43;
                            nextHits.push_back({{bx,y,bx+43,y+16},static_cast<rules::options::SetupRule>(row),static_cast<std::uint8_t>(choice)});
                            const int key=300+static_cast<int>(nextHits.size());
                            int selectedChoice=row<16?DefaultChoice[row]:0;
                            if(row<16)
                                for(int candidate=0;candidate<count;++candidate)
                                    if(selected[row]==Values[row][candidate])selectedChoice=candidate;
                            const bool on=row<16?selectedChoice==choice:selected[row]!=0;
                            desired.emplace(key,Spec{O{},target,on?0x00030092U:0x00030091U,on?0x00030094U:0x00030093U,0,bx,y,static_cast<std::uint16_t>(1500+(europe?82:78)+nextHits.size()-1)});
                            if(row<16)
                            {
                                std::expected<std::string,std::string> value=std::to_string(Values[row][choice]);
                                if(row==3||row==4||row==5||row==7||row==8||row==10)value=money::format(Values[row][choice],s.monetarySystem,false,s.setup.boardEdition);
                                if(row==15&&choice==3)value=text(playback,3172);
                                if(!value)return std::unexpected(value.error());
                                if(auto r=draw(image,*value,bx-7+21,y-100+8,true);!r)return r;
                            }
                        }
                        y+=advance;
                        if(y>450){x=407;y=100;}
                    }
                    const int bx=540;
                    const int y1=y+(450-y-72)/3,y2=y+2*(450-y-72)/3+36;
                    nextRestore={bx,y1,bx+127,y1+36};nextShort={bx,y2,bx+127,y2+36};
                    desired[static_cast<int>(O::RULES_BUTTON_RESTORESTANDARD)].x=bx;
                    desired[static_cast<int>(O::RULES_BUTTON_RESTORESTANDARD)].y=y1;
                    desired[static_cast<int>(O::RULES_BUTTON_SHORTGAME)].x=bx;
                    desired[static_cast<int>(O::RULES_BUTTON_SHORTGAME)].y=y2;
                    if(!s.setup.serverMode)
                    {
                        auto label=text(playback,3171);if(!label)return std::unexpected(label.error());
                        if(auto r=draw(image,*label,396,360,true);!r)return r;
                    }
                    if(auto r=surface(240,std::move(image),7,100,europe?1658:1654);!r)return r;
                }
            }
            for(const auto& [key,spec]:desired)
            {
                auto found=next.find(key);
                using B=ui::playersetup::Button;
                const bool pressed=s.pressSerial!=pressSerial_ &&
                    ((spec.object==O::CITY_BUTTON_LEFT && s.pressedButton==B::CityLeft) ||
                     (spec.object==O::CITY_BUTTON_RIGHT && s.pressedButton==B::CityRight) ||
                     (spec.object==O::COUNTRY_BUTTON_LEFT && s.pressedButton==B::CountryLeft) ||
                     (spec.object==O::COUNTRY_BUTTON_RIGHT && s.pressedButton==B::CountryRight) ||
                     (spec.object==O::CURRENCY_BUTTON_LEFT && s.pressedButton==B::CurrencyLeft) ||
                     (spec.object==O::CURRENCY_BUTTON_RIGHT && s.pressedButton==B::CurrencyRight));
                if(found!=next.end() && found->second.idle==spec.idle && !found->second.leaving && !pressed)continue;
                Live object=found!=next.end()?found->second:Live{};
                object.priority=spec.priority;object.x=spec.x;object.y=spec.y;
                object.idle=spec.idle;object.out=spec.out;object.leaving=false;
                object.loop=spec.object>=O::PICKTOKEN_ROTATING_CANNON && spec.object<=O::PICKTOKEN_ROTATING_SHOE;
                object.animating=spec.in!=0;
                if(auto r=change(object,spec.in?spec.in:spec.idle);!r)return r;
                next[key]=object;
            }
            for(auto& [key,object]:next)
            {
                if(desired.contains(key)||object.leaving)continue;
                if(auto r=change(object,object.out);!r)return r;
                object.animating=object.id!=0;object.leaving=true;
            }
        }
        std::erase_if(next,[](const auto& item){return item.second.id==0;});
        if(commands.size()>sequence::SequenceCommandQueue::Capacity-playback.commands().pendingCount())
            return std::unexpected("Sequence queue cannot fit player selection transition");
        for(auto& command:commands)
            if(!std::visit([&](auto item){return playback.commands().enqueue(std::move(item));},std::move(command)))
                return std::unexpected("Validated player selection command rejected");
        live_=std::move(next);
        pressSerial_=s.pressSerial;
        if(!phaseChange || !desired.empty() || live_.empty())
        {
            if (phaseChange && target != P::None) startedPhase_ = target;
            phase_ = target;
        }
        if (target == P::None) startedPhase_.reset();
        interactable_ = target != P::None && phase_ == target;
        ready_=phase_==target && std::none_of(live_.begin(),live_.end(),[](const auto& item){return item.second.animating;});
        ruleHits_=std::move(nextHits);restoreRect_=nextRestore;shortRect_=nextShort;
        return {};
    }

    void PlayerSelectionPlayback::reset() noexcept
    {
        live_.clear();surfaces_.clear();textCache_.clear();ruleHits_.clear();
        restoreRect_={};shortRect_={};phase_=P::None;ready_=false;
        pressSerial_=0;interactable_=false;startedPhase_.reset();
    }
}
