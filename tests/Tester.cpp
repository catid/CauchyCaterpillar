#include "../CCatCpp.h"
#include "Logger.h"
#include "SiameseTools.h"
#include "StrikeRegister.h"

static logger::Channel Logger("Tester", logger::Level::Trace);

static const size_t kCheckLimit = 2000;

static const float plr = 0.20f;

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

int main()
{
    Logger.Info("Cauchy Caterpillar Tester");

    Sender sender;
    Receiver receiver;

    if (!sender.Initialize(kWindowMsec))
    {
        Logger.Error("Failed to initialize sender");
        return 1;
    }

    if (!receiver.Initialize(kWindowMsec))
    {
        Logger.Error("Failed to initialize receiver");
        return 1;
    }

    siamese::PCGRandom prng;
    prng.Seed(0);

    const uint32_t plrThresh = (uint32_t)(0xffffffff * plr);

    uint64_t sequence = 0;

    unsigned fecRate = 0;

    uint64_t t0 = siamese::GetTimeMsec();

    while (!sender.IsError() && !receiver.IsError())
    {
        // Generate a packet
        uint8_t data[kCheckLimit];
        size_t bytes = (prng.Next() % kCheckLimit) + 1;
        SetPacket(sequence, data, bytes);

        CCatOriginal original;
        original.SequenceNumber = sequence++;
        original.Data = data;
        original.Bytes = bytes;
        sender.SendOriginal(original);

        if (prng.Next() > plrThresh)
        {
            if (receiver.StrikeRegister.IsDuplicate(original.SequenceNumber))
            {
                Logger.Error("Saw duplicate sequence ", original.SequenceNumber);
                break;
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

            if (prng.Next() > plrThresh)
            {
                receiver.OnRecovery(recovery);
            }
        }

        const uint64_t t1 = siamese::GetTimeMsec();

        if (t1 - t0 > 1000)
        {
            Logger.Info("Got ", receiver.OriginalPackets, " / ", sequence, " = ", (receiver.OriginalPackets * 100.f / (float)sequence), "% - Recovered = ", (receiver.RecoveredPackets * 100.f / (float)sequence), "%");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    Logger.Error("Quit on error in codec");

    return 0;
}
