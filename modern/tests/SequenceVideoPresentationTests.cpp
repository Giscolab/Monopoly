#include "SequenceVideoRuntime.hpp"
#include "VideoPresentation.hpp"
#include "VideoDecoderFixture.hpp"
#include "SyntheticSequenceResources.hpp"
#include "World2DRenderer.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace monopoly;
using namespace std::chrono_literals;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class T> T take(std::expected<T,std::string> result) { if (!result) throw std::runtime_error(result.error()); return std::move(*result); }
void checked(std::expected<void,std::string> result) { if(!result) throw std::runtime_error(result.error()); }
template<class Predicate> void until(Predicate ready,const char* description) {
    const auto deadline=std::chrono::steady_clock::now()+12s;
    do { if(ready()) return; std::this_thread::sleep_for(2ms); } while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error(std::string("deadline: ")+description);
}
video::DecoderOptions decoderOptions() {
    video::DecoderOptions options; options.ffmpeg=test::ffmpegExecutable();
    if(const auto* ffprobe=SDL_getenv("MONOPOLY_TEST_FFPROBE");ffprobe && *ffprobe) options.ffprobe=ffprobe;
    return options;
}
bool red(const std::vector<std::uint8_t>& pixels) { return pixels.size()>=4 && pixels[0]>220 && pixels[1]<15 && pixels[2]<15; }
bool blue(const std::vector<std::uint8_t>& pixels) { return pixels.size()>=4 && pixels[2]>220 && pixels[0]<15 && pixels[1]<15; }
void testSourcePresentationGeometryAndAlpha() {
    const auto small=video::binkDoubleSizeBounds(320,240);
    require(small.left==80 && small.top==60 && small.right==720 && small.bottom==540,"Bink320x240 doubles and centers in the source800x600 view");
    const auto full=video::binkDoubleSizeBounds(400,300);
    require(full.left==0 && full.top==0 && full.right==800 && full.bottom==600,"Bink400x300 doubles to the complete view");
    const auto large=video::binkDoubleSizeBounds(1000,700);
    require(large.left==0 && large.top==0 && large.right==800 && large.bottom==600,"oversized Bink destination clamps to the source viewport");
    video::DecodedVideoFrame frame{0,1,2,{255,0,0,255,0,255,0,255}};
    data::SequenceVideoData options;options.enableVideo=true;options.flipVertically=true;options.alphaLevel=255;
    auto image=take(video::prepareVideoFrame(frame,options));
    require(image.pixels==std::vector<std::uint8_t>{0,255,0,0,255,0,0,255},"vertical flip preserves source green transparency key at opaque alpha");
    options.alphaLevel=128;image=take(video::prepareVideoFrame(frame,options));
    require(image.pixels[3]==128 && image.pixels[7]==128,"partial alpha applies once to both green and red pixels");
    options.drawSolid=true;options.alphaLevel=0;image=take(video::prepareVideoFrame(frame,options));
    require(image.pixels[3]==255 && image.pixels[7]==255,"drawSolid bypasses alpha and the green key");
    frame.rgba.pop_back();require(!video::prepareVideoFrame(frame,options),"truncated decoded RGBA is rejected before publication");
}
void testSilentPresentation(const test::VideoDecoderFixture& movie,bool expectedAudio) {
    video::Presentation presentation; checked(presentation.open(movie.file(),false,decoderOptions()));
    std::optional<video::DecodedVideoFrame> current;
    until([&]{ const auto clock=take(presentation.pump(0)); auto frame=take(presentation.frameAt(clock.elapsedMicroseconds)); if(frame) current=std::move(frame); return current && red(current->rgba); },"first actual compressed red frame");
    const auto snapshot=presentation.snapshot();
    require(snapshot.metadata && snapshot.metadata->hasAudio==expectedAudio && current->width==32 && current->height==24 && current->rgba.size()==32*24*4,"actual fixture metadata and complete decoded RGBA extent");
    until([&]{ const auto clock=take(presentation.pump(1250000)); require(!clock.waitingForAudio && clock.consumedAudioBytes==0,"disabled audio cannot stall the movie or consume PCM"); auto frame=take(presentation.frameAt(clock.elapsedMicroseconds)); if(frame) current=std::move(frame); return current && blue(current->rgba); },"distinct blue frame at movie time1.25s");
    checked(presentation.seek(0,1250000)); current.reset();
    until([&]{ const auto clock=take(presentation.pump(1250000)); auto frame=take(presentation.frameAt(clock.elapsedMicroseconds)); if(frame) current=std::move(frame); return current && current->timestampMicroseconds==0 && red(current->rgba); },"seek discards stale blue frames and restarts red");
    until([&]{ const auto clock=take(presentation.pump(4250000)); (void)take(presentation.frameAt(clock.elapsedMicroseconds)); return presentation.videoDrained(); },"silent presentation drains actual decoded EOF");
    presentation.stop(); const auto stopped=presentation.snapshot();
    require(stopped.phase==video::DecoderPhase::Stopped && stopped.queuedVideoFrames==0 && stopped.queuedAudioChunks==0,"stop reaps decoder and empties both queues");
}
void testAudioClockAndAbsentAudio(const test::VideoDecoderFixture& movie,const test::VideoDecoderFixture& silent) {
    video::Presentation presentation;
    checked(presentation.setGain(0.35F));
    checked(presentation.setPitch(96'000U, 48'000U));
    require(!presentation.setGain(std::numeric_limits<float>::infinity()),
        "video presentation rejects non-finite sequence gain");
    checked(presentation.open(silent.file(),true,decoderOptions()));
    until([&]{ const auto clock=take(presentation.pump(0)); const auto frame=take(presentation.frameAt(clock.elapsedMicroseconds)); return frame && red(frame->rgba); },"silent stream first decoded frame anchors its clock");
    const auto noAudio=take(presentation.pump(1250000));
    require(!presentation.snapshot().metadata->hasAudio && noAudio.elapsedMicroseconds==1250000 && noAudio.consumedAudioBytes==0 && noAudio.audioDrained && !noAudio.waitingForAudio,"audio enabled with no audio track uses sequence time immediately");
    presentation.stop(); checked(presentation.open(movie.file(),true,decoderOptions()));
    video::PresentationClock clock; bool sawRed=false,sawBlue=false;
    const auto pump=[&](bool paused=false) {
        clock=take(presentation.pump(0,paused));
        if(auto frame=take(presentation.frameAt(clock.elapsedMicroseconds))) { sawRed=sawRed||red(frame->rgba); sawBlue=sawBlue||blue(frame->rgba); }
    };
    until([&]{pump();return clock.consumedAudioBytes>0;},"SDL dummy device consumes decoded PCM");
    require(clock.elapsedMicroseconds>0,"audio clock advances while sequence time remains exactly zero");
    checked(presentation.setGain(0.20F));
    checked(presentation.setPitch(48'000U, 48'000U));
    pump();
    require(clock.consumedAudioBytes>0,
        "live video audio stream accepts sequence gain and pitch changes without resetting playback");
    pump(true); const auto paused=clock.elapsedMicroseconds;
    std::this_thread::sleep_for(50ms); pump(true);
    require(clock.elapsedMicroseconds==paused,"pause freezes the presented media clock");
    until([&]{pump();return clock.audioDrained && presentation.videoDrained();},"dummy audio clock and video both reach decoded EOF");
    require(sawRed && sawBlue && clock.consumedAudioBytes>=48000U*2U*2U && clock.elapsedMicroseconds>=1900000,"real audio consumption drives both distinct compressed frames through the two-second movie");
    presentation.stop(); require(presentation.snapshot().phase==video::DecoderPhase::Stopped,"stop closes the audio-backed presentation");
}

// Real legacy chunk encoding, in a test-only DAT. The media itself is the
// compressed FFmpeg fixture, not fabricated decoder output or a runtime factory.
void installFiniteVideo(SyntheticSequenceResources& resources,const test::VideoDecoderFixture& movie) {
    resources.service.shutdown();
    std::filesystem::create_directory(resources.directory/"AVI");
    std::filesystem::copy_file(movie.file(),resources.directory/"AVI/movie.avi");
    auto bytes=SyntheticSequenceResources::words({0,0,0x41000078U,17});
    // L_Seqncr.h SequenceVideoChunk: solid, flip, alpha, video, audio, direct, double, S/B/C.
    for(const auto value:{1U,0U,255U,1U,1U,0U,0U,0U,0U,0U}) bytes.push_back(static_cast<std::byte>(value));
    const auto append=[&](std::initializer_list<std::uint32_t> values) {
        const auto more=SyntheticSequenceResources::words(values);bytes.insert(bytes.end(),more.begin(),more.end());
    };
    append({0x81000005U});bytes.push_back(std::byte{2}); // Dimensionality2.
    append({0x8C000005U});bytes.push_back(std::byte{53}); // HIT_BOX_LABEL, source private chunk140.
    append({0x88000014U,0,0,32,24}); // Intrinsic-size destination rectangle.
    append({0x8D000006U});bytes.push_back(std::byte{0x80});bytes.push_back(std::byte{0xBB}); // SET_SOUND_PITCH, 48000 Hz.
    append({0x1800000EU}); // FILE_NAME_5, ten bytes including terminating NUL.
    for(const char value:std::string("movie.avi")) bytes.push_back(static_cast<std::byte>(value));
    bytes.push_back(std::byte{0});
    const auto header=0x06000000U|static_cast<std::uint32_t>(bytes.size());
    for(unsigned i=0;i<4;++i) bytes[i]=static_cast<std::byte>((header>>(8U*i))&255U);
    const std::array items{data::ArchiveBuildItem{data::LegacyDataType::Chunky,std::move(bytes)}};
    require(data::writeLegacyDataArchive(resources.directory/"Dat_Mon/dat_main.dat",items).has_value(),"write finite video CNK fixture");
    const auto paths=data::ResourcePaths::create(std::array{resources.directory});
    require(paths && resources.service.initialize(*paths),"mount finite video CNK fixture");
}
void testFiniteAudioSequenceLifecycle(const test::VideoDecoderFixture& movie) {
    SyntheticSequenceResources resources;installFiniteVideo(resources,movie);
    engine::SequencePlayback playback(resources.service.snapshot());video::SequenceRuntimeBridge bridge;
    bridge.setDecoderOptions(decoderOptions());
    const auto id=data::packDataId(data::LegacyGroupId::Main,0);
    const auto program=sequence::SequenceProgram::load(resources.service.snapshot(),id);
    require(program.has_value(),"load actual finite video CNK through the resource parser");
    require((*program)->descriptions().front().record.header.endTime==120 &&
        (*program)->descriptions().front().record.header.endingAction==1,"fixture carries authored finite duration and Stop action");
    require(std::any_of((*program)->descriptions().front().attributes.values.begin(),
        (*program)->descriptions().front().attributes.values.end(),[](const auto& attribute) {
            const auto* label=std::get_if<data::SequenceLabelAttribute>(&attribute);return label && label->labelNumber==53;
        }),"finite CNK parser reads source HIT_BOX_LABEL private attribute53");
    const auto* video=std::get_if<data::SequenceVideoData>(&(*program)->descriptions().front().record.data);
    require(video && video->drawSolid && !video->flipVertically && video->alphaLevel==255 &&
        video->enableVideo && video->enableAudio && !video->drawDirectlyToScreen && !video->doubleAlternateLines &&
        video->saturation==0 && video->brightness==0 && video->contrast==0,
        "finite CNK decodes exact source playback flags before the real decoder opens");
    checked(playback.start(id,9));checked(playback.update(0));
    const auto roots=playback.runtime().matching(id,9);require(roots.size()==1,"finite video has one live root");
    const auto node=roots.front();std::int32_t tick=600;
    require(playback.runtime().inspect(node)->label==53 &&
        playback.runtime().inspect(node)->priority==9 &&
        playback.runtime().inspect(node)->pitch==48'000U,
        "CNK label, caller priority and legacy video pitch reach the live runtime node");
    std::vector<sequence::SequenceEvent> events;
    const auto update=[&] {
        checked(playback.update(tick));const auto cycle=playback.commands().cycleEvents();
        events.insert(events.end(),cycle.begin(),cycle.end());
    };
    const auto sync=[&] { (void)take(bridge.sync(playback));update(); };
    const auto count=[&](sequence::SequenceEventKind kind) {
        return std::count_if(events.begin(),events.end(),[&](const auto& event) {return event.node==node && event.kind==kind;});
    };
    update();
    require(playback.runtime().inspect(node) && playback.runtime().inspect(node)->clock==0 && count(sequence::SequenceEventKind::ReachedEnd)==0,
        "finite CNK remains at media clock zero while backend initialization is deferred beyond its authored duration");
    (void)take(bridge.sync(playback));tick=1200;update();
    require(playback.runtime().inspect(node) && playback.runtime().inspect(node)->clock==0,
        "first asynchronous probe cannot charge parent elapsed time to the finite media clock");
    bool sawRed=false,sawBlue=false;std::uint64_t consumed{};
    const auto observe=[&] {
        if(bridge.states().empty()) return;
        const auto& state=bridge.states().front();consumed=std::max(consumed,state.consumedAudioBytes);
        if(state.surface) {
            if(const auto image=playback.runtimeBitmaps().asset(*state.surface)) {
                sawRed=sawRed||red(image->image.pixels);sawBlue=sawBlue||blue(image->image.pixels);
            }
        }
    };
    until([&]{sync();observe();return consumed>=6400;},"finite CNK consumes at least two ticks of actual SDL dummy audio");
    require(playback.runtime().inspect(node) && playback.runtime().inspect(node)->clock>0 && playback.runtime().inspect(node)->clock<120,
        "finite CNK media clock advances with consumed PCM before completion");
    tick=12000;sync();observe();
    require(playback.runtime().inspect(node) && playback.runtime().inspect(node)->clock<120 && count(sequence::SequenceEventKind::ReachedEnd)==0,
        "large parent clock jump cannot prematurely complete an audio-backed finite CNK");
    require(playback.runtime().setPaused(node,true).has_value(),"pause the finite video node");sync();
    const auto pausedClock=playback.runtime().inspect(node)->clock;
    std::this_thread::sleep_for(50ms);tick+=600;sync();
    require(playback.runtime().inspect(node)->clock==pausedClock,"paused video holds its sequence media clock despite wall and parent time");
    require(playback.runtime().setPaused(node,false).has_value(),"resume the finite video node");
    until([&]{sync();observe();return !playback.runtime().inspect(node);},"decoded audio EOF naturally stops finite video without a Stop command");
    require(sawRed && sawBlue && consumed>=48000U*2U*2U,"finite CNK reaches both real compressed colours using actual consumed PCM");
    require(count(sequence::SequenceEventKind::ReachedEnd)==1 && count(sequence::SequenceEventKind::Destroyed)==1,
        "natural Stop EOF emits one completion and destroys its actual sequence node");
    const auto ended=std::find_if(events.begin(),events.end(),[&](const auto& event) {return event.node==node && event.kind==sequence::SequenceEventKind::ReachedEnd;});
    require(ended!=events.end() && ended->label==53 && ended->endingAction==1 && ended->clock==120 && ended->dataId==id,
        "EOF completion preserves CNK identity, label and authored duration for consumers");
    require(playback.runtimeBitmaps().size()==0 && playback.world2D().size()==0,
        "natural Stop EOF removes the bitmap and rendered surface in the completion cycle");
    sync();
    require(bridge.states().empty() && playback.runtime().liveNodeCount()==0 && count(sequence::SequenceEventKind::ReachedEnd)==1,
        "next bridge cycle reaps decoder session without a ghost root or duplicate completion");
}
std::vector<std::uint8_t> capture(SDL_GPUDevice* device,engine::World2DRenderer& renderer,const engine::SequenceWorld2DSlot& slot) {
    constexpr unsigned width=800,height=600;
    SDL_GPUTextureCreateInfo ti{}; ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;ti.width=width;ti.height=height;ti.layer_count_or_depth=1;ti.num_levels=1;
    auto* target=SDL_CreateGPUTexture(device,&ti);require(target,"GPU movie readback target");
    SDL_GPUTransferBufferCreateInfo bi{};bi.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;bi.size=width*height*4;
    auto* transfer=SDL_CreateGPUTransferBuffer(device,&bi);auto* command=SDL_AcquireGPUCommandBuffer(device);require(transfer && command,"GPU movie readback command");
    SDL_GPUColorTargetInfo color{};color.texture=target;color.clear_color={0,0,0,1};color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    auto* pass=SDL_BeginGPURenderPass(command,&color,1,nullptr);require(pass,"GPU movie clear pass");SDL_EndGPURenderPass(pass);
    const auto drawn=take(renderer.render(command,target,width,height,slot));require(drawn==slot.size(),"GPU draws every published movie/overlay object");
    auto* copy=SDL_BeginGPUCopyPass(command);require(copy,"GPU movie download pass");SDL_GPUTextureRegion source{};source.texture=target;source.w=width;source.h=height;source.d=1;SDL_GPUTextureTransferInfo destination{transfer,0,width,height};SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
    auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);require(fence && SDL_WaitForGPUFences(device,true,&fence,1),"GPU movie frame fence");
    auto* mapped=SDL_MapGPUTransferBuffer(device,transfer,false);require(mapped,"GPU movie pixels mapped");std::vector<std::uint8_t> result(bi.size);std::memcpy(result.data(),mapped,result.size());SDL_UnmapGPUTransferBuffer(device,transfer);SDL_ReleaseGPUFence(device,fence);SDL_ReleaseGPUTransferBuffer(device,transfer);SDL_ReleaseGPUTexture(device,target);return result;
}
std::array<std::uint8_t,4> pixel(const std::vector<std::uint8_t>& image,unsigned x,unsigned y) {const auto i=(y*800+x)*4;return{image.at(i),image.at(i+1),image.at(i+2),image.at(i+3)};}

void testBridgeAndGpu(const test::VideoDecoderFixture& movie) {
    SyntheticSequenceResources resources;
    std::filesystem::create_directory(resources.directory/"AVI");
    std::filesystem::copy_file(movie.file(),resources.directory/"AVI/movie.avi");
    engine::SequencePlayback playback(resources.service.snapshot()); video::SequenceRuntimeBridge bridge;
    bridge.setDecoderOptions(decoderOptions());
    data::SequenceVideoData options; options.enableVideo=true; options.enableAudio=false; options.alphaLevel=128;
    options.doubleAlternateLines=true; // Source disables doubling outside direct-screen playback.
    data::Sequence2DBoundingBoxAttribute bounds{{},10,20,74,68};
    const auto movieId=data::packDataId(data::LegacyGroupId::Main,0x7000);
    const auto program=sequence::SequenceProgram::runtimeVideo(movieId,"movie.avi",options,bounds);
    require(program.has_value(),"runtime video program uses a real external compressed file");
    sequence::ClockStartOptions staying;staying.endingAction=2;
    require(playback.commands().enqueue(sequence::StartSequenceCommand{*program,7,staying,sequence::moveXYTransform(100,50),52}).has_value(),"start real StayAtEnd movie sequence");
    checked(playback.update(0)); std::int32_t tick=0;
    std::vector<video::SequenceJumpNotice> observedJumps;
    std::vector<sequence::SequenceEvent> observedEvents;
    const auto sync=[&] {
        const auto jumps=take(bridge.sync(playback));
        observedJumps.insert(observedJumps.end(),jumps.begin(),jumps.end());
        checked(playback.update(tick));
        const auto events=playback.commands().cycleEvents();
        observedEvents.insert(observedEvents.end(),events.begin(),events.end());
    };
    const auto image=[&]() -> std::shared_ptr<const data::BitmapRuntimeAsset> {
        if(bridge.states().empty() || !bridge.states().front().surface) return {};
        return playback.runtimeBitmaps().asset(*bridge.states().front().surface);
    };
    until([&]{sync();return image() && red(image()->image.pixels) && playback.world2D().size()==1;},"bridge publishes decoded red pixels");
    const auto state=bridge.states().front(); const auto surface=*state.surface; const auto node=state.node;
    require(state.initialized && state.boundingBox && state.boundingBox->left==10 && state.boundingBox->right==74 && !state.options.doubleAlternateLines && state.consumedAudioBytes==0,"effective bounds and disabled alternate-line flag survive async initialization");
    const auto roots=playback.runtime().matching(surface,7); require(roots.size()==1,"movie surface keeps sequence priority7");
    const auto* object=playback.world2D().find(roots.front());
    require(object && object->worldTransform.values[0]==2 && object->worldTransform.values[4]==2 && object->worldTransform.values[6]==110 && object->worldTransform.values[7]==70 && object->asset->image.pixels[3]==128,"bounding box scales decoded image and composes with sequence XY while preserving alpha");
    auto* device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL,false,"direct3d12");
    require(device,"real D3D12 video integration is required; no skip");
    try {
        auto renderer=take(engine::World2DRenderer::load(device,MONOPOLY_SHADER_DIR,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM));
        auto pixels=capture(device,*renderer,playback.world2D()); const auto inside=pixel(pixels,115,75);
        require(inside[0]>=110 && inside[0]<=140 && inside[1]<8 && inside[2]<8 && inside[3]==255 && pixel(pixels,109,75)==std::array<std::uint8_t,4>{0,0,0,255} && pixel(pixels,174,75)==std::array<std::uint8_t,4>{0,0,0,255},"real decoded movie raster obeys scaled bounds and half-alpha blending in GPU readback");
        const auto overlay=take(playback.runtimeBitmaps().create(1,1,false));
        checked(playback.runtimeBitmaps().update(overlay,{1,1,{0,255,0,255}}));
        auto transform=sequence::identity2D(); transform.values[0]=64;transform.values[4]=48;transform.values[6]=110;transform.values[7]=70;
        checked(playback.startMoved(overlay,8,transform));checked(playback.update(tick));
        pixels=capture(device,*renderer,playback.world2D());require(pixel(pixels,115,75)==std::array<std::uint8_t,4>{0,255,0,255},"higher-priority existing bitmap overlays the actual decoded movie");
        checked(playback.stop(overlay,8));checked(playback.update(tick));require(playback.runtimeBitmaps().remove(overlay),"remove owned overlay fixture");
        checked(playback.move(movieId,7,sequence::moveXYTransform(120,50)));checked(playback.update(tick));sync();
        pixels=capture(device,*renderer,playback.world2D());require(pixel(pixels,115,75)==std::array<std::uint8_t,4>{0,0,0,255} && pixel(pixels,135,75)[0]>110 && bridge.states().front().surface==surface,"moving the sequence repositions the same movie surface in actual GPU output");
        tick=75;checked(playback.update(tick));
        until([&]{sync();return image() && blue(image()->image.pixels);},"sequence clock advances actual compressed movie to blue");
        pixels=capture(device,*renderer,playback.world2D());require(pixel(pixels,135,75)[2]>110 && pixel(pixels,135,75)[0]<8,"updated decoded frame replaces the cached GPU texture pixels");
        checked(bridge.cutToFrame(node,0));
        until([&]{sync();return image() && red(image()->image.pixels) && bridge.states().front().status.currentFrame==0;},"bridge seek replaces stale blue with fresh red pixels");
        observedJumps.clear();
        checked(bridge.setAlternative(node,2,12));
        checked(bridge.chooseAlternative(node,2,true));
        tick=93;checked(playback.update(tick)); // 300ms after the preceding seek.
        until([&]{sync();return image() && blue(image()->image.pixels) && bridge.states().front().status.currentFrame==12;},"armed alternative decodes the blue frame at destination1.2s");
        require(std::any_of(observedJumps.begin(),observedJumps.end(),[&](const auto& notice){
            return notice.node==node && notice.event.decisionFrame==2 && notice.event.jumpToFrame==12 && notice.event.alternativeTaken;
        }),"real bridge jump emits the exact taken-alternative notice");
        pixels=capture(device,*renderer,playback.world2D());
        require(pixel(pixels,135,75)[2]>110 && pixel(pixels,135,75)[0]<8,"alternative destination frame reaches actual GPU pixels");
        checked(bridge.forgetAlternatives(node));checked(bridge.cutToFrame(node,0));
        until([&]{sync();return image() && red(image()->image.pixels) && bridge.states().front().status.currentFrame==0;},"forget alternatives then cut restores actual first frame");
        tick=255;checked(playback.update(tick));
        until([&]{sync();return bridge.states().front().status.ended;},"real decoder EOF marks nonlooping sequence ended");
        require(image() && blue(image()->image.pixels) && playback.runtime().inspect(node),"StayAtEnd keeps the node and final decoded frame");
        const auto endedCount=[&](sequence::SequenceNodeId id) {
            return std::count_if(observedEvents.begin(),observedEvents.end(),[&](const auto& event) {
                return event.node==id && event.kind==sequence::SequenceEventKind::ReachedEnd;
            });
        };
        require(endedCount(node)==1 && std::any_of(observedEvents.begin(),observedEvents.end(),[&](const auto& event) {
            return event.node==node && event.kind==sequence::SequenceEventKind::ReachedEnd && event.label==52 && event.endingAction==2;
        }),"StayAtEnd emits its labeled completion once through the normal sequence cycle");
        tick+=60;sync();sync();
        require(endedCount(node)==1 && playback.runtime().inspect(node) && image() && blue(image()->image.pixels),"subsequent updates preserve held movie without duplicate completion");
        checked(playback.stop(movieId,7));checked(playback.update(tick));sync();
        require(bridge.states().empty() && playback.runtimeBitmaps().size()==0 && playback.world2D().size()==0,"stopping video sequence releases decoder surface and bitmap root");
        pixels=capture(device,*renderer,playback.world2D());require(renderer->textureCount()==0 && pixel(pixels,135,75)==std::array<std::uint8_t,4>{0,0,0,255},"stopped video also releases cached GPU texture and framebuffer pixels");
        sequence::ClockStartOptions looping;looping.endingAction=3;
        require(playback.commands().enqueue(sequence::StartSequenceCommand{*program,7,looping,sequence::moveXYTransform(100,50)}).has_value(),"start real looping video sequence");checked(playback.update(tick));
        until([&]{sync();return image() && red(image()->image.pixels);},"loop starts with first decoded frame");
        const auto loopNode=bridge.states().front().node;
        tick+=180;checked(playback.update(tick));
        until([&]{sync();return endedCount(loopNode)==1 && bridge.states().front().initialized && bridge.states().front().status.currentFrame==0 && image() && red(image()->image.pixels);},"decoded EOF loop seeks and presents first frame again");
        require(!bridge.states().front().status.ended && playback.runtime().inspect(loopNode) && endedCount(loopNode)==1,"loop EOF emits one cycle completion and keeps the same live node");
        checked(bridge.reset(playback));checked(playback.update(tick));
        require(bridge.states().empty() && playback.runtimeBitmaps().size()==0 && playback.world2D().size()==0,"explicit bridge reset clears active decoder and presentation resources");
        renderer.reset(); SDL_DestroyGPUDevice(device);device=nullptr;
    } catch(...) { if(device) SDL_DestroyGPUDevice(device);throw; }
}
}
int main() {
    try {
        require(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"dummy",SDL_HINT_OVERRIDE),"select SDL dummy audio before initialization");
        require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO),"initialize SDL video and dummy audio");
        require(std::string(SDL_GetCurrentAudioDriver())=="dummy","physical audio devices are never selected by this fixture");
        testSourcePresentationGeometryAndAlpha();std::cout<<"[PASS] source destination geometry, flip and alpha\n";
        test::VideoDecoderFixture audio(true),silent(false);
        testSilentPresentation(audio,true);testSilentPresentation(silent,false);std::cout<<"[PASS] actual compressed frames, no-audio paths, seek and EOF\n";
        testAudioClockAndAbsentAudio(audio,silent);std::cout<<"[PASS] SDL dummy consumed PCM clock, pause and EOF\n";
        testFiniteAudioSequenceLifecycle(audio);std::cout<<"[PASS] finite CNK probe/audio clock and natural Stop EOF lifecycle\n";
        testBridgeAndGpu(silent);std::cout<<"[PASS] sequence bridge, transformed alpha GPU pixels, priority, seek, loop and cleanup\n";
        SDL_Quit();return 0;
    } catch(const std::exception& error) {std::cerr<<"[FAIL] "<<error.what()<<" SDL: "<<SDL_GetError()<<'\n';SDL_Quit();return 1;}
}