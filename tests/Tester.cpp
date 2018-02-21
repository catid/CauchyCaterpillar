#include "../CCatCpp.h"
#include "Logger.h"
#include "SiameseTools.h"
#include "StrikeRegister.h"

// Compiler-specific debug break
#if defined(_DEBUG) || defined(DEBUG)
    #define TESTER_DEBUG
    #if defined(_WIN32)
        #define TESTER_DEBUG_BREAK() __debugbreak()
    #else
        #define TESTER_DEBUG_BREAK() __builtin_trap()
    #endif
    #define TESTER_DEBUG_ASSERT(cond) { if (!(cond)) { TESTER_DEBUG_BREAK(); } }
#else
    #define TESTER_DEBUG_BREAK() do {} while (false);
    #define TESTER_DEBUG_ASSERT(cond) do {} while (false);
#endif

static logger::Channel Logger("Tester", logger::Level::Trace);

static const size_t kCheckLimit = 1000;

static const float plr = 0.2f;

static void SetPacket(uint64_t sequence, void* data, size_t bytes)
{
    siamese::PCGRandom prng;
    prng.Seed(sequence, bytes);

    uint8_t* buffer = (uint8_t*)data;

    if (bytes >= 4)
    {
        *(uint32_t*)(buffer) = (uint32_t)bytes;
        buffer += 4, bytes -= 4;
    }
    while (bytes >= 4)
    {
        *(uint32_t*)(buffer) = prng.Next();
        buffer += 4, bytes -= 4;
    }
    if (bytes > 0)
    {
        uint32_t x = prng.Next();
        for (size_t i = 0; i < bytes; ++i)
        {
            buffer[i] = (uint8_t)x;
            x >>= 8;
        }
    }
}

static bool CheckPacket(uint64_t sequence, const void* data, size_t bytes)
{
    uint8_t expected[kCheckLimit];
    SIAMESE_DEBUG_ASSERT(bytes <= kCheckLimit);
    SetPacket(sequence, expected, bytes);
    return 0 == memcmp(expected, data, bytes);
}

static const unsigned kWindowMsec = 100;

class Sender : public CauchyCaterpillar
{
public:
};

class Receiver : public CauchyCaterpillar
{
public:
    uint64_t RecoveredPackets = 0;
    uint64_t OriginalPackets = 0;
    security::StrikeRegister StrikeRegister;

    void OnRecoveredData(
        const uint8_t* data, ///< Packet data
        unsigned bytes,      ///< Data bytes
        uint64_t sequence    ///< Sequence number of recovered packet
    ) override
    {
        if (StrikeRegister.IsDuplicate(sequence))
        {
            Logger.Error("Saw duplicate sequence ", sequence);
            Error = true;
            return;
        }

        const bool check = CheckPacket(sequence, data, bytes);
        if (!check)
        {
            Error = true;
            Logger.Error("Corrupted packet ", sequence);
            return;
        }

        StrikeRegister.Accept(sequence);

        ++RecoveredPackets;
        ++OriginalPackets;
    }
};

// Number of parallel runs to simulate
static const unsigned kParallelRuns = 100;

// Number of packets to send per burst
static const unsigned kSpeedMultiplier = 10;

static const uint32_t kPlrThresh = (uint32_t)(0xffffffff * plr);

struct RunState
{
    Sender sender;
    Receiver receiver;
    siamese::PCGRandom prng;
    uint64_t sequence = 0;
    unsigned fecRate = 0;
    unsigned packetsSent = 0;

    bool Initialize(unsigned runIndex, uint64_t seed)
    {
        if (!sender.Initialize(kWindowMsec))
        {
            Logger.Error("Failed to initialize sender");
            return false;
        }

        if (!receiver.Initialize(kWindowMsec))
        {
            Logger.Error("Failed to initialize receiver");
            return false;
        }

        prng.Seed(runIndex, seed);

        return true;
    }

    bool Run()
    {
        // Generate a packet
        uint8_t data[kCheckLimit];
        size_t bytes = (prng.Next() % kCheckLimit) + 1;
        SetPacket(sequence, data, bytes);

        CCatOriginal original;
        original.SequenceNumber = sequence++;
        original.Data = data;
        original.Bytes = (unsigned)bytes;
        sender.SendOriginal(original);
        ++packetsSent;

        if (prng.Next() > kPlrThresh)
        {
            if (receiver.StrikeRegister.IsDuplicate(original.SequenceNumber))
            {
                Logger.Error("Saw duplicate sequence ", original.SequenceNumber);
                return false;
            }
            // Would check packet here
            receiver.StrikeRegister.Accept(original.SequenceNumber);

            ++receiver.OriginalPackets;
            receiver.OnOriginal(original);
        }

        // Send FEC every 3 original packets
        // 25% of traffic is FEC
        if (++fecRate >= 3)
        {
            fecRate = 0;

            CCatRecovery recovery;
            sender.SendRecovery(recovery);

            if (prng.Next() > kPlrThresh)
            {
                receiver.OnRecovery(recovery);
            }
        }

        return (!sender.IsError() && !receiver.IsError());
    }

    float GetEffLoss() const
    {
        return receiver.OriginalPackets / (float)sequence;
    }

    unsigned GetResetPacketCounter()
    {
        unsigned value = packetsSent;
        packetsSent = 0;
        return value;
    }
};

template<typename T> struct StatsCollector
{
    T minimum = 0, maximum = 0, sum = 0;
    unsigned count = 0;

    void Update(T value)
    {
        if (count == 0)
        {
            maximum = minimum = sum = value;
            count = 1;
            return;
        }

        if (minimum > value)
            minimum = value;
        if (maximum < value)
            maximum = value;
        sum += value;
        ++count;
    }

    float Average() const
    {
        if (count == 0)
            return 0.f;
        return sum / count;
    }
};

int main()
{
    Logger.Info("Cauchy Caterpillar Tester");

    RunState Runs[kParallelRuns];

    static const uint64_t kExperimentSeed = 0;

    for (unsigned i = 0; i < kParallelRuns; ++i)
    {
        if (!Runs[i].Initialize(i, kExperimentSeed))
        {
            Logger.Error("Initialization failed ", i);
            TESTER_DEBUG_BREAK();
            return -1;
        }
    }

    uint64_t t0 = siamese::GetTimeMsec();

    for (;;)
    {
        for (unsigned i = 0; i < kParallelRuns; ++i)
        {
            for (unsigned j = 0; j < kSpeedMultiplier; ++j)
            {
                if (!Runs[i].Run()) {
                    Logger.Error("A codec experienced an error and had to stop");
                    TESTER_DEBUG_BREAK();
                    return -1;
                }
            }
        }

        const uint64_t t1 = siamese::GetTimeMsec();

        if (t1 - t0 > 1000)
        {
            t0 = t1;

            StatsCollector<float> eloss;
            StatsCollector<unsigned> count;
            for (unsigned i = 0; i < kParallelRuns; ++i) {
                eloss.Update(Runs[i].GetEffLoss());
                count.Update(Runs[i].GetResetPacketCounter());
            }

            Logger.Info(Runs[0].sequence, ": ", eloss.minimum * 100.f, "% / ",
                eloss.Average() * 100.f, "% / ", eloss.maximum * 100.f,
                "% (min/avg/max) effective loss. ", count.Average(), " originals/second");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    TESTER_DEBUG_BREAK();
    Logger.Error("Quit on error in codec");

    return 0;
}
