#pragma once
// The pictures behind a map's gids: loads each tileset's PNG and hands out single 8x8 tiles, flips applied.
// Used by the editor to draw and by the budget analysis to count what a map costs.

#include "tilemap.h"

#include <QHash>
#include <QImage>

namespace zps {

class TileAtlas {
public:
    TileAtlas() = default;
    explicit TileAtlas(const TileMap& map) { load(map); }

    void load(const TileMap& map);                  // reads each tileset image (paths are relative to the map)
    int tilesetCount() const { return sheets_.size(); }
    const QImage& sheet(int tilesetIndex) const;    // null if it could not be read
    bool sheetOk(int tilesetIndex) const { return !sheet(tilesetIndex).isNull(); }

    // One tile as an 8x8 ARGB image with the gid's flip bits applied. Null for an empty cell, a gid that belongs
    // to no tileset, or a tile outside its image. (Rotation is not applied: the console has none.)
    QImage tile(quint32 gid) const;

private:
    QList<Tileset> tilesets_;
    QList<QImage> sheets_;
    mutable QHash<quint32, QImage> cache_;
    static const QImage kNull;
};

} // namespace zps
