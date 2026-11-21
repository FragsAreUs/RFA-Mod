// A fix for a race in the game's own world streaming. A part of rfa_qol.dll; [Mods] streaming=0 turns
// it off.
//
// When zones stream out, the game saves them on two worker threads at once, one batch of zones at a
// time (the main thread's 0x6D70D0 hands out both jobs, one after the other):
//   the producer 0x6D6C80 (job 0x6D7800) goes through the batch's zones and, for each, writes its
//     objects into the zone's buffers (0x6D7820: the zone's +0x5C, +0x60 and +0x64), then counts the
//     zone as done in 0x187F374, which it sets to 0 first;
//   the consumer 0x6D6EA0 (job 0x6D7810) takes the zones in the same order, waits for each until the
//     producer's count says it is done (0x187F374 > its own place, 0x187F370), then saves it and frees
//     its buffers (0x6D7A40, then 0x6D7CC0).
// Only the producer resets the count, as the first thing it does. When the consumer gets to look
// before that, the count is still the last batch's, so it takes the first zone at once, saves it
// half written and frees its buffers while the producer is still writing into them: the producer
// then reads the freed list and crashes (0xC94D2F reading 0x168; 2026-11-07's crash in rfg_07_11).
//
// Here the two jobs are replaced with ones that call the same two functions, in the same way, except
// that the producer sets the count to 0 before anything else and says so, and the consumer waits until
// the producer of its own batch has. The game always hands out one producer and one consumer together,
// so the n-th consumer waits for the n-th producer. Should they ever get out of step, the consumer
// stops waiting after a few seconds, says so in the log and goes on as the game would.
#define RFA_MOD_NAME "streaming"
#include "rfa_mod.h"
#include "rfa_qol.h"

#include <atomic>

namespace {

constexpr DWORD kWorld = 0x1870630;             // the world (this of both functions)
constexpr DWORD kProducerJob = 0x6D7800;        // the producer's job: 0x6D6C80(world)
constexpr DWORD kConsumerJob = 0x6D7810;        // the consumer's job: 0x6D6EA0(world)
constexpr DWORD kProduce = 0x6D6C80;
constexpr DWORD kConsume = 0x6D6EA0;
constexpr DWORD kZonesDone = 0x187F374;         // the producer's count of zones written
constexpr DWORD kResetZonesDone = 0x6D6C89;     // the producer's `mov dword ptr [187F374h],0`
constexpr DWORD kConsumerReadsDone = 0x6D6EE2;  // the consumer's `mov edx,dword ptr [187F374h]`
constexpr DWORD kWaitLimitMs = 5000;

using StreamOutFn = void(__thiscall*)(void* world);

std::atomic<unsigned> g_producersStarted{0};
std::atomic<unsigned> g_consumersStarted{0};
std::atomic<int> g_waitsLogged{0};

void* World() { return reinterpret_cast<void*>(kWorld); }

volatile LONG& ZonesDone() { return *reinterpret_cast<volatile LONG*>(kZonesDone); }

// The producer's job.
void __cdecl ProducerJob() {
    InterlockedExchange(&ZonesDone(), 0);  // as 0x6D6C80 does first, but before the consumer may look
    g_producersStarted.fetch_add(1);
    reinterpret_cast<StreamOutFn>(kProduce)(World());
}

// The consumer's job.
void __cdecl ConsumerJob() {
    const unsigned mine = g_consumersStarted.fetch_add(1) + 1;
    const DWORD start = GetTickCount();
    while (g_producersStarted.load() < mine) {
        if (GetTickCount() - start > kWaitLimitMs) {
            // Out of step: take the producers' count as ours, so the next batch pairs up again.
            g_consumersStarted.store(g_producersStarted.load());
            if (g_waitsLogged.fetch_add(1) < 8)
                rfa::Log("A zone stream-out's saving job waited %lu ms for its writing job to start; it goes on without it",
                         kWaitLimitMs);
            break;
        }
        Sleep(1);
    }
    reinterpret_cast<StreamOutFn>(kConsume)(World());
}

// The two jobs are `push ebp; mov ebp,esp; mov ecx,1870630h; call <function>; pop ebp; ret`.
bool JobIs(DWORD job, DWORD function) {
    const BYTE* code = reinterpret_cast<const BYTE*>(job);
    const DWORD callTarget = job + 13 + rfa::CodeValue(job + 9);
    return code[0] == 0x55 && code[1] == 0x8B && code[2] == 0xEC && code[3] == 0xB9 &&
           rfa::CodeValue(job + 4) == kWorld && code[8] == 0xE8 && callTarget == function && code[13] == 0x5D &&
           code[14] == 0xC3;
}

}  // namespace

namespace streaming {

void Init() {
    if (!JobIs(kProducerJob, kProduce) || !JobIs(kConsumerJob, kConsume) ||
        rfa::CodeValue(kResetZonesDone + 2) != kZonesDone || rfa::CodeValue(kConsumerReadsDone + 2) != kZonesDone) {
        rfa::Log("The zone stream-out's jobs (0x%08lX, 0x%08lX) aren't as expected; the game keeps its own", kProducerJob,
                 kConsumerJob);
        return;
    }
    if (!rfa::ReplaceFunction(kProducerJob, reinterpret_cast<const void*>(&ProducerJob)) ||
        !rfa::ReplaceFunction(kConsumerJob, reinterpret_cast<const void*>(&ConsumerJob))) {
        rfa::Log("Could not change the zone stream-out's jobs");
        return;
    }
    rfa::LogDetail("Zone stream-out: the saving job waits for its writing job to start");
}

}  // namespace streaming
