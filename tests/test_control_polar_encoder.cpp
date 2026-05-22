#include "slb/control_polar_encoder.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using slb::control::Bit;

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct TestCase {
    std::string name;
    std::function<void()> run;
};

template <typename T, typename U>
void requireEqual(const T& actual, const U& expected, const std::string& message) {
    if (!(actual == expected)) {
        std::ostringstream oss;
        oss << message << " (actual=" << actual << ", expected=" << expected << ")";
        throw TestFailure(oss.str());
    }
}

template <typename T>
void requireVectorEqual(const std::vector<T>& actual,
                        const std::vector<T>& expected,
                        const std::string& message) {
    if (actual != expected) {
        std::ostringstream oss;
        oss << message << " (actual=[";
        for (std::size_t i = 0; i < actual.size(); ++i) {
            if (i != 0) {
                oss << ",";
            }
            oss << actual[i];
        }
        oss << "], expected=[";
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (i != 0) {
                oss << ",";
            }
            oss << expected[i];
        }
        oss << "])";
        throw TestFailure(oss.str());
    }
}

void requireThrows(const std::function<void()>& fn, const std::string& message) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw TestFailure(message);
}

std::vector<Bit> alternatingBits(std::size_t length, std::size_t phase = 0) {
    std::vector<Bit> bits(length);
    for (std::size_t i = 0; i < length; ++i) {
        bits[i] = static_cast<Bit>(((i + phase) * 3 + 1) % 2);
    }
    return bits;
}

std::vector<std::size_t> inversePermutation(const std::array<std::size_t, 62>& permutation) {
    std::vector<std::size_t> inverse(62);
    for (std::size_t i = 0; i < permutation.size(); ++i) {
        inverse[permutation[i]] = i;
    }
    return inverse;
}

void testDefaultInterleaversArePermutations() {
    const auto interleavers = slb::control::defaultInterleaverSequences();
    for (std::size_t seq = 0; seq < interleavers.size(); ++seq) {
        std::vector<std::size_t> values(interleavers[seq].begin(), interleavers[seq].end());
        std::sort(values.begin(), values.end());
        for (std::size_t i = 0; i < values.size(); ++i) {
            requireEqual(values[i], i, "default Iseq must be a permutation of 0..61");
        }
    }
}

void testMotherCodeLengthAndPcParameters() {
    {
        auto params = slb::control::deriveControlPolarParameters(17, 1, 10);
        requireEqual(params.n, std::size_t{5}, "n must clamp to nmin");
        requireEqual(params.motherCodeLength, std::size_t{32}, "N must clamp to 32");
        requireEqual(params.nPc, std::size_t{0}, "K=17 must not use PC bits");
    }
    {
        auto params = slb::control::deriveControlPolarParameters(18, 64, 207);
        requireEqual(params.nPc, std::size_t{3}, "K=18 must use 3 PC bits");
        requireEqual(params.nPcByWeight, std::size_t{0}, "threshold <=192 must use nPCwm=0");
    }
    {
        auto params = slb::control::deriveControlPolarParameters(18, 64, 208);
        requireEqual(params.nPc, std::size_t{3}, "K=18 must use 3 PC bits");
        requireEqual(params.nPcByWeight, std::size_t{1}, "threshold >192 must use nPCwm=1");
    }
    {
        auto params = slb::control::deriveControlPolarParameters(26, 5000, 62);
        requireEqual(params.n, std::size_t{10}, "n must clamp to control nmax");
        requireEqual(params.motherCodeLength, std::size_t{1024}, "control N max must be 1024");
        requireEqual(params.nPc, std::size_t{0}, "K=26 must not use PC bits");
    }
}

void testReliabilitySequenceForLength() {
    auto natural = slb::control::naturalReliabilitySequence(1024);
    auto q64 = slb::control::reliabilitySequenceForLength(natural, 64);
    requireEqual(q64.size(), std::size_t{64}, "Q_N must have N entries");
    requireEqual(q64.front(), std::size_t{0}, "natural Q_N first index");
    requireEqual(q64.back(), std::size_t{63}, "natural Q_N last index");

    requireThrows([] {
        slb::control::reliabilitySequenceForLength({0, 1, 1, 3}, 4);
    }, "duplicate reliability indexes must be rejected");
    requireThrows([] {
        slb::control::reliabilitySequenceForLength({0, 1, 2}, 4);
    }, "incomplete reliability coverage must be rejected");
}

void testStandardReliabilitySequenceC1Integrity() {
    auto sequence = slb::control::standardReliabilitySequence();
    requireEqual(sequence.size(), std::size_t{8192}, "C.1 sequence must contain 8192 values");

    std::vector<std::size_t> sorted = sequence;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        requireEqual(sorted[i], i, "C.1 sequence must be a permutation of 0..8191");
    }

    requireVectorEqual(std::vector<std::size_t>(sequence.begin(), sequence.begin() + 16),
                       std::vector<std::size_t>({0, 1, 2, 4, 8, 16, 32, 3,
                                                 5, 64, 9, 6, 17, 10, 18, 128}),
                       "C.1 first 16 sentinel values");
    requireVectorEqual(std::vector<std::size_t>(sequence.end() - 16, sequence.end()),
                       std::vector<std::size_t>({8179, 8181, 7935, 8182,
                                                 8185, 8063, 8186, 8183,
                                                 8188, 8187, 8175, 8127,
                                                 8190, 8191, 8159, 8189}),
                       "C.1 last 16 sentinel values");

    const auto q1024 = slb::control::reliabilitySequenceForLength(sequence, 1024);
    requireEqual(q1024.size(), std::size_t{1024}, "C.1-derived Q_1024 length");
    requireVectorEqual(std::vector<std::size_t>(q1024.begin(), q1024.begin() + 16),
                       std::vector<std::size_t>({0, 1, 2, 4, 8, 16, 32, 3,
                                                 5, 64, 9, 6, 17, 10, 18, 128}),
                       "C.1-derived Q_1024 first 16 values");
}

void testPolarTransformSmallVectors() {
    requireVectorEqual(slb::control::polarTransform({0, 1}), std::vector<Bit>({1, 1}),
                       "N=2 Polar transform");
    requireVectorEqual(slb::control::polarTransform({1, 0, 1, 1}),
                       std::vector<Bit>({1, 1, 0, 1}), "N=4 Polar transform");
}

void testPolarTransformLinearity() {
    for (std::size_t n : {32U, 64U, 128U}) {
        auto a = alternatingBits(n, 0);
        auto b = alternatingBits(n, 1);
        std::vector<Bit> axorb(n);
        for (std::size_t i = 0; i < n; ++i) {
            axorb[i] = static_cast<Bit>(a[i] ^ b[i]);
        }
        auto ta = slb::control::polarTransform(a);
        auto tb = slb::control::polarTransform(b);
        auto taxorb = slb::control::polarTransform(axorb);
        std::vector<Bit> combined(n);
        for (std::size_t i = 0; i < n; ++i) {
            combined[i] = static_cast<Bit>(ta[i] ^ tb[i]);
        }
        requireVectorEqual(combined, taxorb, "Polar transform must be linear over GF(2)");
    }
}

void testBitSelectionNoPcNaturalReliability() {
    auto q = slb::control::reliabilitySequenceForLength(
        slb::control::naturalReliabilitySequence(1024), 64);
    auto selection = slb::control::selectBitPositions(q, 10, 0, 64, 62, 64);
    requireEqual(selection.t, std::size_t{10}, "E0>=N must set T=K+nPC");
    requireVectorEqual(selection.informationBits,
                       std::vector<std::size_t>({54, 55, 56, 57, 58, 59, 60, 61, 62, 63}),
                       "natural reliability should select the highest indexes");
}

void testBitSelectionFormulaBranches() {
    auto q32 = slb::control::reliabilitySequenceForLength(
        slb::control::naturalReliabilitySequence(1024), 32);

    requireEqual(slb::control::selectBitPositions(q32, 2, 0, 16, 16, 32).t,
                 std::size_t{1}, "punching branch E0/N < 5/8");
    requireEqual(slb::control::selectBitPositions(q32, 4, 0, 21, 21, 32).t,
                 std::size_t{4}, "punching branch E0/N < 3/4");
    requireEqual(slb::control::selectBitPositions(q32, 5, 0, 25, 25, 32).t,
                 std::size_t{5}, "punching branch E0/N >= 3/4");
    requireEqual(slb::control::selectBitPositions(q32, 8, 0, 17, 17, 32).t,
                 std::size_t{8}, "shortening branch E0/N < 9/16");
    requireEqual(slb::control::selectBitPositions(q32, 9, 0, 20, 20, 32).t,
                 std::size_t{9}, "shortening branch E0/N >= 9/16");
}

void testShorteningTemporaryFrozenBits() {
    auto q = slb::control::reliabilitySequenceForLength(
        slb::control::naturalReliabilitySequence(1024), 128);
    auto selection = slb::control::selectBitPositions(q, 40, 0, 80, 80, 128);
    requireEqual(selection.t, std::size_t{40}, "shortening example T");
    requireEqual(selection.temporaryFrozenBits.front(), std::size_t{80},
                 "shortening should freeze [E0:N-1]");
    requireEqual(selection.temporaryFrozenBits.back(), std::size_t{127},
                 "shortening should freeze through N-1");
    requireVectorEqual(selection.informationBits,
                       std::vector<std::size_t>({40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                                                 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                                                 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
                                                 70, 71, 72, 73, 74, 75, 76, 77, 78, 79}),
                       "shortening should choose highest reliable non-frozen natural indexes");
}

void testPcBitSelection() {
    auto q = slb::control::reliabilitySequenceForLength(
        slb::control::naturalReliabilitySequence(1024), 64);
    auto selection = slb::control::selectBitPositions(q, 18, 3, 64, 208, 64);
    auto pcBits = slb::control::selectPcBits(q, selection.informationBits, 3, 1);
    requireVectorEqual(pcBits, std::vector<std::size_t>({43, 44, 48}),
                       "weighted PC selection should follow reliability and row weight rules");

    auto pcBitsNoWeight = slb::control::selectPcBits(q, selection.informationBits, 3, 0);
    requireVectorEqual(pcBitsNoWeight, std::vector<std::size_t>({43, 44, 45}),
                       "nPCwm=0 should use the least reliable information positions");
}

void testPolarInputWithPcConsumesInformationBits() {
    auto q = slb::control::reliabilitySequenceForLength(
        slb::control::naturalReliabilitySequence(1024), 64);
    auto selection = slb::control::selectBitPositions(q, 18, 3, 64, 208, 64);
    auto pcBits = slb::control::selectPcBits(q, selection.informationBits, 3, 1);
    auto input = alternatingBits(18);
    auto u = slb::control::buildPolarInput(input, 64, selection.informationBits, pcBits);
    requireEqual(u.size(), std::size_t{64}, "u length must equal N");
    std::set<std::size_t> info(selection.informationBits.begin(), selection.informationBits.end());
    std::set<std::size_t> pc(pcBits.begin(), pcBits.end());
    std::size_t dataCount = 0;
    for (std::size_t i = 0; i < u.size(); ++i) {
        if (info.count(i) == 0) {
            requireEqual(u[i], Bit{0}, "frozen positions must be zero");
        } else if (pc.count(i) == 0) {
            ++dataCount;
        }
    }
    requireEqual(dataCount, std::size_t{18}, "non-PC information positions must equal K");
}

void testRateMatchPlanAndOutput() {
    auto plan = slb::control::makeRateMatchPlan(10, 64, 64, 8, 2);
    requireEqual(plan.circularBufferLength, std::size_t{64}, "puncturing/IR M");
    requireEqual(plan.circularBufferStart, std::size_t{32}, "rvid=2 k0 for M=64");
    requireVectorEqual(plan.sourceIndexes,
                       std::vector<std::size_t>({32, 33, 34, 35, 36, 37, 38, 39}),
                       "rate match source indexes");

    std::vector<Bit> encoded(64);
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        encoded[i] = static_cast<Bit>((i / 2) % 2);
    }
    auto out = slb::control::rateMatch(encoded, plan);
    requireVectorEqual(out, std::vector<Bit>({0, 0, 1, 1, 0, 0, 1, 1}),
                       "rate match output must follow source indexes");
}

void testRateMatchShorteningPlan() {
    auto plan = slb::control::makeRateMatchPlan(40, 80, 128, 10, 1);
    requireEqual(plan.circularBufferLength, std::size_t{80}, "shortening/CC M");
    requireEqual(plan.circularBufferStart, std::size_t{0}, "rvid=1 k0 for M<128");
    requireEqual(plan.sourceIndexes.size(), std::size_t{10}, "rate match length");
}

void testChannelInterleaveIndexMap() {
    const auto interleavers = slb::control::defaultInterleaverSequences();
    const auto map = slb::control::channelInterleaveIndexMap(124, interleavers);
    requireEqual(map.size(), std::size_t{124}, "interleaver map length");
    std::vector<std::size_t> sorted = map;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        requireEqual(sorted[i], i, "interleaver map must be a permutation");
    }

    const auto inverse0 = inversePermutation(interleavers[0]);
    const auto inverse1 = inversePermutation(interleavers[1]);
    requireEqual(map[0], inverse0[0], "first output is row 0 bank 0 column 0");
    requireEqual(map[31], 62 + inverse1[0], "row 1 bank 0 follows row 0 bank 0");
    requireEqual(map[62], inverse0[31], "bank 1 starts after all bank 0 rows");
    requireEqual(map[93], 62 + inverse1[31], "row 1 bank 1 position");
}

void testChannelInterleaveBits() {
    std::vector<Bit> e(62);
    for (std::size_t i = 0; i < e.size(); ++i) {
        e[i] = static_cast<Bit>((i % 3) == 0);
    }
    auto interleaved = slb::control::channelInterleave(
        e, slb::control::defaultInterleaverSequences());
    auto map = slb::control::channelInterleaveIndexMap(
        e.size(), slb::control::defaultInterleaverSequences());
    for (std::size_t i = 0; i < interleaved.size(); ++i) {
        requireEqual(interleaved[i], e[map[i]], "interleaved bit must match index map");
    }
}

void testEndToEndSingleBlockGolden() {
    auto config = slb::control::makeEncoderConfig(slb::control::naturalReliabilitySequence(1024));
    std::vector<slb::control::ControlBlockInput> blocks = {
        {alternatingBits(10), 64, 62, 0},
    };
    auto result = slb::control::encodeControlInfo(blocks, config);
    requireEqual(result.bits.size(), std::size_t{62}, "single block output length");
    requireEqual(result.blocks.size(), std::size_t{1}, "single block debug count");
    requireEqual(result.blocks[0].parameters.motherCodeLength, std::size_t{64},
                 "single block N");
    requireEqual(result.blocks[0].rateMatchPlan.circularBufferStart, std::size_t{0},
                 "single block k0");
    requireEqual(slb::control::bitsToString(result.bits),
                 std::string("10010111001100011110001001110000000000000000000000000000000000"),
                 "single block golden output");
    requireVectorEqual(result.bits, result.blocks[0].interleavedBits,
                       "single block output must equal f_0");
}

void testEndToEndMultiBlockGolden() {
    auto config = slb::control::makeEncoderConfig(slb::control::naturalReliabilitySequence(1024));
    std::vector<slb::control::ControlBlockInput> blocks = {
        {alternatingBits(10), 64, 62, 0},
        {alternatingBits(18, 1), 64, 124, 2},
    };
    auto result = slb::control::encodeControlInfo(blocks, config);
    requireEqual(result.bits.size(), std::size_t{186}, "multi-block output length");
    requireEqual(result.blocks.size(), std::size_t{2}, "multi-block debug count");
    requireEqual(result.blocks[1].parameters.nPc, std::size_t{3}, "second block nPC");
    requireEqual(result.blocks[1].rateMatchPlan.circularBufferStart, std::size_t{32},
                 "second block rvid=2 k0");
    requireEqual(slb::control::bitsToString(result.bits),
                 std::string("10010111001100011110001001110000000000000000000000000000000000"
                             "01011110011001001010010101001010110110000101110000111111001100"
                             "11000000111001111011010011010000101111001100100101001110010101"),
                 "multi-block golden output");
    std::vector<Bit> concatenated = result.blocks[0].interleavedBits;
    concatenated.insert(concatenated.end(),
                        result.blocks[1].interleavedBits.begin(),
                        result.blocks[1].interleavedBits.end());
    requireVectorEqual(result.bits, concatenated,
                       "multi-block output must be f_0 || f_1");
}

void testEndToEndWithStandardC1Reliability() {
    auto config = slb::control::makeStandardEncoderConfig();
    std::vector<slb::control::ControlBlockInput> blocks = {
        {alternatingBits(10), 64, 62, 0},
        {alternatingBits(18, 1), 64, 124, 2},
    };
    auto result = slb::control::encodeControlInfo(blocks, config);
    requireEqual(result.bits.size(), std::size_t{186}, "standard C.1 output length");
    requireEqual(result.blocks[0].parameters.motherCodeLength, std::size_t{64},
                 "standard C.1 first block N");
    requireEqual(result.blocks[1].parameters.nPc, std::size_t{3},
                 "standard C.1 second block nPC");
    requireVectorEqual(std::vector<std::size_t>(result.blocks[0].bitSelection.informationBits.begin(),
                                                result.blocks[0].bitSelection.informationBits.end()),
                       std::vector<std::size_t>({31, 47, 55, 57, 58, 59, 60, 61, 62, 63}),
                       "standard C.1 first block information positions");
    requireVectorEqual(result.blocks[1].pcBits,
                       std::vector<std::size_t>({27, 39, 56}),
                       "standard C.1 second block PC positions");
    requireEqual(slb::control::bitsToString(result.bits),
                 std::string("11100100001101101100110000100110100111101000101011011011110000"
                             "01110001101001011101010011101001100001001001111001100101010101"
                             "00000001011100000111011110110111111110101011101010000000001101"),
                 "standard C.1 multi-block golden output");
}

void testValidationFailures() {
    auto config = slb::control::makeEncoderConfig(slb::control::naturalReliabilitySequence(1024));
    requireThrows([&] {
        slb::control::encodeControlInfo({}, config);
    }, "empty block list must be rejected");
    requireThrows([&] {
        slb::control::encodeControlInfo({{{0, 2, 1}, 64, 62, 0}}, config);
    }, "non-bit input must be rejected");
    requireThrows([&] {
        slb::control::encodeControlInfo({{{0, 1, 1}, 64, 62, 4}}, config);
    }, "invalid rvid must be rejected");
    requireThrows([&] {
        slb::control::encodeControlInfo({{alternatingBits(10), 64,
                                          slb::control::kInterleaverCols *
                                                  slb::control::kInterleaverRowsMax +
                                              1,
                                          0}},
                                        config);
    }, "interleaver capacity overflow must be rejected");
    requireThrows([&] {
        auto badConfig = slb::control::makeEncoderConfig({0, 1, 2, 3});
        slb::control::encodeControlInfo({{alternatingBits(10), 64, 62, 0}}, badConfig);
    }, "insufficient reliability sequence must be rejected");
}

void testInterleaverCapacityBoundary() {
    const auto maxE = slb::control::kInterleaverCols * slb::control::kInterleaverRowsMax;
    std::vector<Bit> e(maxE);
    for (std::size_t i = 0; i < e.size(); ++i) {
        e[i] = static_cast<Bit>((i * 17 + 5) & 1U);
    }
    const auto map = slb::control::channelInterleaveIndexMap(
        maxE, slb::control::defaultInterleaverSequences());
    requireEqual(map.size(), maxE, "max-capacity interleaver map length");
    std::vector<std::size_t> sorted = map;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        requireEqual(sorted[i], i, "max-capacity interleaver map must be a permutation");
    }
    const auto f = slb::control::channelInterleave(
        e, slb::control::defaultInterleaverSequences());
    requireEqual(f.size(), maxE, "max-capacity interleaver output length");
}

void testDeterministicPropertySweep() {
    auto config = slb::control::makeEncoderConfig(slb::control::naturalReliabilitySequence(1024));
    std::mt19937 rng(20260522U);
    std::uniform_int_distribution<int> bitDist(0, 1);
    const std::vector<std::size_t> kValues = {1, 10, 17, 18, 21, 25, 26, 40};
    const std::vector<std::size_t> e0Values = {32, 64, 128, 256, 1024};
    const std::vector<std::size_t> eValues = {31, 62, 124, 255, 511};

    std::size_t scenarioCount = 0;
    std::size_t rejectedParameterCount = 0;
    for (std::size_t k : kValues) {
        for (std::size_t e0 : e0Values) {
            for (std::size_t e : eValues) {
                slb::control::ControlPolarParameters params;
                try {
                    params = slb::control::deriveControlPolarParameters(k, e0, e);
                } catch (const std::invalid_argument&) {
                    ++rejectedParameterCount;
                    continue;
                }
                std::vector<Bit> bits(k);
                for (auto& bit : bits) {
                    bit = static_cast<Bit>(bitDist(rng));
                }
                for (std::uint8_t rvid = 0; rvid <= 3; ++rvid) {
                    auto result = slb::control::encodeControlInfo({{bits, e0, e, rvid}}, config);
                    requireEqual(result.bits.size(), e, "property sweep output length");
                    requireEqual(result.blocks[0].polarInput.size(), params.motherCodeLength,
                                 "property sweep u length");
                    requireEqual(result.blocks[0].encodedBits.size(), params.motherCodeLength,
                                 "property sweep d length");
                    requireEqual(result.blocks[0].rateMatchedBits.size(), e,
                                 "property sweep e length");
                    requireEqual(result.blocks[0].interleavedBits.size(), e,
                                 "property sweep f length");
                    ++scenarioCount;
                }
            }
        }
    }
    requireEqual(scenarioCount, std::size_t{780}, "property sweep valid scenario count");
    requireEqual(rejectedParameterCount, std::size_t{5},
                 "property sweep rejected parameter count");
}

}  // namespace

int main() {
    const std::vector<TestCase> tests = {
        {"default interleaver sequences are permutations", testDefaultInterleaversArePermutations},
        {"mother code length and PC parameter selection", testMotherCodeLengthAndPcParameters},
        {"reliability sequence filtering and validation", testReliabilitySequenceForLength},
        {"standard C.1 reliability sequence integrity", testStandardReliabilitySequenceC1Integrity},
        {"Polar transform small vectors", testPolarTransformSmallVectors},
        {"Polar transform linearity", testPolarTransformLinearity},
        {"bit selection without PC using natural reliability", testBitSelectionNoPcNaturalReliability},
        {"bit selection formula branches", testBitSelectionFormulaBranches},
        {"shortening temporary frozen bits", testShorteningTemporaryFrozenBits},
        {"PC bit selection", testPcBitSelection},
        {"Polar input construction with PC bits", testPolarInputWithPcConsumesInformationBits},
        {"rate match plan and output", testRateMatchPlanAndOutput},
        {"rate match shortening plan", testRateMatchShorteningPlan},
        {"channel interleaver index map", testChannelInterleaveIndexMap},
        {"channel interleaver bits", testChannelInterleaveBits},
        {"end-to-end single block golden", testEndToEndSingleBlockGolden},
        {"end-to-end multi-block golden", testEndToEndMultiBlockGolden},
        {"end-to-end with standard C.1 reliability", testEndToEndWithStandardC1Reliability},
        {"validation failures", testValidationFailures},
        {"interleaver capacity boundary", testInterleaverCapacityBoundary},
        {"deterministic property sweep", testDeterministicPropertySweep},
    };

    std::size_t passed = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            ++passed;
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& ex) {
            std::cerr << "[FAIL] " << test.name << ": " << ex.what() << '\n';
            return 1;
        }
    }
    std::cout << "Executed " << passed << " tests successfully.\n";
    return 0;
}
