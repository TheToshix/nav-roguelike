// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace nav {

enum class Lang { Ru, En };

/// A pair of strings the game can show in either language.
///
/// Every user-visible string in the engine is a Text, so the language can be
/// switched at any moment — including for messages already in the log, which
/// keep both variants rather than a single rendered string.
struct Text {
    std::string ru;
    std::string en;

    Text() = default;
    Text(std::string r, std::string e) : ru(std::move(r)), en(std::move(e)) {}
    /// Single-argument form for strings that are identical in both languages
    /// (numbers, symbols, proper nouns).
    Text(const char* both) : ru(both), en(both) {}
    explicit Text(std::string both) : ru(both), en(std::move(both)) {}

    const std::string& get(Lang l) const { return l == Lang::Ru ? ru : en; }
    bool empty() const { return ru.empty() && en.empty(); }

    Text& operator+=(const Text& o) { ru += o.ru; en += o.en; return *this; }
    friend Text operator+(Text a, const Text& b) { a += b; return a; }
};

/// Substitutes "{}" placeholders left to right, independently per language.
/// Extra arguments are ignored; extra placeholders are left in place.
inline Text format(const Text& pattern, const std::vector<Text>& args) {
    auto fill = [&](const std::string& src, Lang lang) {
        std::string out;
        out.reserve(src.size() + 16 * args.size());
        std::size_t arg = 0, i = 0;
        while (i < src.size()) {
            if (src[i] == '{' && i + 1 < src.size() && src[i + 1] == '}' && arg < args.size()) {
                out += args[arg++].get(lang);
                i += 2;
            } else {
                out += src[i++];
            }
        }
        return out;
    };
    return Text{fill(pattern.ru, Lang::Ru), fill(pattern.en, Lang::En)};
}

inline Text format(const Text& pattern, const Text& a) { return format(pattern, std::vector<Text>{a}); }
inline Text format(const Text& pattern, const Text& a, const Text& b) {
    return format(pattern, std::vector<Text>{a, b});
}
inline Text format(const Text& pattern, const Text& a, const Text& b, const Text& c) {
    return format(pattern, std::vector<Text>{a, b, c});
}

/// Wraps a number as a Text (identical in both languages).
inline Text num(long long n) { return Text(std::to_string(n)); }

}  // namespace nav
