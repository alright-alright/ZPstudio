#include "assetanalysis.h"

#include "tileatlas.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSet>

namespace zps {

bool AssetReport::ok() const { return errorCount() == 0; }

int AssetReport::errorCount() const
{
    int n = 0;
    for (const EntryReport& e : entries) n += e.errors.size();
    for (const Budget& b : budgets) n += b.over() ? 1 : 0;
    return n;
}

namespace {

// One opaque pixel's colour as the console sees it: RGB555, so colours differing only in the
// low 3 bits of a channel are the same colour (png2zp.md).
quint16 rgb555(QRgb p) { return quint16(((qRed(p) >> 3) << 10) | ((qGreen(p) >> 3) << 5) | (qBlue(p) >> 3)); }

struct ImageData {
    QSet<quint16> colours;
    QSet<QByteArray> tiles;
    int width = 0, height = 0;
    bool indexed = false;
    bool hasAlpha = false;
};

// transparentKey: "alpha" | "index0" | "#rrggbb" | "none" | "" (auto: alpha if the PNG has any)
ImageData scan(const QImage& img, const QString& transparent)
{
    ImageData d;
    d.width = img.width();
    d.height = img.height();
    d.indexed = img.format() == QImage::Format_Indexed8 || img.format() == QImage::Format_Mono ||
                img.format() == QImage::Format_MonoLSB;
    d.hasAlpha = img.hasAlphaChannel();
    const QImage argb = img.convertToFormat(QImage::Format_ARGB32);

    QRgb key = 0;
    const bool keyed = transparent.startsWith(QLatin1Char('#')) && transparent.size() == 7;
    if (keyed) key = QColor(transparent).rgb();
    const bool useAlpha = transparent == QLatin1String("alpha") || (transparent.isEmpty() && d.hasAlpha);
    const bool index0 = transparent == QLatin1String("index0") && d.indexed;

    auto transparentAt = [&](int x, int y) {
        const QRgb p = argb.pixel(x, y);
        if (useAlpha && qAlpha(p) == 0) return true;
        if (keyed && (p & 0x00FFFFFF) == (key & 0x00FFFFFF)) return true;
        if (index0 && img.pixelIndex(x, y) == 0) return true;
        return false;
    };

    for (int y = 0; y < d.height; ++y)
        for (int x = 0; x < d.width; ++x)
            if (!transparentAt(x, y)) d.colours.insert(rgb555(argb.pixel(x, y)));

    // Whole 8x8 blocks only; a ragged edge is reported as an error elsewhere.
    for (int ty = 0; ty + kTileSize <= d.height; ty += kTileSize)
        for (int tx = 0; tx + kTileSize <= d.width; tx += kTileSize) {
            QByteArray block;
            block.reserve(kTileSize * kTileSize * 2);
            for (int y = 0; y < kTileSize; ++y)
                for (int x = 0; x < kTileSize; ++x) {
                    const quint16 c = transparentAt(tx + x, ty + y) ? quint16(0xFFFF) : rgb555(argb.pixel(tx + x, ty + y));
                    block.append(char(c & 0xFF));
                    block.append(char(c >> 8));
                }
            d.tiles.insert(block);
        }
    return d;
}

// What a tilemap-backed background costs: the distinct tiles its layer uses and the colours of the sheets it draws
// from (png2zp adds a whole sheet's palette once any tile of it is used).
struct TiledScan {
    bool ok = false;
    QStringList errors;
    int width = 0, height = 0;                 // in cells
    QSet<QByteArray> tiles;
    QSet<quint16> colours;                     // every colour of the sheets in use
    QSet<quint16> indexedColours;              // the part of that from sheets that keep their own palette
    int quantizedSheetColours = 0, quantizedSheets = 0;   // sheets that will be reduced instead
    bool indexedAll = true;
};

QByteArray tileKey(const QImage& tile)
{
    QByteArray block;
    for (int y = 0; y < tile.height(); ++y)
        for (int x = 0; x < tile.width(); ++x) {
            const QRgb p = tile.pixel(x, y);
            const quint16 c = qAlpha(p) == 0 ? quint16(0xFFFF) : rgb555(p);
            block.append(char(c & 0xFF));
            block.append(char(c >> 8));
        }
    return block;
}

TiledScan scanTiled(const QString& path, const QString& layerName, const QString& transparent)
{
    TiledScan out;
    TileMap map;
    QString err;
    if (!TileMap::load(path, &map, &err)) { out.errors << err; return out; }
    const int li = layerName.isEmpty() ? (map.layers().isEmpty() ? -1 : 0) : map.layerIndex(layerName);
    if (li < 0) {
        out.errors << (layerName.isEmpty() ? QStringLiteral("The map has no tile layer.")
                                           : QStringLiteral("The map has no tile layer named \"%1\".").arg(layerName));
        return out;
    }
    out.errors << map.problems();
    out.width = map.width();
    out.height = map.height();

    const TileAtlas atlas(map);
    QSet<int> sheetsUsed;
    QSet<QByteArray> missingSheets;
    for (quint32 gid : map.layers()[li].data) {
        if (!(gid & kGidMask)) {                         // an empty cell becomes the blank tile
            out.tiles.insert(QByteArray(kTileSize * kTileSize * 2, char(0xFF)));
            continue;
        }
        int ts = -1;
        for (int i = 0; i < map.tilesets().size(); ++i)
            if (map.tilesets()[i].contains(gid & kGidMask)) { ts = i; break; }
        if (ts < 0) continue;                            // reported by problems()
        if (!atlas.sheetOk(ts)) {
            if (!missingSheets.contains(map.tilesets()[ts].image.toUtf8())) {
                missingSheets.insert(map.tilesets()[ts].image.toUtf8());
                out.errors << QStringLiteral("Cannot read the tile sheet %1.").arg(map.tilesets()[ts].image);
            }
            continue;
        }
        sheetsUsed.insert(ts);
        const QImage tile = atlas.tile(gid & kGidMask);   // flips do not make a new tile unless "flips" folding is off
        if (tile.isNull()) {
            out.errors << QStringLiteral("A tile lies outside its sheet %1.").arg(map.tilesets()[ts].image);
            break;
        }
        out.tiles.insert(tileKey(tile));
    }
    for (int ts : sheetsUsed) {
        const QImage& sheet = atlas.sheet(ts);
        const ImageData d = scan(sheet, transparent);
        const bool indexed = QImage(QDir(map.dir()).filePath(map.tilesets()[ts].image)).format() == QImage::Format_Indexed8;
        out.colours.unite(d.colours);
        if (indexed) out.indexedColours.unite(d.colours);
        else { out.indexedAll = false; out.quantizedSheetColours += int(d.colours.size()); ++out.quantizedSheets; }
    }
    out.ok = true;
    return out;
}

} // namespace

AssetReport analyseAssets(const AssetManifest& manifest)
{
    AssetReport report;
    const QDir dir(manifest.dir());
    const int quantize = manifest.quantize();

    QSet<quint16> indexedColours;     // exact union over images that keep their own palette
    int quantizedColours = 0;         // reduced images: each can cost up to `quantize` entries
    QSet<QByteArray> bankTiles[4];
    int cells = 0;

    for (const AssetEntry& e : manifest.entries()) {
        EntryReport r;
        r.entry = e;

        if (e.role == AssetEntry::Role::TextBox) { report.entries << r; continue; }
        if (e.image.isEmpty()) {
            if (!e.tiled.isEmpty()) {
                const bool bg1 = e.role == AssetEntry::Role::Background1;
                const QJsonObject spec = manifest.json().value(QString::fromLatin1(bg1 ? "bg1" : "bg0")).toObject();
                const TiledScan t = scanTiled(dir.filePath(e.tiled), spec.value(QStringLiteral("layer")).toString(),
                                              spec.value(QStringLiteral("transparent")).toString());
                r.errors << t.errors;
                if (t.ok) {
                    r.analysed = true;
                    r.width = t.width * kTileSize;
                    r.height = t.height * kTileSize;
                    r.tiles = int(t.tiles.size());
                    r.colours = int(t.colours.size());
                    indexedColours.unite(t.indexedColours);
                    quantizedColours += quantize > 0 ? qMin(t.quantizedSheetColours, quantize * t.quantizedSheets) : 0;
                    bankTiles[bg1 ? 2 : 0].unite(t.tiles);
                    if (r.colours > kMaxColours)
                        r.errors << QStringLiteral("%1 colours in the tile sheets; the whole game has %2.").arg(r.colours).arg(kMaxColours);
                    if (!t.indexedAll && quantize == 0)
                        r.errors << QStringLiteral("A tile sheet is not an indexed (palette) PNG. Convert it, or turn on automatic colour reduction.");
                }
            } else {
                r.errors << QStringLiteral("No image set.");
            }
            report.entries << r;
            continue;
        }

        const QString file = dir.filePath(e.image);
        if (!QFileInfo::exists(file)) {
            r.errors << QStringLiteral("File not found: %1").arg(e.image);
            report.entries << r;
            continue;
        }
        QImage img(file);
        if (img.isNull()) {
            r.errors << QStringLiteral("Cannot read %1 as an image.").arg(e.image);
            report.entries << r;
            continue;
        }

        // Look up an optional per-image "transparent" setting (backgrounds only carry one here).
        QString transparent;
        if (e.role == AssetEntry::Role::Background0)
            transparent = manifest.json().value(QStringLiteral("bg0")).toObject().value(QStringLiteral("transparent")).toString();
        else if (e.role == AssetEntry::Role::Background1)
            transparent = manifest.json().value(QStringLiteral("bg1")).toObject().value(QStringLiteral("transparent")).toString();

        const ImageData d = scan(img, transparent);
        r.analysed = true;
        r.width = d.width;
        r.height = d.height;
        r.indexed = d.indexed;
        r.colours = int(d.colours.size());
        r.tiles = int(d.tiles.size());

        if (d.width % kTileSize || d.height % kTileSize)
            r.errors << QStringLiteral("Size %1x%2 is not a multiple of 8 (tiles are 8x8).").arg(d.width).arg(d.height);
        const bool isLayer = e.role == AssetEntry::Role::Background0 || e.role == AssetEntry::Role::Background1;
        if (isLayer && (d.width > kMaxLayerPixels || d.height > kMaxLayerPixels))
            r.errors << QStringLiteral("A background is at most %1x%1 pixels.").arg(kMaxLayerPixels);
        if (!d.indexed && quantize == 0)
            r.errors << QStringLiteral("Not an indexed (palette) PNG. Convert it, or turn on automatic colour reduction.");
        if (r.colours > kMaxColours)
            r.errors << QStringLiteral("%1 colours in one image; the whole game has %2.").arg(r.colours).arg(kMaxColours);
        if (e.role == AssetEntry::Role::Frame && (d.width != kTileSize || d.height != kTileSize))
            r.errors << QStringLiteral("The box border is a single 8x8 tile.");

        if (d.indexed || quantize == 0) indexedColours.unite(d.colours);
        else quantizedColours += qMin(r.colours, quantize);

        int bank = 3;
        switch (e.role) {
        case AssetEntry::Role::Background0: bank = 0; break;
        case AssetEntry::Role::Sprite:      bank = 1; break;
        case AssetEntry::Role::Background1: bank = 2; break;
        default: bank = 3; break;
        }
        bankTiles[bank].unite(d.tiles);
        if (e.role == AssetEntry::Role::Sprite) {
            r.cells = (d.width / kTileSize) * (d.height / kTileSize);
            cells += r.cells;
        }
        report.entries << r;
    }

    Budget colours{QStringLiteral("Colours"), int(indexedColours.size()) + quantizedColours, kMaxColours, quantizedColours > 0};
    report.budgets << colours;
    static const char* bankNames[4] = {"Tiles: background 0", "Tiles: sprites", "Tiles: background 1", "Tiles: text/window"};
    for (int b = 0; b < 4; ++b)
        report.budgets << Budget{QString::fromLatin1(bankNames[b]), int(bankTiles[b].size()), kTilesPerBank, true};
    report.budgets << Budget{QStringLiteral("Sprite cells"), cells, kMaxCells, false};
    return report;
}

} // namespace zps
