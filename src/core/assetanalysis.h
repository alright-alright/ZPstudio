#pragma once
// Reads the PNGs an assets.json names and estimates what they cost against the console's limits,
// so the UI can show "212 of 240 colours" before a build instead of an error after it.
// The authoritative check is still png2zp at build time (identical-tile folding, flips and
// quantisation are only approximated here), so numbers are labelled estimates.

#include "assetmanifest.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace zps {

// Limits from png2zp.md.
constexpr int kMaxColours = 240;      // opaque colours across the whole game
constexpr int kTilesPerBank = 256;
constexpr int kMaxCells = 256;        // the shared sprite cell table
constexpr int kMaxLayerPixels = 512;  // a layer image is at most 512x512
constexpr int kTileSize = 8;

struct EntryReport {
    AssetEntry entry;
    bool analysed = false;           // false: tiled map, text box, or the image could not be read
    int width = 0, height = 0;
    int colours = 0;                 // distinct opaque colours (RGB555)
    int tiles = 0;                   // distinct 8x8 tiles (blank counted)
    int cells = 0;                   // sprites: tiles in the sheet
    bool indexed = false;
    QStringList errors;              // will stop the build
    QStringList notes;               // informational
    bool ok() const { return errors.isEmpty(); }
};

struct Budget {
    QString label;
    int used = 0;
    int limit = 0;
    bool estimate = true;
    bool over() const { return used > limit; }
};

struct AssetReport {
    QList<EntryReport> entries;
    QList<Budget> budgets;           // colours, tile banks 0-3, sprite cells
    bool ok() const;
    int errorCount() const;
};

AssetReport analyseAssets(const AssetManifest& manifest);

} // namespace zps
