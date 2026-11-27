// The media worker: reads and decodes media on a thread of its own (see worker.h).
#include "worker.h"

#include <algorithm>

namespace rfa_audio {

bool StreamFeed::Take(std::vector<int16_t>& piece)
{
    std::lock_guard<std::mutex> hold(lock);
    if (ready.empty()) return false;
    piece = std::move(ready.front());
    ready.pop_front();
    return true;
}

bool StreamFeed::Finished()
{
    std::lock_guard<std::mutex> hold(lock);
    return ended && ready.empty();
}

bool StreamFeed::Failed()
{
    std::lock_guard<std::mutex> hold(lock);
    return failed;
}

bool StreamFeed::WantsMore()
{
    std::lock_guard<std::mutex> hold(lock);
    return !ended && ready.size() < Ahead;
}

void StreamFeed::Give(std::vector<int16_t>&& piece, bool atEnd, bool readFailed)
{
    std::lock_guard<std::mutex> hold(lock);
    if (!piece.empty()) ready.push_back(std::move(piece));
    if (atEnd) ended = true;
    if (readFailed) failed = true;
}

void MediaWorker::Start()
{
    if (thread.joinable()) return;
    stopping = false;
    thread = std::thread([this] { Run(); });
}

void MediaWorker::Stop()
{
    if (!thread.joinable()) return;
    {
        std::lock_guard<std::mutex> hold(lock);
        stopping = true;
    }
    wake.notify_all();
    thread.join();
    loads.clear();
    feeds.clear();
}

void MediaWorker::Load(std::shared_ptr<MediumLoad> load)
{
    {
        std::lock_guard<std::mutex> hold(lock);
        loads.push_back(std::move(load));
        woken = true;
    }
    wake.notify_one();
}

void MediaWorker::Feed(std::shared_ptr<StreamFeed> feed)
{
    {
        std::lock_guard<std::mutex> hold(lock);
        feeds.push_back(std::move(feed));
        woken = true;
    }
    wake.notify_one();
}

void MediaWorker::Wake()
{
    {
        std::lock_guard<std::mutex> hold(lock);
        woken = true;
    }
    wake.notify_one();
}

void MediaWorker::ForgetBank(uint32_t bankId)
{
    std::unique_lock<std::mutex> hold(lock);
    for (auto& load : loads)
        if (load->bank && load->bank->id == bankId) {
            load->why = "its bank was unloaded";
            load->state = -1;
        }
    loads.erase(std::remove_if(loads.begin(), loads.end(), [](const auto& l) { return l->state.load() != 0; }), loads.end());
    for (auto& feed : feeds)
        if (feed->stream.OfBank() && feed->stream.OfBank()->id == bankId) feed->cancelled = true;
    // A pass under way may be reading one of its media: wait for it to end.
    passDone.wait(hold, [this] { return !inPass; });
    feeds.erase(std::remove_if(feeds.begin(), feeds.end(), [](const auto& f) { return f->cancelled.load(); }), feeds.end());
}

void MediaWorker::Run()
{
    std::vector<int16_t> piece;
    for (;;) {
        std::shared_ptr<MediumLoad> load;
        std::vector<std::shared_ptr<StreamFeed>> streams;
        {
            std::unique_lock<std::mutex> hold(lock);
            wake.wait(hold, [this] { return stopping || woken; });
            if (stopping) return;
            woken = false;
            if (!loads.empty()) {
                load = std::move(loads.front());
                loads.pop_front();
            }
            feeds.erase(std::remove_if(feeds.begin(), feeds.end(), [](const auto& f) { return f->cancelled.load() || f->Finished(); }),
                        feeds.end());
            streams = feeds;
            inPass = true;
        }
        // A load first (a voice waits to start), then a piece for each stream that wants one; another pass follows at
        // once while any wants more.
        if (load) {
            bool ok = load->knownInfo || ReadMediumInfo(*load->bank, load->mediumId, load->info, load->why);
            if (ok) {
                load->whole = IsWhole(load->info.format, load->loopCount);
                if (load->whole) ok = DecodeWhole(load->info, load->samples, load->why);
            }
            load->state = ok ? 1 : -1;
        }
        bool more = false;
        for (auto& feed : streams) {
            if (feed->cancelled || !feed->WantsMore()) continue;
            if (feed->stopLooping) feed->stream.StopLooping();
            const int channels = std::max(1, feed->stream.Format().channels);
            piece.assign(static_cast<size_t>(feed->pieceFrames) * channels, 0);
            int frames = feed->stream.Read(piece.data(), feed->pieceFrames);
            piece.resize(static_cast<size_t>(std::max(0, frames)) * channels);
            feed->Give(std::move(piece), feed->stream.AtEnd(), feed->stream.failed);
            piece = std::vector<int16_t>();
            if (feed->WantsMore()) more = true;
        }
        {
            std::lock_guard<std::mutex> hold(lock);
            inPass = false;
            if (more || !loads.empty()) woken = true;
        }
        passDone.notify_all();
    }
}

}  // namespace rfa_audio
