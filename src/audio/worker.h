// The media worker: a thread of its own that reads and decodes media, so the engine's lock is never held while the
// disk is read. A voice whose medium hasn't been read yet waits for it (a load); a streamed voice takes pieces the
// worker decodes a few ahead (a feed). With a loopback device (the builder's check) the engine reads media itself,
// in step with the time it renders, and the worker isn't used.
#pragma once

#include "media.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rfa_audio {

// A medium read for a voice that waits for it: its format (from its .wem's head) if not known yet, and its samples
// if it is decoded whole. The worker fills it in and then sets state; the engine reads it once state isn't 0.
struct MediumLoad {
    const Bank* bank = nullptr;
    uint32_t mediumId = 0;
    int loopCount = 1;             // with its size, decides whether it is decoded whole (IsWhole)
    bool knownInfo = false;        // its format is known (in info): only its samples are wanted
    MediumInfo info;
    bool whole = false;            // decoded whole: samples hold it; otherwise it is to be streamed
    std::vector<int16_t> samples;
    std::string why;               // why it failed
    std::atomic<int> state{0};     // 0 being read, 1 read, -1 failed
};

// A medium streamed: the worker decodes pieces of it a few ahead; the engine takes them in turn.
struct StreamFeed {
    static constexpr size_t Ahead = 4;  // pieces kept decoded ahead
    Stream stream;                      // the worker's alone, once fed
    int pieceFrames = StreamBufferFrames;
    std::atomic<bool> cancelled{false}, stopLooping{false};

    // The next piece decoded (channels interleaved); false if none is ready yet.
    bool Take(std::vector<int16_t>& piece);
    // Every piece decoded has been taken and there are no more (its end, or it couldn't be read).
    bool Finished();
    bool Failed();

    // The worker's side.
    bool WantsMore();
    void Give(std::vector<int16_t>&& piece, bool atEnd, bool failed);

private:
    std::mutex lock;
    std::deque<std::vector<int16_t>> ready;  // oldest first
    bool ended = false, failed = false;
};

class MediaWorker {
public:
    ~MediaWorker() { Stop(); }
    void Start();
    void Stop();
    // A medium to read for a waiting voice.
    void Load(std::shared_ptr<MediumLoad> load);
    // A stream to keep fed (its Stream already opened).
    void Feed(std::shared_ptr<StreamFeed> feed);
    // A piece was taken: the worker may read the next.
    void Wake();
    // Drops every load and feed of a bank and returns once none of its media is being read (the bank is about to
    // be unloaded). The worker never takes the engine's lock, so the engine may call this holding it.
    void ForgetBank(uint32_t bankId);

private:
    void Run();
    std::thread thread;
    std::mutex lock;
    std::condition_variable wake, passDone;
    bool stopping = false, woken = false, inPass = false;
    std::deque<std::shared_ptr<MediumLoad>> loads;
    std::vector<std::shared_ptr<StreamFeed>> feeds;
};

}  // namespace rfa_audio
