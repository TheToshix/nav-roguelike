// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace nav {

/// Deterministic pseudo-random generator (xoshiro256** seeded via splitmix64).
///
/// The standard library is deliberately avoided here: `std::mt19937` is
/// portable but `std::uniform_int_distribution` is *not* — its output differs
/// between libstdc++, libc++ and MSVC. Reproducible runs across the native and
/// WebAssembly builds are a hard requirement (a bug report must be replayable
/// from its seed), so every random draw goes through this class.
class Rng {
public:
    explicit Rng(std::uint64_t seed = 0x9E3779B97F4A7C15ULL) { reseed(seed); }

    void reseed(std::uint64_t seed) {
        // splitmix64 expands a single 64-bit seed into the 256-bit state.
        for (auto& word : s_) {
            seed += 0x9E3779B97F4A7C15ULL;
            std::uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            word = z ^ (z >> 31);
        }
    }

    std::uint64_t next() {
        const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const std::uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    /// Uniform integer in [lo, hi]. Returns `lo` when the range is inverted so
    /// that a caller passing an empty range cannot produce undefined behaviour.
    int range(int lo, int hi) {
        if (hi <= lo) return lo;
        const std::uint64_t span = static_cast<std::uint64_t>(hi - lo) + 1;
        return lo + static_cast<int>(bounded(span));
    }

    /// Uniform integer in [0, n). Returns 0 for n <= 0.
    int below(int n) { return n <= 0 ? 0 : static_cast<int>(bounded(static_cast<std::uint64_t>(n))); }

    /// True with probability `percent`/100.
    bool chance(int percent) {
        if (percent <= 0) return false;
        if (percent >= 100) return true;
        return below(100) < percent;
    }

    /// Rolls `count` dice of `sides` faces and adds `bonus`.
    int dice(int count, int sides, int bonus = 0) {
        int total = bonus;
        for (int i = 0; i < count; ++i) total += range(1, sides);
        return total;
    }

    double unit() {  // [0, 1)
        return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
    }

    template <typename T>
    const T& pick(const std::vector<T>& v) { return v[static_cast<std::size_t>(below(static_cast<int>(v.size())))]; }

    template <typename T>
    void shuffle(std::vector<T>& v) {
        for (std::size_t i = v.size(); i > 1; --i) {
            const std::size_t j = static_cast<std::size_t>(below(static_cast<int>(i)));
            std::swap(v[i - 1], v[j]);
        }
    }

    /// Picks an index proportional to its weight. Returns -1 if all weights <= 0.
    int weighted(const std::vector<int>& weights) {
        int total = 0;
        for (int w : weights) if (w > 0) total += w;
        if (total <= 0) return -1;
        int roll = below(total);
        for (std::size_t i = 0; i < weights.size(); ++i) {
            if (weights[i] <= 0) continue;
            roll -= weights[i];
            if (roll < 0) return static_cast<int>(i);
        }
        return static_cast<int>(weights.size()) - 1;
    }

    /// Full generator state, so a saved game resumes the exact same sequence.
    const std::uint64_t* state() const { return s_; }
    void set_state(const std::uint64_t st[4]) { for (int i = 0; i < 4; ++i) s_[i] = st[i]; }

    /// Turns a human-typed seed ("кощей", "12345") into a 64-bit value.
    static std::uint64_t hash_seed(const std::string& text) {
        std::uint64_t h = 1469598103934665603ULL;  // FNV-1a offset basis
        for (unsigned char c : text) { h ^= c; h *= 1099511628211ULL; }
        return h ? h : 1ULL;
    }

private:
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    /// Rejection sampling: keeps the distribution exactly uniform, unlike `% n`.
    std::uint64_t bounded(std::uint64_t n) {
        const std::uint64_t limit = UINT64_MAX - (UINT64_MAX % n) - 1;
        std::uint64_t r;
        do { r = next(); } while (r > limit);
        return r % n;
    }

    std::uint64_t s_[4]{};
};

}  // namespace nav
