/** \file
    \brief Cauchy Caterpillar Tester
    \copyright Copyright (c) 2017 Christopher A. Taylor.  All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of CCat nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#include "../CCatCpp.h"
#include "Logger.h"
#include "SiameseTools.h"
#include "StrikeRegister.h"

#include <omp.h> // Requires OpenMP for parallel for

#include <fstream>
using namespace std;


// Window length in time
static const unsigned kWindowMsec = 100;

// Maximum size of test packets
static const size_t kTestPacketMaxBytes = 33;

// Duration of test in seconds
const unsigned kDurationSeconds = 10;

// Number of parallel runs to simulate
static const unsigned kParallelRuns = 5000;

// Simulate ~4 Mbps stream (1300 byte packets at 385 packets per second)
static const int kPacketsPerSecond = 385;


static std::atomic<bool> m_TestFailed;


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
    uint8_t expected[kTestPacketMaxBytes];
    SIAMESE_DEBUG_ASSERT(bytes <= kTestPacketMaxBytes);
    SetPacket(sequence, expected, bytes);
    return 0 == memcmp(expected, data, bytes);
}

class TestSender : public CauchyCaterpillar
{
public:
};

class TestReceiver : public CauchyCaterpillar
{
public:
    uint64_t RecoveredPackets = 0;
    uint64_t OriginalPackets = 0;
    security::StrikeRegister StrikeRegister;

    void OnRecoveredData(const CCatOriginal& original) override
    {
        if (StrikeRegister.IsDuplicate(original.SequenceNumber))
        {
            Logger.Error("Saw duplicate sequence ", original.SequenceNumber);
            Error = true;
            return;
        }

        const bool check = CheckPacket(
            original.SequenceNumber,
            original.Data,
            original.Bytes);
        if (!check)
        {
            Error = true;
            Logger.Error("Corrupted packet ", original.SequenceNumber);
            return;
        }

        StrikeRegister.Accept(original.SequenceNumber);

        ++RecoveredPackets;
        ++OriginalPackets;
    }
};

struct RunState
{
    TestSender Sender;
    TestReceiver Receiver;
    siamese::PCGRandom Prng;
    uint64_t Sequence = 0;
    uint64_t FECSent = 0;
    unsigned PacketsSent = 0;
    float FECRate = 0.f;

    bool Initialize(unsigned runIndex, uint64_t seed, float fecRate)
    {
        FECRate = fecRate;

        if (!Sender.Initialize(kWindowMsec))
        {
            Logger.Error("Failed to initialize sender");
            return false;
        }

        if (!Receiver.Initialize(kWindowMsec))
        {
            Logger.Error("Failed to initialize receiver");
            return false;
        }

        Prng.Seed(runIndex, seed);

        return true;
    }

    bool Run(float plr)
    {
        // Generate a packet
        uint8_t data[kTestPacketMaxBytes];
        size_t bytes = (Prng.Next() % kTestPacketMaxBytes) + 1;
        SetPacket(Sequence, data, bytes);

        CCatOriginal original;
        original.SequenceNumber = Sequence++;
        original.Data = data;
        original.Bytes = (unsigned)bytes;
        Sender.SendOriginal(original);
        ++PacketsSent;

        // Precalculate PLR 32-bit PRNG threshold
        const uint32_t kPlrPRNG32Thresh = (uint32_t)(0xffffffff * plr);

        if (Prng.Next() > kPlrPRNG32Thresh)
        {
            if (Receiver.StrikeRegister.IsDuplicate(original.SequenceNumber))
            {
                Logger.Error("Saw duplicate sequence ", original.SequenceNumber);
                return false;
            }
            // Would check packet here
            Receiver.StrikeRegister.Accept(original.SequenceNumber);

            ++Receiver.OriginalPackets;
            Receiver.OnOriginal(original);
        }

        // Maintain a fixed FEC rate >= fec / (original + fec)
        if (FECSent < (uint64_t)(FECRate * (Sequence + FECSent)))
        {
            CCatRecovery recovery;
            Sender.SendRecovery(recovery);
            ++FECSent;

            if (Prng.Next() > kPlrPRNG32Thresh) {
                Receiver.OnRecovery(recovery);
            }
        }

        return (!Sender.IsError() && !Receiver.IsError());
    }

    float GetEffLoss() const
    {
        return 1.f - Receiver.OriginalPackets / (float)Sequence;
    }

    unsigned GetResetPacketCounter()
    {
        unsigned value = PacketsSent;
        PacketsSent = 0;
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

    T Average() const
    {
        if (count == 0)
            return 0;
        return sum / count;
    }
};

struct TestResults
{
    unsigned PacketsPerSecond = 0;
    float MinimumEffectiveLoss = 0.f;
    float AverageEffectiveLoss = 0.f;
    float MaximumEffectiveLoss = 0.f;
};

void SimulateOneStream(RunState* state, float plr, int i)
{
    uint64_t t0 = siamese::GetTimeMsec();

    int packet_count = 0;
    const int total_packet_count = kPacketsPerSecond * kDurationSeconds;

    for (;;)
    {
        const uint64_t t1 = siamese::GetTimeMsec();
        const int64_t dt = t1 - t0;

        // Calculate number of packets we should have sent
        const int expected_packet_count = static_cast<int>((dt * kPacketsPerSecond) / 1000);

        for (; packet_count < expected_packet_count; ++packet_count)
        {
            if (packet_count >= total_packet_count) {
                return; // Done!
            }

            // This sends one simulated packet.
            if (!state->Run(plr))
            {
                Logger.Error("Codec ", i, " experienced an error and had to stop");
                TESTER_DEBUG_BREAK();
                m_TestFailed = true;
                return;
            }
        }

        // Sends in bursts every 10 msec
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool GetMinimumResult(
    float plr,
    float fec,
    TestResults& results)
{
    RunState* Runs = new RunState[kParallelRuns];

    static const uint64_t kExperimentSeed = 0;

    for (unsigned i = 0; i < kParallelRuns; ++i)
    {
        if (!Runs[i].Initialize(i, kExperimentSeed, fec))
        {
            Logger.Error("Initialization failed ", i);
            TESTER_DEBUG_BREAK();
            return false;
        }
    }

    std::atomic<bool> failed = ATOMIC_VAR_INIT(false);

#pragma omp parallel for
    for (int i = 0; i < kParallelRuns; ++i)
    {
        SimulateOneStream(&Runs[i], plr, i);
    }

    if (failed) {
        Logger.Error("A codec experienced an error and had to stop");
        return false;
    }

    StatsCollector<float> eloss;
    StatsCollector<unsigned> count;
    StatsCollector<unsigned> fecsent;
    for (unsigned i = 0; i < kParallelRuns; ++i) {
        eloss.Update(Runs[i].GetEffLoss());
        count.Update(Runs[i].GetResetPacketCounter());
        fecsent.Update((unsigned)Runs[i].FECSent);
    }

    results.PacketsPerSecond = (unsigned)(count.Average() / (float)kDurationSeconds);
    results.MinimumEffectiveLoss = eloss.minimum * 100.f;
    results.AverageEffectiveLoss = eloss.Average() * 100.f;
    results.MaximumEffectiveLoss = eloss.maximum * 100.f;

    delete[] Runs;
    return true;
}

static ofstream m_File;
static std::mutex m_SyncLock;

void TestFECRate(float plr, float fec)
{
    TestResults results;
    if (!GetMinimumResult(plr, fec, results)) {
        m_TestFailed = true;
        return;
    }

    std::lock_guard<std::mutex> locker(m_SyncLock);

    m_File << plr * 100.f << "\t" << fec * 100.f << "\t" << results.PacketsPerSecond << "\t" <<
        results.MinimumEffectiveLoss << "\t" << results.AverageEffectiveLoss << "\t" <<
        results.MaximumEffectiveLoss << endl;

    Logger.Info(plr * 100.f, "\t", fec * 100.f, "\t", results.PacketsPerSecond, "\t",
        results.MinimumEffectiveLoss, "\t", results.AverageEffectiveLoss, "\t",
        results.MaximumEffectiveLoss);
}

int main()
{
    Logger.Info("Cauchy Caterpillar Tester");

    omp_set_num_threads(kParallelRuns);

    Logger.Info("This is running ", kParallelRuns, " parallel simulations in realtime for ", kDurationSeconds,
        " seconds");
    Logger.Info(" for different Packet Loss Rates (PLR) and different Forward Error Correction (FEC) overhead.");
    Logger.Info("The FEC used is called Cauchy Caterpillar (CCat).");
    Logger.Info("It is a short-window (", kWindowMsec, " milliseconds) convolutional code.");
    Logger.Info("For each PLR, FEC, Packets/Second (PPS), the min/avg/max Effective Packet Loss Rate (EPLR) is presented.");
    Logger.Info("This is the percentage loss rate experienced by the application in spite of FEC being used.");

    static const char* kLeaderStr = "PLR%\tFEC%\tPPS\tEPLR%Min\tEPLR%Avg\tEPLR%Max";

    m_File.open("simulation_results.txt");
    if (!m_File)
    {
        Logger.Error("Unable to open output file");
        return -1;
    }
    m_File << kLeaderStr << endl;

    Logger.Info(kLeaderStr);

    m_TestFailed = false;

    for (float plr = 0.01f; plr < 0.1f; plr += 0.005f)
    {
        for (int i = 20 * 2; i >= 0; --i)
        {
            const float fec = i * 0.005f;

            TestFECRate(plr, fec);
        }

        if (m_TestFailed)
        {
            TESTER_DEBUG_BREAK();
            Logger.Error("Quit on error in codec");
            return -1;
        }
    }

    TESTER_DEBUG_BREAK();
    Logger.Error("Quit on error in codec");

    return 0;
}
