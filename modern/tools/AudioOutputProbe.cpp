// Standalone Windows render-endpoint qualification. Never opens a microphone.
// Software loopback does not establish physical speaker audibility.
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <propvarutil.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(std::string(operation) +
        " HRESULT=" + std::to_string(static_cast<unsigned long>(result)));
}
struct ComScope {
    ComScope() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx"); }
    ~ComScope() { CoUninitialize(); }
};
struct TaskFree { void operator()(WAVEFORMATEX* value) const { CoTaskMemFree(value); } };
struct Property {
    PROPVARIANT value{};
    Property() { PropVariantInit(&value); }
    ~Property() { PropVariantClear(&value); }
};
struct Running {
    IAudioClient* client{};
    ~Running() { if (client) client->Stop(); }
};
struct Packet {
    IAudioCaptureClient* client{};
    UINT32 frames{};
    ~Packet() { if (client) client->ReleaseBuffer(frames); }
};
std::string utf8(const wchar_t* value) {
    if (!value) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), count, nullptr, nullptr);
    result.pop_back();
    for (char& c : result) if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    return result;
}
void u32(std::ostream& out, std::uint32_t value) {
    const char bytes[]{static_cast<char>(value), static_cast<char>(value >> 8),
        static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
    out.write(bytes, 4);
}
struct Wave {
    std::ofstream file;
    std::streampos sizePosition{};
    std::uint32_t bytes{};
    Wave(const std::filesystem::path& path, const WAVEFORMATEX& format) : file(path, std::ios::binary) {
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.write("RIFF", 4); u32(file, 0); file.write("WAVEfmt ", 8);
        const auto formatSize = static_cast<std::uint32_t>(sizeof(WAVEFORMATEX) + format.cbSize);
        u32(file, formatSize);
        file.write(reinterpret_cast<const char*>(&format), formatSize);
        if (formatSize & 1U) file.put('\0');
        file.write("data", 4); sizePosition = file.tellp(); u32(file, 0);
    }
    void append(const BYTE* data, std::uint32_t count) {
        if (count > UINT32_MAX - bytes - 128U) throw std::runtime_error("WAV size limit");
        if (data) file.write(reinterpret_cast<const char*>(data), count);
        else {
            const char zeros[4096]{};
            for (std::uint32_t left = count; left;) {
                const auto chunk = std::min<std::uint32_t>(left, sizeof(zeros));
                file.write(zeros, chunk); left -= chunk;
            }
        }
        bytes += count;
    }
    void finish() {
        if (bytes & 1U) file.put('\0');
        const auto end = file.tellp();
        file.seekp(sizePosition); u32(file, bytes);
        file.seekp(4); u32(file, static_cast<std::uint32_t>(end) - 8U);
        file.close();
    }
};
struct Stats {
    std::uint64_t samples{}, frames{}, packets{}, silentPackets{}, discontinuities{}, timestampErrors{}, nonfinite{};
    long double squares{};
    double peak{};
    void sample(double value) {
        if (!std::isfinite(value)) { ++nonfinite; value = 0; }
        ++samples; squares += value * value; peak = std::max(peak, std::abs(value));
    }
};
double readSample(const BYTE* data, unsigned bits, bool floating) {
    if (floating) {
        if (bits == 32) { float value; std::memcpy(&value, data, 4); return value; }
        double value; std::memcpy(&value, data, 8); return value;
    }
    if (bits == 8) return (static_cast<int>(*data) - 128) / 128.0;
    std::int64_t value{};
    for (unsigned i = 0; i < bits / 8; ++i) value |= std::int64_t(data[i]) << (i * 8);
    if (value & (std::int64_t(1) << (bits - 1))) value -= std::int64_t(1) << bits;
    return static_cast<double>(value) / static_cast<double>(std::int64_t(1) << (bits - 1));
}
void report(std::ostream& out, double elapsed, const Stats& stats) {
    const auto rms = stats.samples ? std::sqrt(static_cast<double>(stats.squares / stats.samples)) : 0;
    out << elapsed << '\t' << stats.frames << '\t' << stats.packets << '\t'
        << stats.silentPackets << '\t' << stats.discontinuities << '\t'
        << stats.timestampErrors << '\t' << stats.nonfinite << '\t' << rms << '\t' << stats.peak << '\n';
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: AudioOutputProbe <existing-output-directory> <seconds:1..300>");
        std::size_t consumed{};
        const auto seconds = std::stoul(argv[2], &consumed);
        if (consumed != std::wcslen(argv[2]) || seconds < 1 || seconds > 300)
            throw std::runtime_error("seconds must be 1..300");
        const auto directory = std::filesystem::canonical(argv[1]);
        if (!std::filesystem::is_directory(directory)) throw std::runtime_error("output must be an existing directory");
        for (const auto& part : directory)
            if (_wcsicmp(part.c_str(), L"Source") == 0) throw std::runtime_error("Source is read-only");
        const auto wavePath = directory / "windows-output.wav";
        const auto reportPath = directory / "windows-output.tsv";
        if (std::filesystem::exists(wavePath) || std::filesystem::exists(reportPath))
            throw std::runtime_error("output files already exist; choose a fresh directory");
        ComScope com;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator)), "MMDeviceEnumerator");
        ComPtr<IMMDevice> device;
        check(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device), "default render endpoint");
        ComPtr<IPropertyStore> properties;
        check(device->OpenPropertyStore(STGM_READ, &properties), "endpoint properties");
        Property name;
        check(properties->GetValue(PKEY_Device_FriendlyName, &name.value), "endpoint name");
        ComPtr<IAudioEndpointVolume> volume;
        check(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(volume.GetAddressOf())), "endpoint volume");
        BOOL muted{}; float gain{};
        check(volume->GetMute(&muted), "GetMute");
        check(volume->GetMasterVolumeLevelScalar(&gain), "GetMasterVolumeLevelScalar");
        ComPtr<IAudioClient> client;
        check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(client.GetAddressOf())), "audio client");
        WAVEFORMATEX* raw{}; check(client->GetMixFormat(&raw), "GetMixFormat");
        std::unique_ptr<WAVEFORMATEX, TaskFree> format(raw);
        WORD tag = format->wFormatTag;
        if (tag == WAVE_FORMAT_EXTENSIBLE) {
            if (format->cbSize < 22) throw std::runtime_error("short extensible format");
            const auto& extended = *reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format.get());
            if (IsEqualGUID(extended.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) tag = WAVE_FORMAT_IEEE_FLOAT;
            else if (IsEqualGUID(extended.SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) tag = WAVE_FORMAT_PCM;
        }
        const bool floating = tag == WAVE_FORMAT_IEEE_FLOAT;
        const auto bits = format->wBitsPerSample;
        if ((!floating && tag != WAVE_FORMAT_PCM) ||
            (floating && bits != 32 && bits != 64) ||
            (!floating && bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
            !format->nChannels || format->nBlockAlign != format->nChannels * (bits / 8))
            throw std::runtime_error("unsupported endpoint PCM layout");
        check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
            10000000, 0, format.get(), nullptr), "loopback Initialize");
        ComPtr<IAudioCaptureClient> capture;
        check(client->GetService(IID_PPV_ARGS(&capture)), "capture service");
        std::ofstream log(reportPath); log.exceptions(std::ios::badbit | std::ios::failbit);
        const auto heading = std::string("proof\tsoftware endpoint loopback; physical audibility unverified\nendpoint\t") +
            utf8(name.value.vt == VT_LPWSTR ? name.value.pwszVal : L"unknown");
        log << heading << "\nrate\t" << format->nSamplesPerSec << "\nchannels\t" << format->nChannels
            << "\nbits\t" << bits << "\nformat_tag\t" << tag << "\nmute_start\t" << muted
            << "\nvolume_start\t" << gain << "\nrequested_seconds\t" << seconds
            << "\nelapsed_seconds\tframes\tpackets\tsilent_flag_packets\tdiscontinuities\ttimestamp_errors\tnonfinite\trms\tpeak\n";
        std::cout << heading << "\nRecording " << seconds << " seconds\n";
        Wave wave(wavePath, *format);
        check(client->Start(), "loopback Start"); Running running{client.Get()};
        const auto start = std::chrono::steady_clock::now();
        auto nextReport = start + std::chrono::seconds(1);
        Stats interval, total;
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
            UINT32 available{}; check(capture->GetNextPacketSize(&available), "GetNextPacketSize");
            while (available) {
                BYTE* data{}; UINT32 frames{}; DWORD flags{};
                check(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr), "GetBuffer");
                Packet packet{capture.Get(), frames};
                const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                const auto byteCount = static_cast<std::uint64_t>(frames) * format->nBlockAlign;
                if (byteCount > UINT32_MAX) throw std::runtime_error("packet size overflow");
                wave.append(silent ? nullptr : data, static_cast<std::uint32_t>(byteCount));
                for (Stats* stats : {&interval, &total}) {
                    stats->frames += frames; ++stats->packets;
                    stats->silentPackets += silent;
                    stats->discontinuities += (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
                    stats->timestampErrors += (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0;
                    const auto samples = static_cast<std::uint64_t>(frames) * format->nChannels;
                    for (std::uint64_t i = 0; i < samples; ++i)
                        stats->sample(silent ? 0 : readSample(data + i * (bits / 8), bits, floating));
                }
                // Release this packet before asking for the next one.
                check(capture->ReleaseBuffer(frames), "ReleaseBuffer"); packet.client = nullptr;
                check(capture->GetNextPacketSize(&available), "GetNextPacketSize");
                if (std::chrono::steady_clock::now() - start >= std::chrono::seconds(seconds)) break;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextReport) {
                const double elapsed = std::chrono::duration<double>(now - start).count();
                report(log, elapsed, interval); report(std::cout, elapsed, interval); log.flush();
                interval = {}; nextReport = now + std::chrono::seconds(1);
            }
            Sleep(10);
        }
        check(client->Stop(), "loopback Stop"); running.client = nullptr;
        wave.finish();
        check(volume->GetMute(&muted), "GetMute end");
        check(volume->GetMasterVolumeLevelScalar(&gain), "GetMasterVolumeLevelScalar end");
        log << "total\n"; report(log, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), total);
        log << "mute_end\t" << muted << "\nvolume_end\t" << gain << '\n';
        std::cout << "Complete; " << total.frames << " captured frames; peak " << total.peak << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "AudioOutputProbe: " << error.what() << '\n'; return 1;
    }
}
