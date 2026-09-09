// SPDX-License-Identifier: MIT
//
// The colour-blind palette. display_color() is a pure rewrite of a hex colour;
// these tests pin down that the default palette changes nothing, that the
// colour-blind one pushes greens towards blue and reds towards orange (so the
// two stop reading alike), that colours already clear of the red-green axis are
// left alone, and that the output is always a well-formed colour.
#include <gtest/gtest.h>

#include <cctype>
#include <string>

#include "nav/data.hpp"

using namespace nav;

namespace {

struct Rgb { int r, g, b; };

Rgb parse(const std::string& hex) {
    auto p = [&](int o) { return std::stoi(hex.substr(static_cast<std::size_t>(o), 2), nullptr, 16); };
    return {p(1), p(3), p(5)};
}

bool well_formed(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return false;
    for (std::size_t i = 1; i < 7; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

}  // namespace

TEST(Palette, TheDefaultPaletteChangesNothing) {
    for (const char* c : {"#6b8055", "#b05353", "#4d7fa8", "#9a9a9a", "#ffffff", "#000000"})
        EXPECT_EQ(display_color(c, Palette::Default), c);
}

TEST(Palette, MalformedInputComesBackUntouched) {
    EXPECT_EQ(display_color("", Palette::Colorblind), "");
    EXPECT_EQ(display_color(nullptr, Palette::Colorblind), "");
    EXPECT_EQ(display_color("red", Palette::Colorblind), "red");
    EXPECT_EQ(display_color("#12", Palette::Colorblind), "#12");
    EXPECT_EQ(display_color("#zzzzzz", Palette::Colorblind), "#zzzzzz");
}

TEST(Palette, GreensArePushedTowardsBlue) {
    for (const char* c : {"#6b8055", "#3d4a33", "#4a7d60", "#7ac77a", "#5a6b3a"}) {
        const std::string out = display_color(c, Palette::Colorblind);
        ASSERT_TRUE(well_formed(out)) << c << " -> " << out;
        const Rgb in = parse(c), got = parse(out);
        EXPECT_GT(got.b, in.b) << c << ": blue did not rise";
        EXPECT_GT(got.b, got.g) << c << ": still greener than it is blue";
        EXPECT_LT(got.r, in.r) << c << ": red was not pulled down";
    }
}

TEST(Palette, RedsArePushedTowardsOrange) {
    for (const char* c : {"#b05353", "#d9534f", "#c9736b"}) {
        const std::string out = display_color(c, Palette::Colorblind);
        ASSERT_TRUE(well_formed(out)) << c << " -> " << out;
        const Rgb in = parse(c), got = parse(out);
        EXPECT_GT(got.g, in.g) << c << ": green did not rise (towards orange)";
        EXPECT_LT(got.b, in.b) << c << ": blue was not pulled down";
        EXPECT_GE(got.r, got.g) << c << ": red should still lead";
    }
}

TEST(Palette, ColoursClearOfTheRedGreenAxisAreLeftAlone) {
    for (const char* c : {"#4d7fa8", "#9a9a9a", "#8e9aa8", "#c9b6e0", "#e8d8a0", "#ffffff"})
        EXPECT_EQ(display_color(c, Palette::Colorblind), c) << c << " was changed";
}

TEST(Palette, TheMireStopsLookingLikeTheScorch) {
    // The green mire water and the orange scorch river are the classic
    // red-green confusion. Under the colour-blind palette the mire turns blue
    // and the scorch stays warm — they separate on the blue channel.
    const Rgb mire = parse(display_color(zone_theme(Zone::Chernotop).liquid_color,
                                        Palette::Colorblind));
    const Rgb scorch = parse(display_color(zone_theme(Zone::Peklo).liquid_color,
                                           Palette::Colorblind));
    EXPECT_GT(mire.b, mire.g) << "the mire water is not bluer than it is green";
    EXPECT_GT(mire.b, scorch.b) << "the mire and the scorch did not separate on blue";
    EXPECT_GT(scorch.r, scorch.b) << "the scorch river stopped reading as warm";
}

TEST(Palette, TheRewriteIsDeterministic) {
    for (const char* c : {"#6b8055", "#b05353", "#4d7fa8"})
        EXPECT_EQ(display_color(c, Palette::Colorblind), display_color(c, Palette::Colorblind));
}
