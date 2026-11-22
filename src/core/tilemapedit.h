#pragma once
// The drawing operations of the tile map editor as pure functions: they work out which cells would change and
// to what; applying them (with undo) is TileMapDocument's job.

#include "tilemap.h"

#include <QPoint>
#include <QVector>

namespace zps {

struct CellChange {
    int x = 0, y = 0;
    quint32 gid = 0;             // the new value (flip bits included)
};

// A block of tiles to paint with: one tile, or a rectangle picked from the tileset. 0 in `gids` is "empty".
struct Brush {
    int w = 1, h = 1;
    QVector<quint32> gids = QVector<quint32>(1, 0);

    static Brush single(quint32 gid) { Brush b; b.gids[0] = gid; return b; }
    // Wraps in both directions (negative offsets too), so a brush tiles when it is used to fill.
    quint32 at(int bx, int by) const { return gids[(((by % h) + h) % h) * w + (((bx % w) + w) % w)]; }
    // Mirrored as a picture: the tiles swap places and each one's own flip bit toggles.
    Brush flippedH() const;
    Brush flippedV() const;
};

// The brush stamped with its top-left corner on (x, y); cells outside the map are skipped.
QVector<CellChange> stampCells(const TileMap& map, const Brush& brush, int x, int y);

// The cells on a line from a to b inclusive (Bresenham): a fast drag still paints a continuous stroke.
QVector<QPoint> lineCells(QPoint a, QPoint b);

// Every cell of the rectangle spanning the two corners (any order), filled with the brush as a repeating pattern
// anchored at the rectangle's top-left.
QVector<CellChange> rectCells(const TileMap& map, QPoint a, QPoint b, const Brush& brush);

// Flood fill: the connected cells (4-way) of the same gid as the one at `at`, painted with the brush as a repeating
// pattern anchored at `at`. Nothing changes if the brush would paint what is already there.
QVector<CellChange> fillCells(const TileMap& map, int layer, QPoint at, const Brush& brush);

} // namespace zps
