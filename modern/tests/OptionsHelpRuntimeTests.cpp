#include "OptionsHelpRuntime.hpp"
#include "SyntheticSequenceResources.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
using namespace monopoly;
namespace {
void require(bool okay, const char* reason) { if (!okay) throw std::runtime_error(reason); }
constexpr std::string_view StyleFixture="<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>Windows Help</title></head><body><h1>Windows Help</h1><nav><ul><li><a href=\"#topic-0\">Original topic</a></li></ul></nav><section id=\"topic-0\"><p>Exporter test sentinel document: Montr\xC3\xA9" "al \xE2\x82\xAC.</p><img src=\"data:image/png;base64,AAAA\"></section></body></html>";
void testStyle() {
    const auto styled=optionsui::styleFullHelpHtml(StyleFixture,32768);
    require(styled.has_value(),"qualified UTF8 exporter document receives theme");
    const auto position=StyleFixture.find("</head>");
    require(styled->starts_with(StyleFixture.substr(0,position)) && styled->ends_with(StyleFixture.substr(position)),
        "every original content/link/title/image byte is preserved before and after one head insertion");
    require(styled->find("monopoly-modern-help")!=std::string::npos && styled->find("@media(max-width:640px)")!=std::string::npos,
        "self-contained responsive theme present");
    require(!optionsui::styleFullHelpHtml(*styled,32768),"theme insertion is idempotent");
    require(!optionsui::styleFullHelpHtml(StyleFixture,styled->size()-1) &&
        optionsui::styleFullHelpHtml(StyleFixture,styled->size()).has_value(),"size bound includes every appended theme byte");
    require(!optionsui::styleFullHelpHtml("<html><body>Unknown exporter</body></html>",32768),"unknown exporter markup stays original");
    for(const auto invalid:{std::string{"\xC0\xAF"},std::string{"\xED\xA0\x80"},std::string{"\xF4\x90\x80\x80"},std::string{"\xE2\x82"},std::string(1,'\0')}) {
        auto malformed=std::string(StyleFixture);malformed.insert(position,invalid);
        require(!optionsui::styleFullHelpHtml(malformed,32768),"invalid UTF8/embedded NUL stays original");
    }
    auto missing=std::string(StyleFixture);missing.replace(missing.find("<nav>"),5,"<div>");
    require(!optionsui::styleFullHelpHtml(missing,32768),"unqualified navigation structure stays original");
}
struct Block {
    std::atomic<bool> entered{false}, release{false}, returned{false};
    std::filesystem::path document;
};
std::expected<bool,std::string> finish() {
    for (int i=0;i<1000;++i) {
        auto result=optionsui::pollFullHelp();
        if (!result || *result) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return std::unexpected("test completion deadline");
}
void test(const std::string& executable) {
    SyntheticSequenceResources resources;
    { std::ofstream f(resources.directory/"mono01.hlp",std::ios::binary); const char signature[]={0x3f,0x5f,0x03,0};f.write(signature,4); }
    const auto snapshot=resources.service.snapshot();
    optionsui::FullHelpOptions options; options.exporterExecutable=executable; options.timeoutMilliseconds=2000;
    auto block=std::make_shared<Block>();
    options.openDocument=[block](const std::filesystem::path& document)->std::expected<void,std::string> {
        block->document=document; block->entered.store(true);
        while (!block->release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        block->returned.store(true); return {};
    };
    try {
        require(optionsui::openFullHelp(*snapshot,options).has_value(),"start actual export process");
        for(int i=0;i<1000 && !block->entered.load();++i) { require(optionsui::pollFullHelp().has_value(),"export pending");std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        require(block->entered.load(),"validated HTML reaches opener");
        require(std::filesystem::is_regular_file(block->document),"opener receives retained actual document");
        {std::ifstream f(block->document,std::ios::binary);std::string html((std::istreambuf_iterator<char>(f)),{});
            require(html.find("monopoly-modern-help")!=std::string::npos && html.ends_with(StyleFixture.substr(StyleFixture.find("</head>"))),
                "atomic completed export is themed before browser receives it and retains original content");}
        for(int i=0;i<30;++i) {
            const auto start=std::chrono::steady_clock::now();const auto result=optionsui::pollFullHelp();
            require(result && !*result,"blocked browser remains pending");
            require(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100),"blocked browser does not block application polling");
        }
        optionsui::cancelFullHelp();
        require(!optionsui::fullHelpPending(),"cancellation releases UI job");
        require(!optionsui::openFullHelp(*snapshot,options),"cancelled outstanding browser prevents another worker");
        require(std::filesystem::is_regular_file(block->document),"cancel cannot remove document still consumed by shell");
        block->release.store(true);
        while(!block->returned.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // The worker stores completed immediately after callback return.
        for(int i=0;i<100 && !optionsui::openFullHelp(*snapshot,options);++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(optionsui::fullHelpPending(),"completed old worker permits a fresh job");
        const auto success=finish();require(success && *success,"successful dispatch acknowledged once");
        require(optionsui::pollFullHelp()==std::expected<bool,std::string>{false},"successful completion consumed once");
        options.openDocument=[](const std::filesystem::path&)->std::expected<void,std::string>{return std::unexpected("shell sentinel error");};
        require(optionsui::openFullHelp(*snapshot,options).has_value(),"start error job");
        const auto failure=finish();require(!failure && failure.error()=="shell sentinel error","browser failure reaches application");
        require(optionsui::pollFullHelp()==std::expected<bool,std::string>{false},"failure consumed once");
        block=std::make_shared<Block>();options.timeoutMilliseconds=1000;
        options.openDocument=[block](const std::filesystem::path& document)->std::expected<void,std::string>{block->document=document;block->entered.store(true);while(!block->release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));block->returned.store(true);return {};};
        require(optionsui::openFullHelp(*snapshot,options).has_value(),"start timeout job");
        const auto timeout=finish();require(!timeout && timeout.error().find("browser dispatch timed out")!=std::string::npos,"browser timeout bounded separately from export");
        require(!optionsui::openFullHelp(*snapshot,options),"timed-out outstanding browser prevents accumulating threads");
        require(std::filesystem::is_regular_file(block->document),"timeout preserves document for OS request");
        block->release.store(true);
        while(!block->returned.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } catch(...) {block->release.store(true);optionsui::cancelFullHelp();throw;}
}
}
int main(int argc,char** argv) {
    if(argc==4 && std::string_view(argv[1])=="--style-document") {
        std::error_code error;const auto size=std::filesystem::file_size(argv[2],error);
        if(error || size>32*1024*1024)return 1;
        std::ifstream input(argv[2],std::ios::binary);std::string html(static_cast<std::size_t>(size),'\0');
        input.read(html.data(),static_cast<std::streamsize>(html.size()));if(!input)return 1;
        const auto styled=optionsui::styleFullHelpHtml(html,32*1024*1024);if(!styled)return 1;
        std::ofstream output(argv[3],std::ios::binary);output.write(styled->data(),static_cast<std::streamsize>(styled->size()));return output?0:1;
    }
    // This executable is also the deterministic fake exporter, never a fake HLP parser.
    if(argc>=4 && std::string_view(argv[2])=="--html") {std::ofstream out(argv[3],std::ios::binary);out<<StyleFixture;return out?0:1;}
#ifndef _WIN32
    std::cout<<"Windows browser isolation tests skipped\n";return 0;
#else
    try {testStyle();require(SDL_Init(0),"SDL initializes");test(std::filesystem::absolute(argv[0]).string());SDL_Quit();std::cout<<"[PASS] Full Help exact content theme and browser isolation, cancel, timeout and error contracts\n";return 0;}
    catch(const std::exception& error){std::cerr<<"[FAIL] "<<error.what()<<'\n';SDL_Quit();return 1;}
#endif
}
