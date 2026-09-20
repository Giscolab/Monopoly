#include "PlayerSelectionHistory.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace monopoly::playerselection
{
    namespace
    {
        std::string narrow(std::wstring_view input)
        {
            const std::wstring value(input);
            char* result=SDL_iconv_string("UTF-8","WCHAR_T",reinterpret_cast<const char*>(value.c_str()),(value.size()+1)*sizeof(wchar_t));
            if(!result)return {};
            std::string output(result);SDL_free(result);return output;
        }
        std::wstring widen(std::string_view input)
        {
            const std::string value(input);
            char* result=SDL_iconv_string("WCHAR_T","UTF-8",value.c_str(),value.size()+1);
            if(!result)return {};
            std::wstring output(reinterpret_cast<const wchar_t*>(result));SDL_free(result);return output;
        }
        int number(std::string_view input)
        {
            int value{};
            const auto parsed=std::from_chars(input.data(),input.data()+input.size(),value);
            return parsed.ec==std::errc{}?value:0;
        }
        std::string_view trim(std::string_view value)
        {
            const auto begin=value.find_first_not_of(" \t\r");
            if(begin==std::string_view::npos)return {};
            return value.substr(begin,value.find_last_not_of(" \t\r")-begin+1);
        }
    }

    std::expected<void,std::string> PlayerSelectionHistory::open(std::filesystem::path path)
    {
        if(path.empty())return std::unexpected("Player history path is empty");
        std::array<HistoryEntry,100> entries{};
        std::string preserved;
        std::error_code error;
        const bool exists=std::filesystem::exists(path,error);
        if(error)return std::unexpected(error.message());
        if(exists)
        {
            if(std::filesystem::file_size(path,error)>4*1024*1024 || error)
                return std::unexpected("Player history INI exceeds read limit");
            std::ifstream file(path,std::ios::binary);
            if(!file)return std::unexpected("Cannot read player history INI");
            std::string line;int section=-1;
            while(std::getline(file,line))
            {
                auto value=trim(line);
                if(value.size()>2 && value.front()=='[' && value.back()==']')
                {
                    const auto name=value.substr(1,value.size()-2);
                    section=-1;
                    if(name.starts_with("Player"))
                    {
                        const auto suffix=name.substr(6);
                        int n{};
                        const auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),n);
                        if(parsed.ec==std::errc{} && parsed.ptr==suffix.data()+suffix.size() && n>=1 && n<=100)section=n-1;
                    }
                }
                if(section<0){preserved+=line;preserved+='\n';continue;}
                const auto equals=value.find('=');
                if(equals==std::string_view::npos)continue;
                const auto key=trim(value.substr(0,equals));
                const auto content=trim(value.substr(equals+1));
                auto& entry=entries[static_cast<std::size_t>(section)];
                if(key=="Name")entry.name=widen(content).substr(0,255);
                else if(key=="Wins")entry.wins=number(content);
                else if(key=="GreatestNetWorth")entry.greatestNetWorth=number(content);
            }
            if(file.bad())return std::unexpected("Failed reading player history INI");
        }
        entries_=std::move(entries);preserved_=std::move(preserved);path_=std::move(path);
        winnerProcessed_=false;
        return {};
    }

    std::expected<void,std::string> PlayerSelectionHistory::save(const std::array<HistoryEntry,100>& entries)
    {
        if(!configured())return std::unexpected("Player history is not configured");
        std::error_code error;
        if(!path_.parent_path().empty())std::filesystem::create_directories(path_.parent_path(),error);
        if(error)return std::unexpected(error.message());
        auto temporary=path_;temporary+=".pending";
        {
            std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
            if(!file)return std::unexpected("Cannot write player history temporary INI");
            file<<preserved_;
            for(std::size_t i=0;i<entries.size();++i)
            {
                const auto& entry=entries[i];if(entry.name.empty())continue;
                auto name=narrow(entry.name);
                if(name.find_first_of("\r\n")!=std::string::npos)return std::unexpected("Player history name contains a line break");
                file<<"[Player"<<i+1<<"]\r\nName="<<name<<"\r\nWins="<<entry.wins
                    <<"\r\nGreatestNetWorth="<<entry.greatestNetWorth<<"\r\n";
            }
            file.flush();if(!file)return std::unexpected("Failed writing player history INI");
        }
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            return std::unexpected("Cannot publish player history INI: "+std::to_string(GetLastError()));
#else
        std::filesystem::rename(temporary,path_,error);
        if(error)return std::unexpected(error.message());
#endif
        return {};
    }

    std::expected<void,std::string> PlayerSelectionHistory::gameStarted(std::span<const HistoryPlayer> players)
    {
        auto next=entries_;
        for(const auto& player:players)
        {
            // Exact active UDPSEL_GameHasJustStarted condition (including its
            // remote-AI quirk): skip only an AI whose slot is local.
            if(player.name.empty() || (player.aiLevel!=0 && player.local))continue;
            if(std::any_of(next.begin(),next.end(),[&](const auto& e){return e.name==player.name;}))continue;
            auto free=std::find_if(next.begin(),next.end(),[](const auto& e){return e.name.empty();});
            if(free==next.end())free=next.begin();
            *free={player.name,0,0};
        }
        if(auto r=save(next);!r)return r;
        entries_=std::move(next);winnerProcessed_=false;return {};
    }

    std::expected<void,std::string> PlayerSelectionHistory::gameOver(std::wstring_view name,int worth,int aiLevel)
    {
        if(winnerProcessed_)return {};
        auto next=entries_;
        if(aiLevel==0)
        {
            auto winner=std::find_if(next.begin(),next.end(),[&](const auto& e){return e.name==name;});
            if(winner!=next.end())
            {
                if(winner->wins<std::numeric_limits<int>::max())++winner->wins;
                winner->greatestNetWorth=std::max(winner->greatestNetWorth,worth);
                if(auto r=save(next);!r)return r;
            }
        }
        entries_=std::move(next);winnerProcessed_=true;return {};
    }

    std::vector<HistoryEntry> PlayerSelectionHistory::highScores() const
    {
        std::vector<HistoryEntry> result;
        for(const auto& entry:entries_)if(!entry.name.empty() && entry.wins>0)result.push_back(entry);
        std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b)
            {return a.wins>b.wins || (a.wins==b.wins && a.greatestNetWorth>b.greatestNetWorth);});
        if(result.size()>8)result.resize(8);
        return result;
    }
    std::vector<std::wstring> PlayerSelectionHistory::names() const
    {
        std::vector<std::wstring> result;
        for(const auto& entry:entries_)if(!entry.name.empty())result.push_back(entry.name.substr(0,10));
        return result;
    }
}
