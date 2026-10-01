#include "ModernMenuSkin.hpp"
#include "FontRuntime.hpp"
#include "ResourceRuntime.hpp"
#include "SequenceRuntime.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    using Clock = std::chrono::steady_clock;
    double milliseconds(Clock::time_point start)
    { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
    std::uint64_t hashPixels(const data::LegacyBitmapRGBA8& image)
    {
        std::uint64_t hash=14695981039346656037ULL;
        for(const auto value:image.pixels) { hash^=value;hash*=1099511628211ULL; }
        hash^=image.width;hash*=1099511628211ULL;hash^=image.height;
        return hash;
    }
    struct Fixture { const char* name; data::DataId root, contents; bool principal{true}; };
    // Exact measured owners/leaves from ModernMenuSkin qualification. Load the
    // real DAT pixels, including alpha; do not synthesize opaque substitutes.
    constexpr std::array fixtures{
        Fixture{"StatsBackground800x225",0x50089,0x50914},
        Fixture{"StatsBackground400x225",0x50089,0x50913,false},
        Fixture{"StatsBackground399x3",0x50089,0x50916,false},
        Fixture{"StatsPanel198x222",0x20349,0x20349},
        Fixture{"StatsPanel130x226",0x2034f,0x2034f},
        Fixture{"CalculatorPanel",0x2006a,0x20324},
        Fixture{"CalculatorDescription",0x20091,0x2035a},
        Fixture{"StatsBar",0x5029b,0x50912},
        Fixture{"StatsHeading",0x5029c,0x50911},
        Fixture{"StatsTabReturnFrame",0x50178,0x511ee},
        // Independently decoded production CNKs: roots run1..9,0; the
        // bitmap bank runs0..9. These explicit pairs catch a rotated guard.
        Fixture{"Digit1Idle",0x2007d,0x20334}, Fixture{"Digit2Idle",0x2007e,0x20335},
        Fixture{"Digit3Idle",0x2007f,0x20336}, Fixture{"Digit4Idle",0x20080,0x20337},
        Fixture{"Digit5Idle",0x20081,0x20338}, Fixture{"Digit6Idle",0x20082,0x20339},
        Fixture{"Digit7Idle",0x20083,0x2033a}, Fixture{"Digit8Idle",0x20084,0x2033b},
        Fixture{"Digit9Idle",0x20085,0x2033c}, Fixture{"Digit0Idle",0x20086,0x20333},
        Fixture{"Digit1Pressed",0x20087,0x2033e}, Fixture{"Digit2Pressed",0x20088,0x2033f},
        Fixture{"Digit3Pressed",0x20089,0x20340}, Fixture{"Digit4Pressed",0x2008a,0x20341},
        Fixture{"Digit5Pressed",0x2008b,0x20342}, Fixture{"Digit6Pressed",0x2008c,0x20343},
        Fixture{"Digit7Pressed",0x2008d,0x20344}, Fixture{"Digit8Pressed",0x2008e,0x20345},
        Fixture{"Digit9Pressed",0x2008f,0x20346}, Fixture{"Digit0Pressed",0x20090,0x2033d},
        Fixture{"Clear",0x501fe,0x50f14},
        Fixture{"BankHousesIdle",0x500fe,0x50c94}, Fixture{"BankHousesSelectedFinal",0x500ff,0x50ca0},
        Fixture{"BankPropertiesIdle",0x50104,0x50cae}, Fixture{"BankPropertiesSelectedFinal",0x50105,0x50cba},
        Fixture{"BankLiabilitiesIdle",0x50101,0x50ca1}, Fixture{"BankLiabilitiesSelectedFinal",0x50102,0x50cad},
        Fixture{"BankTurnsIdle",0x500fb,0x50c87}, Fixture{"BankTurnsSelectedFinal",0x500fc,0x50c93},
        Fixture{"DeedsPriceIdle",0x501a1,0x50e9b}, Fixture{"DeedsPriceSelectedFinal",0x501a2,0x50ea6},
        Fixture{"DeedsOwnerIdle",0x5019b,0x50e5e}, Fixture{"DeedsOwnerSelectedFinal",0x5019c,0x50e69},
        Fixture{"DeedsRentIdle",0x50179,0x507fc}, Fixture{"DeedsRentSelectedFinal",0x5017a,0x50807},
        Fixture{"DeedsEarningsIdle",0x50182,0x50917}, Fixture{"DeedsEarningsSelectedFinal",0x50183,0x50922}};
}

int main(int argc,char** argv)
{
    try
    {
        if(argc<2 || argc>3) throw std::runtime_error("Usage: BenchmarkModernMenuSkin <retail-runtime-root> [Arial.ttf]");
        const std::array roots{std::filesystem::absolute(argv[1])};
        const auto paths=data::ResourcePaths::create(roots);
        if(!paths) throw std::runtime_error(paths.error().detail);
        data::ResourceRuntime resources;
        if(const auto initialized=resources.initialize(*paths);!initialized)
            throw std::runtime_error(initialized.error().detail);
        const auto snapshot=resources.snapshot();
        fonts::Runtime font;
        std::vector<std::filesystem::path> fontRoots{roots.front()};
        if(const char* windows=std::getenv("WINDIR")) fontRoots.emplace_back(std::filesystem::path(windows)/"Fonts");
        std::filesystem::path fontPath;
        if(argc==3) fontPath=std::filesystem::absolute(argv[2]);
        else
        {
            const auto resolved=fonts::resolveRetailArial(fontRoots);
            if(!resolved) throw std::runtime_error(resolved.error().detail);
            fontPath=*resolved;
        }
        if(const auto selected=font.setFont(fontPath,"Arial");!selected) throw std::runtime_error(selected.error().detail);
        if(const auto sized=font.setSize(12);!sized) throw std::runtime_error(sized.error().detail);
        font.setWeight(700);
        std::cout<<std::setprecision(9)<<"scope=actual ModernMenuSkin CPU substitution; no game FPS or GPU upload\n"
            <<"font="<<fontPath.string()<<" caption_size=54 weight=700 antialiased=true repeats=20\n"
#ifdef NDEBUG
            <<"configuration=release\n";
#else
            <<"configuration=debug\n";
#endif
        data::BitmapRuntimeCache bitmaps;
        for(const auto& fixture:fixtures)
        {
            const auto program=sequence::SequenceProgram::load(snapshot,fixture.root);
            if(!program)throw std::runtime_error(program.error().detail);
            if(!std::ranges::any_of((*program)->descriptions(),[&](const auto& description){
                return description.contentsDataId==fixture.contents;
            })) throw std::runtime_error(std::string(fixture.name)+" contents is not owned by the actual decoded CNK");
            const auto loadStart=Clock::now();
            const auto metadata=snapshot->data().metadata(fixture.contents);
            if(!metadata) throw std::runtime_error(metadata.error().detail);
            const auto bytes=snapshot->data().load(fixture.contents);
            if(!bytes) throw std::runtime_error(bytes.error().detail);
            const auto decoded=bitmaps.resolve(fixture.contents,metadata->type,*bytes);
            if(!decoded) throw std::runtime_error(decoded.error().detail);
            const auto original=*decoded;
            const double loadMs=milliseconds(loadStart);
            std::array<double,20> cold{},text{},hot{};
            std::uint64_t expectedHash{};
            unsigned textCalls{};
            for(unsigned repeat=0;repeat<20;++repeat)
            {
                double textMs{};
                menu::ModernMenuSkin skin(data::BoardEdition::Usa,data::LanguageId::EnglishUs,
                    [&](std::string_view caption)->std::expected<data::LegacyBitmapRGBA8,std::string> {
                        const auto start=Clock::now();
                        const auto rasterize=[&]()->std::expected<data::LegacyBitmapRGBA8,std::string> {
                        struct Restore {
                            fonts::Runtime& font;fonts::Settings old;
                            ~Restore(){(void)font.setSize(old.size);font.setWeight(old.weight);font.setItalic(old.italic);
                                font.setUnderline(old.underline);font.setStrikeOut(old.strikeOut);}
                        } restore{font,font.settings()};
                        if(const auto sized=font.setSize(54);!sized)return std::unexpected(sized.error().detail);
                        font.setWeight(700);font.setItalic(false);font.setUnderline(false);font.setStrikeOut(false);
                        auto result=font.render(caption,0xFFFFFF,true);
                        if(!result)return std::unexpected(result.error().detail);
                        return std::move(*result);
                        };
                        auto result=rasterize();
                        ++textCalls;textMs+=milliseconds(start);
                        return result;
                    });
                auto start=Clock::now();
                const auto output=skin.substitute(fixture.root,original,fixture.principal);
                cold[repeat]=milliseconds(start);text[repeat]=textMs;
                if(output==original)throw std::runtime_error(std::string(fixture.name)+" unexpectedly retained fallback");
                const auto hash=hashPixels(output->image);
                if(repeat==0)expectedHash=hash;
                else if(hash!=expectedHash)throw std::runtime_error("cold fixture output is nondeterministic");
                start=Clock::now();
                const auto cached=skin.substitute(fixture.root,original,fixture.principal);
                hot[repeat]=milliseconds(start);
                if(cached!=output)throw std::runtime_error("hot substitution missed immutable derivative cache");
            }
            const auto average=[](const auto& values){double sum{};for(const auto value:values)sum+=value;return sum/values.size();};
            auto sorted=cold;std::sort(sorted.begin(),sorted.end());
            std::cout<<fixture.name<<" root="<<fixture.root<<" contents="<<fixture.contents
                <<" native="<<original->image.width<<'x'<<original->image.height<<" load_decode_ms="<<loadMs
                <<" cold_mean_ms="<<average(cold)<<" cold_median_ms="<<(sorted[9]+sorted[10])/2
                <<" text_mean_ms="<<average(text)<<" non_text_mean_ms="<<average(cold)-average(text)
                <<" cache_hit_mean_ms="<<average(hot)<<" text_calls="<<textCalls
                <<" fnv1a64="<<expectedHash<<'\n';
        }
        return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
