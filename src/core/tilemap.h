#pragma once
// A Tiled map (.tmj, the JSON export) as ZeroPoint uses it: 8x8 tiles, at most 64x64 cells, tile layers whose
// cells are Tiled global tile ids (gids) with the horizontal/vertical flip bits. png2zp reads exactly this
// (ZPdevtools/scripts/png2zp.py, load_tiled), so a map saved here builds, and opens in Tiled too.
//
// Everything Studio does not model (object layers, properties, other keys) is kept in the raw JSON and written
// back untouched, so a map that was made in Tiled survives a round trip through Studio.

#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

namespace zps {

constexpr quint32 kFlipH = 0x80000000u;     // Tiled's flip flags, in the high bits of a gid
constexpr quint32 kFlipV = 0x40000000u;
constexpr quint32 kFlipD = 0x20000000u;     // diagonal (rotation): the console cannot do it, png2zp refuses it
constexpr quint32 kHexRot = 0x10000000u;
constexpr quint32 kGidMask = 0x0FFFFFFFu;
constexpr int kMapCells = 64;               // the console's maps are 64x64 cells (512x512 pixels)
constexpr int kCellPixels = 8;

struct Tileset {
    int firstGid = 1;
    QString name;
    QString image;               // PNG path relative to the map file
    QString source;              // non-empty: an external .tsj (shown, never rewritten)
    int imageWidth = 0, imageHeight = 0;
    int columns = 0, tileCount = 0;
    int margin = 0, spacing = 0;
    int tileWidth = kCellPixels, tileHeight = kCellPixels;

    bool contains(quint32 gid) const { return gid >= quint32(firstGid) && gid < quint32(firstGid + tileCount); }
    // Where tile `local` (0-based) sits in the image.
    QRect tileRect(int local) const;
};

struct TileLayer {
    QString name;
    bool visible = true;
    QVector<quint32> data;       // width*height gids, row by row; 0 = empty
    int jsonIndex = -1;          // position in the file's "layers" (-1: added here)
};

class TileMap {
public:
    static bool load(const QString& path, TileMap* out, QString* error);
    // Not const: after a save the map's own copy of the file is brought up to date (layers added here now have a
    // place in it), so saving twice does not add them twice.
    bool save(QString* error);

    // A blank map: one tile layer of 0s over a tileset image.
    static TileMap createEmpty(const QString& path, const QString& imageRel, QSize imageSize, int width, int height,
                               const QString& layerName);
    // The picture as a map: the image is both the tile sheet and the map (cell i shows tile i), so it looks the
    // same and every cell can then be repainted with any tile of the sheet. The image must be 8-aligned and <= 512x512.
    static TileMap createFromImage(const QString& path, const QString& imageRel, QSize imageSize, const QString& layerName,
                                   QString* error);

    const QString& path() const { return path_; }
    QString dir() const;
    int width() const { return width_; }
    int height() const { return height_; }
    int cellCount() const { return width_ * height_; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

    const QList<Tileset>& tilesets() const { return tilesets_; }
    const QList<TileLayer>& layers() const { return layers_; }
    QList<TileLayer>& layers() { return layers_; }
    QString layerName(int i) const { return layers_.value(i).name; }
    int layerIndex(const QString& name) const;           // -1 if there is none
    QString uniqueLayerName(const QString& base) const;

    quint32 cell(int layer, int x, int y) const;
    void setCell(int layer, int x, int y, quint32 gid);

    const Tileset* tilesetFor(quint32 gid) const;        // gid may carry flip bits

    // What png2zp would reject about this map (empty = it will build): rotated tiles, tiles with no tileset, a
    // tile size other than 8x8, a size over 64x64.
    QStringList problems() const;

private:
    QString path_;
    QJsonObject json_;           // everything, as read: unmodelled keys ride along
    int width_ = 0, height_ = 0;
    int tileWidth_ = kCellPixels, tileHeight_ = kCellPixels;
    QList<Tileset> tilesets_;
    QList<TileLayer> layers_;
};

} // namespace zps
