#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace slb::control {

using Bit = std::uint8_t;

constexpr std::size_t kControlNMin = 5;
constexpr std::size_t kControlNMax = 10;
constexpr std::size_t kInterleaverCols = 62;
constexpr std::size_t kInterleaverRowsMax = 128;
constexpr std::size_t kInterleaverSequenceCount = 3;
constexpr std::size_t kInterleaverBankCount = 2;
constexpr std::size_t kInterleaverBankWidth = kInterleaverCols / kInterleaverBankCount;

using InterleaverSequences =
    std::array<std::array<std::size_t, kInterleaverCols>, kInterleaverSequenceCount>;

struct ControlBlockInput {
    std::vector<Bit> bits;
    std::size_t firstTransmissionLength = 0;  // E0 in T/XS 10001-2025 6.2.6.1.3.
    std::size_t transmissionLength = 0;       // E in T/XS 10001-2025 6.2.6.1.3.
    std::uint8_t redundancyVersion = 0;       // rvid, valid values: 0, 1, 2, 3.
};

struct EncoderConfig {
    // Polar reliability order, ascending reliability. For control information the
    // standard caps N at 1024, so a full production configuration must contain
    // all indexes 0..1023 exactly once. This PDF excerpt does not include
    // Appendix C, so the table is intentionally injected by the caller.
    std::vector<std::size_t> reliabilitySequence;
    InterleaverSequences interleaverSequences;
};

struct ControlPolarParameters {
    std::size_t nPc = 0;
    std::size_t nPcByWeight = 0;
    std::size_t kWithPc = 0;
    std::size_t n = 0;
    std::size_t motherCodeLength = 0;  // N = 2^n.
};

struct BitSelection {
    std::size_t t = 0;
    std::vector<std::size_t> temporaryFrozenBits;
    std::vector<std::size_t> informationBits;
    std::vector<std::size_t> frozenBits;
};

struct RateMatchPlan {
    std::size_t circularBufferLength = 0;  // M.
    std::size_t circularBufferStart = 0;   // k0.
    std::vector<std::size_t> sourceIndexes;
};

struct BlockDebugInfo {
    std::size_t blockIndex = 0;
    std::size_t k = 0;
    std::size_t firstTransmissionLength = 0;
    std::size_t transmissionLength = 0;
    std::uint8_t redundancyVersion = 0;
    ControlPolarParameters parameters;
    BitSelection bitSelection;
    std::vector<std::size_t> pcBits;
    RateMatchPlan rateMatchPlan;
    std::vector<Bit> polarInput;
    std::vector<Bit> encodedBits;
    std::vector<Bit> rateMatchedBits;
    std::vector<Bit> interleavedBits;
};

struct EncodeResult {
    std::vector<Bit> bits;
    std::vector<BlockDebugInfo> blocks;
};

InterleaverSequences defaultInterleaverSequences();

// A deterministic helper for tests and integration scaffolding. It is not a
// standards-compliant Appendix C replacement unless the caller explicitly
// chooses this order for a private profile.
std::vector<std::size_t> naturalReliabilitySequence(std::size_t maxLength);

EncoderConfig makeEncoderConfig(std::vector<std::size_t> reliabilitySequence);

std::vector<std::size_t> standardReliabilitySequence();

EncoderConfig makeStandardEncoderConfig();

ControlPolarParameters deriveControlPolarParameters(
    std::size_t informationBitCount,
    std::size_t firstTransmissionLength,
    std::size_t transmissionLength,
    std::size_t nmax = kControlNMax);

std::vector<std::size_t> reliabilitySequenceForLength(
    const std::vector<std::size_t>& reliabilitySequence,
    std::size_t motherCodeLength);

BitSelection selectBitPositions(
    const std::vector<std::size_t>& reliabilityForLength,
    std::size_t informationBitCount,
    std::size_t nPc,
    std::size_t firstTransmissionLength,
    std::size_t transmissionLength,
    std::size_t motherCodeLength);

std::vector<std::size_t> selectPcBits(
    const std::vector<std::size_t>& reliabilityForLength,
    const std::vector<std::size_t>& informationBits,
    std::size_t nPc,
    std::size_t nPcByWeight);

std::vector<Bit> buildPolarInput(
    const std::vector<Bit>& informationBits,
    std::size_t motherCodeLength,
    const std::vector<std::size_t>& informationPositions,
    const std::vector<std::size_t>& pcPositions);

std::vector<Bit> polarTransform(std::vector<Bit> input);

RateMatchPlan makeRateMatchPlan(
    std::size_t informationBitCount,
    std::size_t firstTransmissionLength,
    std::size_t motherCodeLength,
    std::size_t transmissionLength,
    std::uint8_t redundancyVersion);

std::vector<Bit> rateMatch(
    const std::vector<Bit>& encodedBits,
    const RateMatchPlan& plan);

std::vector<std::size_t> channelInterleaveIndexMap(
    std::size_t transmissionLength,
    const InterleaverSequences& interleaverSequences);

std::vector<Bit> channelInterleave(
    const std::vector<Bit>& rateMatchedBits,
    const InterleaverSequences& interleaverSequences);

EncodeResult encodeControlInfo(
    const std::vector<ControlBlockInput>& blocks,
    const EncoderConfig& config);

std::string bitsToString(const std::vector<Bit>& bits);

}  // namespace slb::control
