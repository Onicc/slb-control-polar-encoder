#include "slb/control_polar_encoder.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace slb::control {
namespace {

constexpr int kNullCell = -1;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

void validateBitVector(const std::vector<Bit>& bits, const std::string& name) {
    for (std::size_t i = 0; i < bits.size(); ++i) {
        require(bits[i] == 0U || bits[i] == 1U, name + " contains a non-bit value at index " +
                                                 std::to_string(i));
    }
}

std::size_t ceilDiv(std::size_t value, std::size_t divisor) {
    require(divisor != 0, "ceilDiv divisor must be non-zero");
    return value == 0 ? 0 : 1 + ((value - 1) / divisor);
}

std::int64_t ceilDivSigned(std::int64_t numerator, std::int64_t denominator) {
    require(denominator > 0, "ceilDivSigned denominator must be positive");
    if (numerator >= 0) {
        return (numerator + denominator - 1) / denominator;
    }
    return numerator / denominator;
}

bool lessOrEqualRatio(std::size_t lhsNumerator,
                      std::size_t lhsDenominator,
                      std::size_t rhsNumerator,
                      std::size_t rhsDenominator) {
    require(lhsDenominator != 0 && rhsDenominator != 0, "ratio denominator must be non-zero");
    return lhsNumerator * rhsDenominator <= rhsNumerator * lhsDenominator;
}

bool greaterRatio(std::size_t lhsNumerator,
                  std::size_t lhsDenominator,
                  std::size_t rhsNumerator,
                  std::size_t rhsDenominator) {
    require(lhsDenominator != 0 && rhsDenominator != 0, "ratio denominator must be non-zero");
    return lhsNumerator * rhsDenominator > rhsNumerator * lhsDenominator;
}

bool lessRatio(std::size_t lhsNumerator,
               std::size_t lhsDenominator,
               std::size_t rhsNumerator,
               std::size_t rhsDenominator) {
    require(lhsDenominator != 0 && rhsDenominator != 0, "ratio denominator must be non-zero");
    return lhsNumerator * rhsDenominator < rhsNumerator * lhsDenominator;
}

std::size_t powerOfTwo(std::size_t exponent) {
    require(exponent < std::numeric_limits<std::size_t>::digits,
            "powerOfTwo exponent exceeds size_t width");
    return static_cast<std::size_t>(1) << exponent;
}

std::size_t ceilLog2(std::size_t value) {
    require(value > 0, "ceilLog2 input must be positive");
    std::size_t exponent = 0;
    std::size_t current = 1;
    while (current < value) {
        require(current <= (std::numeric_limits<std::size_t>::max() >> 1),
                "ceilLog2 input is too large");
        current <<= 1U;
        ++exponent;
    }
    return exponent;
}

std::vector<bool> maskFromPositions(const std::vector<std::size_t>& positions,
                                    std::size_t length,
                                    const std::string& name) {
    std::vector<bool> mask(length, false);
    for (std::size_t pos : positions) {
        require(pos < length, name + " contains an index outside the current block length");
        require(!mask[pos], name + " contains duplicate index " + std::to_string(pos));
        mask[pos] = true;
    }
    return mask;
}

std::vector<std::size_t> positionsFromMask(const std::vector<bool>& mask, bool wanted) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        if (mask[i] == wanted) {
            out.push_back(i);
        }
    }
    return out;
}

std::vector<std::size_t> filterByMask(const std::vector<std::size_t>& ordered,
                                      const std::vector<bool>& excluded) {
    std::vector<std::size_t> out;
    out.reserve(ordered.size());
    for (std::size_t value : ordered) {
        require(value < excluded.size(), "ordered index exceeds mask length");
        if (!excluded[value]) {
            out.push_back(value);
        }
    }
    return out;
}

std::vector<std::size_t> takeMostReliable(const std::vector<std::size_t>& ascendingReliability,
                                          std::size_t count,
                                          const std::string& context) {
    require(count <= ascendingReliability.size(),
            context + " requested more reliable positions than are available");
    return std::vector<std::size_t>(ascendingReliability.end() -
                                        static_cast<std::ptrdiff_t>(count),
                                    ascendingReliability.end());
}

std::size_t computeT(std::size_t k,
                     std::size_t kWithPc,
                     std::size_t e0,
                     std::size_t motherCodeLength) {
    require(e0 > 0, "E0 must be positive");
    require(motherCodeLength > 0, "N must be positive");

    if (e0 >= motherCodeLength || greaterRatio(kWithPc, e0, 3, 4)) {
        return kWithPc;
    }

    std::int64_t numerator = 0;
    std::int64_t denominator = 1;
    const auto Kp = static_cast<std::int64_t>(kWithPc);
    const auto E0 = static_cast<std::int64_t>(e0);
    const auto N = static_cast<std::int64_t>(motherCodeLength);

    if (lessOrEqualRatio(k, e0, 7, 16)) {
        if (lessRatio(e0, motherCodeLength, 5, 8)) {
            numerator = Kp * (176 * E0 - 86 * N);
            denominator = 32 * N;
        } else if (lessRatio(e0, motherCodeLength, 3, 4)) {
            numerator = Kp * (40 * E0 - N);
            denominator = 32 * N;
        } else {
            numerator = Kp * (3 * E0 + 5 * N);
            denominator = 8 * N;
        }
    } else {
        if (lessRatio(e0, motherCodeLength, 9, 16)) {
            numerator = Kp * (9 * N - 2 * E0);
            denominator = 8 * N;
        } else {
            numerator = Kp * (31 * N + E0);
            denominator = 32 * N;
        }
    }

    const auto tSigned = ceilDivSigned(numerator, denominator);
    require(tSigned >= 0, "derived T is negative for the supplied parameters");
    const auto t = static_cast<std::size_t>(tSigned);
    require(t <= kWithPc, "derived T exceeds K+nPC for the supplied parameters");
    return t;
}

std::vector<std::size_t> temporaryFrozenBits(std::size_t k,
                                             std::size_t e0,
                                             std::size_t e,
                                             std::size_t motherCodeLength) {
    std::vector<bool> frozen(motherCodeLength, false);
    if (e0 < motherCodeLength) {
        if (lessOrEqualRatio(k, e0, 7, 16)) {
            std::int64_t numerator = 0;
            std::int64_t denominator = 1;
            if (motherCodeLength <= 256) {
                numerator = 3 * static_cast<std::int64_t>(motherCodeLength) -
                            2 * static_cast<std::int64_t>(e0);
                denominator = 4;
            } else {
                numerator = 7 * static_cast<std::int64_t>(motherCodeLength) -
                            6 * static_cast<std::int64_t>(e0);
                denominator = 8;
            }
            const auto limitSigned = ceilDivSigned(numerator, denominator);
            require(limitSigned >= 0, "temporary frozen prefix is negative");
            const auto limit = static_cast<std::size_t>(limitSigned);
            require(limit <= motherCodeLength, "temporary frozen prefix exceeds N");
            for (std::size_t i = 0; i < limit; ++i) {
                frozen[i] = true;
            }
        } else {
            for (std::size_t i = e0; i < motherCodeLength; ++i) {
                frozen[i] = true;
            }
            if (e > 128) {
                frozen[motherCodeLength / 4] = true;
                frozen[motherCodeLength / 2] = true;
            }
        }
    }
    return positionsFromMask(frozen, true);
}

std::size_t popcountSize(std::size_t value) {
    std::size_t count = 0;
    while (value != 0) {
        count += value & 1U;
        value >>= 1U;
    }
    return count;
}

void validateInterleaverSequences(const InterleaverSequences& sequences) {
    for (std::size_t row = 0; row < kInterleaverSequenceCount; ++row) {
        std::array<bool, kInterleaverCols> seen{};
        for (std::size_t col = 0; col < kInterleaverCols; ++col) {
            const auto value = sequences[row][col];
            require(value < kInterleaverCols, "interleaver sequence value is outside 0..61");
            require(!seen[value], "interleaver sequence contains a duplicate value");
            seen[value] = true;
        }
    }
}

}  // namespace

InterleaverSequences defaultInterleaverSequences() {
    return {{
        {{5, 39, 18, 47, 6, 45, 21, 50, 4, 40, 30, 55, 1, 46, 22, 54, 0, 42, 27, 61, 7,
          43, 29, 60, 12, 37, 20, 51, 14, 35, 25, 52, 3, 44, 16, 56, 10, 32, 23, 49, 13,
          41, 28, 58, 9, 31, 26, 53, 15, 38, 17, 48, 11, 36, 24, 57, 2, 33, 19, 59, 8,
          34}},
        {{5, 36, 21, 49, 4, 37, 20, 52, 14, 35, 27, 61, 2, 32, 28, 53, 7, 31, 17, 58, 3,
          38, 26, 60, 11, 43, 16, 51, 8, 45, 25, 56, 12, 34, 19, 47, 13, 41, 24, 54, 10,
          44, 22, 59, 1, 40, 23, 57, 0, 46, 18, 48, 6, 42, 29, 55, 15, 33, 30, 50, 9,
          39}},
        {{8, 36, 16, 52, 14, 35, 19, 51, 9, 45, 24, 58, 15, 33, 23, 59, 11, 38, 30, 48,
          12, 34, 29, 57, 6, 42, 20, 47, 4, 39, 21, 56, 13, 43, 25, 50, 1, 44, 18, 55,
          10, 41, 27, 53, 0, 32, 22, 54, 7, 31, 17, 49, 5, 37, 26, 60, 2, 46, 28, 61, 3,
          40}},
    }};
}

std::vector<std::size_t> naturalReliabilitySequence(std::size_t maxLength) {
    std::vector<std::size_t> sequence(maxLength);
    std::iota(sequence.begin(), sequence.end(), 0);
    return sequence;
}

EncoderConfig makeEncoderConfig(std::vector<std::size_t> reliabilitySequence) {
    return EncoderConfig{std::move(reliabilitySequence), defaultInterleaverSequences()};
}

ControlPolarParameters deriveControlPolarParameters(std::size_t informationBitCount,
                                                    std::size_t firstTransmissionLength,
                                                    std::size_t transmissionLength,
                                                    std::size_t nmax) {
    require(informationBitCount > 0, "control information block must contain at least one bit");
    require(firstTransmissionLength > 0, "E0 must be positive");
    require(transmissionLength > 0, "E must be positive");
    require(nmax >= kControlNMin, "nmax must be greater than or equal to nmin");

    ControlPolarParameters params;
    if (informationBitCount >= 18 && informationBitCount <= 25) {
        params.nPc = 3;
        params.nPcByWeight =
            (transmissionLength + 3 > informationBitCount + 192) ? 1 : 0;
    }
    params.kWithPc = informationBitCount + params.nPc;

    const auto n1 = ceilLog2(firstTransmissionLength);
    params.n = std::max(kControlNMin, std::min(n1, nmax));
    params.motherCodeLength = powerOfTwo(params.n);
    require(params.kWithPc <= params.motherCodeLength,
            "K+nPC exceeds the selected Polar mother code length");
    return params;
}

std::vector<std::size_t> reliabilitySequenceForLength(
    const std::vector<std::size_t>& reliabilitySequence,
    std::size_t motherCodeLength) {
    require(motherCodeLength > 0, "N must be positive");
    std::vector<bool> seen(motherCodeLength, false);
    std::vector<std::size_t> out;
    out.reserve(motherCodeLength);
    for (std::size_t value : reliabilitySequence) {
        if (value < motherCodeLength) {
            require(!seen[value], "reliability sequence contains duplicate index " +
                                      std::to_string(value));
            seen[value] = true;
            out.push_back(value);
        }
    }
    require(out.size() == motherCodeLength,
            "reliability sequence does not cover every index below N=" +
                std::to_string(motherCodeLength));
    return out;
}

BitSelection selectBitPositions(const std::vector<std::size_t>& reliabilityForLength,
                                std::size_t informationBitCount,
                                std::size_t nPc,
                                std::size_t firstTransmissionLength,
                                std::size_t transmissionLength,
                                std::size_t motherCodeLength) {
    require(reliabilityForLength.size() == motherCodeLength,
            "length-specific reliability sequence size must equal N");
    require(informationBitCount > 0, "K must be positive");
    require(firstTransmissionLength > 0, "E0 must be positive");
    require(informationBitCount + nPc <= motherCodeLength, "K+nPC exceeds N");

    BitSelection selection;
    const auto kWithPc = informationBitCount + nPc;
    selection.t = computeT(informationBitCount, kWithPc, firstTransmissionLength,
                           motherCodeLength);
    selection.temporaryFrozenBits = temporaryFrozenBits(informationBitCount,
                                                        firstTransmissionLength,
                                                        transmissionLength,
                                                        motherCodeLength);

    auto temporaryFrozenMask =
        maskFromPositions(selection.temporaryFrozenBits, motherCodeLength, "temporaryFrozenBits");
    auto qiTmp = filterByMask(reliabilityForLength, temporaryFrozenMask);

    auto qi1 = takeMostReliable(qiTmp, selection.t, "QI1 selection");
    auto excluded = maskFromPositions(qi1, motherCodeLength, "QI1");
    for (std::size_t i = 0; i < motherCodeLength / 2; ++i) {
        excluded[i] = true;
    }
    auto qi2Candidates = filterByMask(qiTmp, excluded);
    const auto qi2Count = kWithPc - selection.t;
    auto qi2 = takeMostReliable(qi2Candidates, qi2Count, "QI2 selection");

    auto infoMask = maskFromPositions(qi1, motherCodeLength, "QI1");
    for (std::size_t pos : qi2) {
        require(!infoMask[pos], "QI1/QI2 overlap detected");
        infoMask[pos] = true;
    }
    selection.informationBits = positionsFromMask(infoMask, true);
    selection.frozenBits = positionsFromMask(infoMask, false);
    require(selection.informationBits.size() == kWithPc,
            "information bit selection did not produce K+nPC positions");
    return selection;
}

std::vector<std::size_t> selectPcBits(const std::vector<std::size_t>& reliabilityForLength,
                                      const std::vector<std::size_t>& informationBits,
                                      std::size_t nPc,
                                      std::size_t nPcByWeight) {
    if (nPc == 0) {
        return {};
    }
    require(nPcByWeight <= nPc, "nPcByWeight cannot exceed nPc");
    require(informationBits.size() >= nPc, "not enough information positions for PC bits");

    const auto motherCodeLength = reliabilityForLength.size();
    auto infoMask = maskFromPositions(informationBits, motherCodeLength, "informationBits");
    std::vector<std::size_t> rank(motherCodeLength, 0);
    for (std::size_t i = 0; i < reliabilityForLength.size(); ++i) {
        rank[reliabilityForLength[i]] = i;
    }

    std::vector<std::size_t> qiByReliability;
    qiByReliability.reserve(informationBits.size());
    for (std::size_t pos : reliabilityForLength) {
        if (infoMask[pos]) {
            qiByReliability.push_back(pos);
        }
    }

    std::vector<std::size_t> pcBits;
    const auto lowReliabilityPcCount = nPc - nPcByWeight;
    for (std::size_t i = 0; i < lowReliabilityPcCount; ++i) {
        pcBits.push_back(qiByReliability[i]);
    }

    if (nPcByWeight > 0) {
        const auto qTildeCount = informationBits.size() - nPc;
        require(qTildeCount >= nPcByWeight, "not enough QI~ candidates for weighted PC bits");
        std::vector<std::size_t> qTilde(qiByReliability.end() -
                                            static_cast<std::ptrdiff_t>(qTildeCount),
                                        qiByReliability.end());
        std::sort(qTilde.begin(), qTilde.end(), [&](std::size_t lhs, std::size_t rhs) {
            const auto lhsWeight = popcountSize(lhs);
            const auto rhsWeight = popcountSize(rhs);
            if (lhsWeight != rhsWeight) {
                return lhsWeight < rhsWeight;
            }
            return rank[lhs] > rank[rhs];
        });
        for (std::size_t i = 0; i < nPcByWeight; ++i) {
            pcBits.push_back(qTilde[i]);
        }
    }

    std::sort(pcBits.begin(), pcBits.end());
    require(std::adjacent_find(pcBits.begin(), pcBits.end()) == pcBits.end(),
            "PC bit selection produced duplicate positions");
    return pcBits;
}

std::vector<Bit> buildPolarInput(const std::vector<Bit>& informationBits,
                                 std::size_t motherCodeLength,
                                 const std::vector<std::size_t>& informationPositions,
                                 const std::vector<std::size_t>& pcPositions) {
    validateBitVector(informationBits, "informationBits");
    auto infoMask = maskFromPositions(informationPositions, motherCodeLength,
                                      "informationPositions");
    auto pcMask = maskFromPositions(pcPositions, motherCodeLength, "pcPositions");
    for (std::size_t pcPos : pcPositions) {
        require(infoMask[pcPos], "PC position must also be an information position");
    }
    require(informationPositions.size() >= pcPositions.size(),
            "PC positions cannot outnumber information positions");
    require(informationPositions.size() - pcPositions.size() == informationBits.size(),
            "K does not match selected non-PC information positions");

    std::vector<Bit> u(motherCodeLength, 0);
    std::size_t k = 0;

    if (pcPositions.empty()) {
        for (std::size_t idx = 0; idx < motherCodeLength; ++idx) {
            if (infoMask[idx]) {
                u[idx] = informationBits[k++];
            }
        }
    } else {
        Bit y0 = 0;
        Bit y1 = 0;
        Bit y2 = 0;
        Bit y3 = 0;
        Bit y4 = 0;
        for (std::size_t idx = 0; idx < motherCodeLength; ++idx) {
            const Bit yt = y0;
            y0 = y1;
            y1 = y2;
            y2 = y3;
            y3 = y4;
            y4 = yt;

            if (!infoMask[idx]) {
                u[idx] = 0;
                continue;
            }
            if (pcMask[idx]) {
                u[idx] = y0;
            } else {
                u[idx] = informationBits[k++];
                y0 = static_cast<Bit>(y0 ^ u[idx]);
            }
        }
    }

    require(k == informationBits.size(), "not all input information bits were consumed");
    return u;
}

std::vector<Bit> polarTransform(std::vector<Bit> input) {
    validateBitVector(input, "polarTransform input");
    const auto length = input.size();
    require(length > 0, "polarTransform input cannot be empty");
    require((length & (length - 1U)) == 0U, "polarTransform length must be a power of two");

    for (std::size_t step = 1; step < length; step <<= 1U) {
        for (std::size_t base = 0; base < length; base += step << 1U) {
            for (std::size_t offset = 0; offset < step; ++offset) {
                input[base + offset] =
                    static_cast<Bit>(input[base + offset] ^ input[base + offset + step]);
            }
        }
    }
    return input;
}

RateMatchPlan makeRateMatchPlan(std::size_t informationBitCount,
                                std::size_t firstTransmissionLength,
                                std::size_t motherCodeLength,
                                std::size_t transmissionLength,
                                std::uint8_t redundancyVersion) {
    require(informationBitCount > 0, "K must be positive");
    require(firstTransmissionLength > 0, "E0 must be positive");
    require(motherCodeLength > 0, "N must be positive");
    require(transmissionLength > 0, "E must be positive");
    require(redundancyVersion <= 3, "rvid must be 0, 1, 2, or 3");

    RateMatchPlan plan;
    if (greaterRatio(informationBitCount, firstTransmissionLength, 7, 16)) {
        plan.circularBufferLength = std::min(firstTransmissionLength, motherCodeLength);
    } else {
        plan.circularBufferLength = motherCodeLength;
    }
    require(plan.circularBufferLength > 0, "circular buffer length M must be positive");

    switch (redundancyVersion) {
        case 0:
            plan.circularBufferStart = 0;
            break;
        case 1:
            plan.circularBufferStart = (plan.circularBufferLength / (4 * 32)) * 32;
            break;
        case 2:
            plan.circularBufferStart = (plan.circularBufferLength / (2 * 32)) * 32;
            break;
        case 3:
            plan.circularBufferStart = ((3 * plan.circularBufferLength) / (4 * 32)) * 32;
            break;
        default:
            break;
    }

    plan.sourceIndexes.reserve(transmissionLength);
    for (std::size_t k = 0; k < transmissionLength; ++k) {
        plan.sourceIndexes.push_back((plan.circularBufferStart + k) % plan.circularBufferLength);
    }
    return plan;
}

std::vector<Bit> rateMatch(const std::vector<Bit>& encodedBits, const RateMatchPlan& plan) {
    validateBitVector(encodedBits, "encodedBits");
    require(plan.circularBufferLength > 0, "rate match plan has zero circular buffer length");
    require(plan.circularBufferLength <= encodedBits.size(),
            "rate match circular buffer exceeds encoded bit length");

    std::vector<Bit> out;
    out.reserve(plan.sourceIndexes.size());
    for (std::size_t index : plan.sourceIndexes) {
        require(index < plan.circularBufferLength, "rate match plan source index exceeds M");
        out.push_back(encodedBits[index]);
    }
    return out;
}

std::vector<std::size_t> channelInterleaveIndexMap(
    std::size_t transmissionLength,
    const InterleaverSequences& interleaverSequences) {
    require(transmissionLength > 0, "E must be positive");
    require(transmissionLength <= kInterleaverCols * kInterleaverRowsMax,
            "E exceeds the interleaver capacity Col*Rowmax");
    validateInterleaverSequences(interleaverSequences);

    std::vector<int> buffer(kInterleaverCols * kInterleaverRowsMax, kNullCell);
    for (std::size_t i = 0; i < transmissionLength; ++i) {
        const auto rowId = i / kInterleaverCols;
        const auto columnId = i % kInterleaverCols;
        const auto sequenceId = rowId % kInterleaverSequenceCount;
        const auto interleavedColumn = interleaverSequences[sequenceId][columnId];
        buffer[rowId * kInterleaverCols + interleavedColumn] = static_cast<int>(i);
    }

    const auto usedRows = ceilDiv(transmissionLength, kInterleaverCols);
    std::vector<std::size_t> map;
    map.reserve(transmissionLength);
    for (std::size_t bank = 0; bank < kInterleaverBankCount; ++bank) {
        for (std::size_t row = 0; row < usedRows; ++row) {
            for (std::size_t offset = 0; offset < kInterleaverBankWidth; ++offset) {
                const auto bufferIndex =
                    (row * kInterleaverBankCount + bank) * kInterleaverBankWidth + offset;
                if (buffer[bufferIndex] != kNullCell) {
                    map.push_back(static_cast<std::size_t>(buffer[bufferIndex]));
                }
            }
        }
    }
    require(map.size() == transmissionLength, "interleaver output length mismatch");
    return map;
}

std::vector<Bit> channelInterleave(const std::vector<Bit>& rateMatchedBits,
                                   const InterleaverSequences& interleaverSequences) {
    validateBitVector(rateMatchedBits, "rateMatchedBits");
    const auto map = channelInterleaveIndexMap(rateMatchedBits.size(), interleaverSequences);
    std::vector<Bit> out;
    out.reserve(rateMatchedBits.size());
    for (std::size_t index : map) {
        out.push_back(rateMatchedBits[index]);
    }
    return out;
}

EncodeResult encodeControlInfo(const std::vector<ControlBlockInput>& blocks,
                               const EncoderConfig& config) {
    require(!blocks.empty(), "at least one control block is required");
    validateInterleaverSequences(config.interleaverSequences);

    EncodeResult result;
    for (std::size_t blockIndex = 0; blockIndex < blocks.size(); ++blockIndex) {
        const auto& block = blocks[blockIndex];
        validateBitVector(block.bits, "block[" + std::to_string(blockIndex) + "].bits");

        BlockDebugInfo debug;
        debug.blockIndex = blockIndex;
        debug.k = block.bits.size();
        debug.firstTransmissionLength = block.firstTransmissionLength;
        debug.transmissionLength = block.transmissionLength;
        debug.redundancyVersion = block.redundancyVersion;
        debug.parameters = deriveControlPolarParameters(block.bits.size(),
                                                        block.firstTransmissionLength,
                                                        block.transmissionLength);

        const auto reliabilityForLength =
            reliabilitySequenceForLength(config.reliabilitySequence,
                                         debug.parameters.motherCodeLength);
        debug.bitSelection = selectBitPositions(reliabilityForLength,
                                                block.bits.size(),
                                                debug.parameters.nPc,
                                                block.firstTransmissionLength,
                                                block.transmissionLength,
                                                debug.parameters.motherCodeLength);
        debug.pcBits = selectPcBits(reliabilityForLength,
                                    debug.bitSelection.informationBits,
                                    debug.parameters.nPc,
                                    debug.parameters.nPcByWeight);
        debug.polarInput = buildPolarInput(block.bits,
                                           debug.parameters.motherCodeLength,
                                           debug.bitSelection.informationBits,
                                           debug.pcBits);
        debug.encodedBits = polarTransform(debug.polarInput);
        debug.rateMatchPlan = makeRateMatchPlan(block.bits.size(),
                                                block.firstTransmissionLength,
                                                debug.parameters.motherCodeLength,
                                                block.transmissionLength,
                                                block.redundancyVersion);
        debug.rateMatchedBits = rateMatch(debug.encodedBits, debug.rateMatchPlan);
        debug.interleavedBits = channelInterleave(debug.rateMatchedBits,
                                                  config.interleaverSequences);
        result.bits.insert(result.bits.end(),
                           debug.interleavedBits.begin(),
                           debug.interleavedBits.end());
        result.blocks.push_back(std::move(debug));
    }
    return result;
}

std::string bitsToString(const std::vector<Bit>& bits) {
    validateBitVector(bits, "bits");
    std::string out;
    out.reserve(bits.size());
    for (Bit bit : bits) {
        out.push_back(bit ? '1' : '0');
    }
    return out;
}

}  // namespace slb::control
