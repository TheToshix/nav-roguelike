// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <vector>

#include "nav/geometry.hpp"
#include "nav/types.hpp"

namespace nav {

/// A dungeon level: a dense grid of tiles plus per-cell visibility bookkeeping.
///
/// The map owns no entities. Monsters and items live in Game and refer to the
/// map only through coordinates, which keeps this class trivially testable.
class Map {
public:
    Map() = default;
    Map(int width, int height) { resize(width, height); }

    void resize(int width, int height) {
        w_ = width;
        h_ = height;
        const std::size_t n = static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_);
        tiles_.assign(n, Tile::Wall);
        visible_.assign(n, 0);
        explored_.assign(n, 0);
    }

    int width() const { return w_; }
    int height() const { return h_; }

    bool in_bounds(Vec2 p) const { return p.x >= 0 && p.y >= 0 && p.x < w_ && p.y < h_; }

    Tile at(Vec2 p) const { return in_bounds(p) ? tiles_[idx(p)] : Tile::Wall; }
    void set(Vec2 p, Tile t) { if (in_bounds(p)) tiles_[idx(p)] = t; }

    /// Out-of-bounds counts as solid wall, so callers never need a bounds check
    /// before asking whether they can walk or see somewhere.
    bool walkable(Vec2 p) const { return in_bounds(p) && !blocks_move(tiles_[idx(p)]); }
    bool transparent(Vec2 p) const { return in_bounds(p) && !blocks_sight(tiles_[idx(p)]); }

    bool visible(Vec2 p) const { return in_bounds(p) && visible_[idx(p)] != 0; }
    bool explored(Vec2 p) const { return in_bounds(p) && explored_[idx(p)] != 0; }

    void set_visible(Vec2 p, bool v) {
        if (!in_bounds(p)) return;
        visible_[idx(p)] = v ? 1 : 0;
        if (v) explored_[idx(p)] = 1;
    }

    void clear_visible() { std::fill(visible_.begin(), visible_.end(), std::uint8_t{0}); }

    /// Reveals the whole level (the Magic Map scroll).
    void reveal_all() { std::fill(explored_.begin(), explored_.end(), std::uint8_t{1}); }

    /// Every walkable cell on the level, in scan order.
    std::vector<Vec2> walkable_cells() const {
        std::vector<Vec2> out;
        out.reserve(static_cast<std::size_t>(w_) * 4);
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x)
                if (walkable({x, y})) out.push_back({x, y});
        return out;
    }

    /// First cell holding `t`, or {-1,-1}.
    Vec2 find(Tile t) const {
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x)
                if (tiles_[idx({x, y})] == t) return {x, y};
        return {-1, -1};
    }

    // Raw access, used by the serializer and the tests.
    const std::vector<Tile>& raw_tiles() const { return tiles_; }
    std::vector<Tile>& raw_tiles() { return tiles_; }
    const std::vector<std::uint8_t>& raw_explored() const { return explored_; }
    std::vector<std::uint8_t>& raw_explored() { return explored_; }

private:
    std::size_t idx(Vec2 p) const {
        return static_cast<std::size_t>(p.y) * static_cast<std::size_t>(w_) +
               static_cast<std::size_t>(p.x);
    }

    int w_{0};
    int h_{0};
    std::vector<Tile> tiles_;
    std::vector<std::uint8_t> visible_;
    std::vector<std::uint8_t> explored_;
};

}  // namespace nav
