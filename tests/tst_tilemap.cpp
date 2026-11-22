#include "tilemapdoc.h"
#include "toolchain.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

using namespace zps;

namespace {

void writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// A 32x16 indexed sheet of 8 tiles, each a different flat colour.
void makeSheet(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage img(32, 16, QImage::Format_Indexed8);
    QList<QRgb> t;
    for (int i = 0; i < 8; ++i) t << qRgb(40 + i * 24, 20 + i * 8, 200 - i * 20);
    img.setColorTable(t);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 32; ++x) img.setPixel(x, y, (x / 8) + (y / 8) * 4);
    QVERIFY(img.save(path));
}

// A tiled map the way Tiled writes it: an object layer between two tile layers, plus properties and other keys.
const char* kTiledMap = R"({
  "backgroundcolor": "#112233",
  "compressionlevel": -1, "height": 2, "infinite": false,
  "layers": [
    {"id": 1, "name": "objs", "type": "objectgroup", "objects": [{"id": 1, "name": "spawn", "x": 8, "y": 8}], "opacity": 1, "visible": true, "x": 0, "y": 0},
    {"id": 2, "name": "ground", "type": "tilelayer", "width": 3, "height": 2, "data": [1,2,3,4,5,6], "opacity": 1, "visible": true, "x": 0, "y": 0,
     "properties": [{"name": "solid", "type": "bool", "value": true}]},
    {"id": 3, "name": "sky", "type": "tilelayer", "width": 3, "height": 2, "data": [0,0,0,0,0,0], "opacity": 1, "visible": true, "x": 0, "y": 0}
  ],
  "nextlayerid": 4, "nextobjectid": 2, "orientation": "orthogonal", "renderorder": "right-down",
  "properties": [{"name": "author", "type": "string", "value": "test"}],
  "tiledversion": "1.10.2", "tileheight": 8, "tilesets": [
    {"columns": 4, "firstgid": 1, "image": "sheet.png", "imageheight": 16, "imagewidth": 32, "margin": 0, "name": "sheet", "spacing": 0, "tilecount": 8, "tileheight": 8, "tilewidth": 8}
  ], "tilewidth": 8, "type": "map", "version": "1.10", "width": 3
})";

} // namespace

class TstTileMap : public QObject {
    Q_OBJECT
    QString py_, png2zp_, groundPng_;

    QString devtools() const
    {
        const QString env = qEnvironmentVariable("ZPSTUDIO_DEVTOOLS");
        return QDir::cleanPath(env.isEmpty() ? QDir(ZPSTUDIO_SOURCE_DIR).filePath("../ZPdevtools") : env);
    }

    // Runs png2zp on a manifest; returns its error text (empty = success).
    QString convert(const QString& manifest, const QString& outDir)
    {
        QProcess p;
        p.start(py_, {png2zp_, manifest, "-o", outDir, "--rom-base", "0x110000"});
        if (!p.waitForFinished(60000)) return "png2zp timed out";
        return p.exitCode() == 0 ? QString() : QString::fromUtf8(p.readAllStandardError() + p.readAllStandardOutput());
    }

    struct Cell { int tile; int attr; };
    // The 64x64 map of layer 0 out of a png2zp output folder.
    QVector<Cell> readMap0(const QString& outDir)
    {
        const QString header = QString::fromUtf8(readFile(outDir + "/assets.h"));
        const auto m = QRegularExpression(R"(ASSET_MAP0\s+0x([0-9A-Fa-f]+))").match(header);
        QVector<Cell> cells;
        if (!m.hasMatch()) return cells;
        const QByteArray bin = readFile(outDir + "/assets.bin");
        const int base = int(m.captured(1).toUInt(nullptr, 16)) - 0x110000;
        for (int i = 0; i < 64 * 64; ++i) {
            const auto* p = reinterpret_cast<const quint8*>(bin.constData()) + base + i * 4;
            cells.push_back({p[0] | p[1] << 8, p[2] | p[3] << 8});
        }
        return cells;
    }

private slots:
    void initTestCase()
    {
        py_ = Toolchain::findPython();
        png2zp_ = devtools() + "/scripts/png2zp.py";
        groundPng_ = QDir::cleanPath(devtools() + "/../ZPgameTemplate/assets/ground.png");
    }

    // ---- the file format -------------------------------------------------------------------------------------

    void emptyMapRoundTrips()
    {
        QTemporaryDir d;
        makeSheet(d.path() + "/sheet.png");
        TileMap m = TileMap::createEmpty(d.path() + "/level.tmj", "sheet.png", {32, 16}, 10, 6, "bg");
        QString err;
        QVERIFY2(m.save(&err), qPrintable(err));

        TileMap back;
        QVERIFY2(TileMap::load(d.path() + "/level.tmj", &back, &err), qPrintable(err));
        QCOMPARE(back.width(), 10);
        QCOMPARE(back.height(), 6);
        QCOMPARE(back.layers().size(), 1);
        QCOMPARE(back.layers()[0].name, QStringLiteral("bg"));
        QCOMPARE(back.layers()[0].data, QVector<quint32>(60, 0));
        QCOMPARE(back.tilesets().size(), 1);
        QCOMPARE(back.tilesets()[0].image, QStringLiteral("sheet.png"));
        QCOMPARE(back.tilesets()[0].columns, 4);
        QCOMPARE(back.tilesets()[0].tileCount, 8);
        QVERIFY(back.problems().isEmpty());
    }

    void sizeIsClampedToTheConsolesMap()
    {
        const TileMap m = TileMap::createEmpty("/x/a.tmj", "s.png", {32, 16}, 500, 0, "bg");
        QCOMPARE(m.width(), 64);
        QCOMPARE(m.height(), 1);
    }

    void flipBitsAreWrittenAsIntegers()
    {
        QTemporaryDir d;
        makeSheet(d.path() + "/sheet.png");
        TileMap m = TileMap::createEmpty(d.path() + "/l.tmj", "sheet.png", {32, 16}, 2, 1, "bg");
        m.setCell(0, 0, 0, 3 | kFlipH);                              // 2147483651
        m.setCell(0, 1, 0, 2 | kFlipH | kFlipV);
        QString err;
        QVERIFY(m.save(&err));
        const QByteArray text = readFile(d.path() + "/l.tmj");
        QVERIFY2(text.contains("2147483651"), "the flipped gid must be written as a plain integer");
        QVERIFY(!text.contains("e+"));
        TileMap back;
        QVERIFY(TileMap::load(d.path() + "/l.tmj", &back, &err));
        QCOMPARE(back.cell(0, 0, 0), quint32(3 | kFlipH));
        QCOMPARE(back.cell(0, 1, 0), quint32(2 | kFlipH | kFlipV));
    }

    void thingsStudioDoesNotModelSurviveASave()
    {
        QTemporaryDir d;
        makeSheet(d.path() + "/sheet.png");
        writeFile(d.path() + "/t.tmj", kTiledMap);
        TileMap m;
        QString err;
        QVERIFY2(TileMap::load(d.path() + "/t.tmj", &m, &err), qPrintable(err));
        QCOMPARE(m.layers().size(), 2);                              // the object layer is not a tile layer

        TileMapDocument doc(m);
        QVERIFY(doc.edit(0, {{0, 0, 7}}));
        QVERIFY(doc.removeLayer(1));                                 // drop "sky"
        doc.addLayer("top");
        QVERIFY2(doc.save(&err), qPrintable(err));

        const QJsonObject o = QJsonDocument::fromJson(readFile(d.path() + "/t.tmj")).object();
        QCOMPARE(o.value("backgroundcolor").toString(), QStringLiteral("#112233"));
        QCOMPARE(o.value("properties").toArray().size(), 1);
        const QJsonArray layers = o.value("layers").toArray();
        QCOMPARE(layers.size(), 3);
        QCOMPARE(layers[0].toObject().value("name").toString(), QStringLiteral("objs"));        // still first, untouched
        QCOMPARE(layers[0].toObject().value("objects").toArray().size(), 1);
        QCOMPARE(layers[1].toObject().value("name").toString(), QStringLiteral("ground"));
        QCOMPARE(layers[1].toObject().value("data").toArray()[0].toInt(), 7);                   // the edit
        QCOMPARE(layers[1].toObject().value("properties").toArray().size(), 1);                 // per-layer extras kept
        QCOMPARE(layers[2].toObject().value("name").toString(), QStringLiteral("top"));
        QVERIFY(layers[2].toObject().value("id").toInt() > 3);                                  // a fresh unique id
    }

    void base64DataIsReadAndCompressedDataExplained()
    {
        QTemporaryDir d;
        makeSheet(d.path() + "/sheet.png");
        QByteArray raw;
        for (quint32 g : {1u, 2u, 3u | kFlipV, 4u})
            for (int b = 0; b < 4; ++b) raw.append(char((g >> (8 * b)) & 0xFF));
        auto mapWith = [&](const QString& layerExtra) {
            return QStringLiteral(R"({"height":2,"width":2,"tilewidth":8,"tileheight":8,"orientation":"orthogonal","infinite":false,
                "tilesets":[{"firstgid":1,"image":"sheet.png","imagewidth":32,"imageheight":16,"tilewidth":8,"tileheight":8,"columns":4,"tilecount":8}],
                "layers":[{"type":"tilelayer","name":"l","width":2,"height":2,%1}]})").arg(layerExtra).toUtf8();
        };
        writeFile(d.path() + "/b64.tmj", mapWith(QString(R"("encoding":"base64","data":"%1")").arg(QString::fromLatin1(raw.toBase64()))));
        TileMap m;
        QString err;
        QVERIFY2(TileMap::load(d.path() + "/b64.tmj", &m, &err), qPrintable(err));
        QCOMPARE(m.cell(0, 0, 0), quint32(1));
        QCOMPARE(m.cell(0, 0, 1), quint32(3 | kFlipV));

        writeFile(d.path() + "/z.tmj", mapWith(R"("encoding":"base64","compression":"zlib","data":"eJwDAAAAAAE=")"));
        QVERIFY(!TileMap::load(d.path() + "/z.tmj", &m, &err));
        QVERIFY2(err.contains("CSV"), "the message should say how to fix it in Tiled");
    }

    void infiniteAndNonOrthogonalMapsAreRefused()
    {
        QTemporaryDir d;
        TileMap m;
        QString err;
        writeFile(d.path() + "/i.tmj", R"({"infinite":true,"width":1,"height":1,"layers":[]})");
        QVERIFY(!TileMap::load(d.path() + "/i.tmj", &m, &err));
        QVERIFY(err.contains("infinite"));
        writeFile(d.path() + "/o.tmj", R"({"orientation":"isometric","width":1,"height":1,"layers":[]})");
        QVERIFY(!TileMap::load(d.path() + "/o.tmj", &m, &err));
        writeFile(d.path() + "/junk.tmj", "not json");
        QVERIFY(!TileMap::load(d.path() + "/junk.tmj", &m, &err));
        QVERIFY(!TileMap::load(d.path() + "/missing.tmj", &m, &err));
    }

    void externalTilesetIsFoundRelativeToTheMap()
    {
        QTemporaryDir d;
        makeSheet(d.path() + "/art/sheet.png");
        writeFile(d.path() + "/art/set.tsj", R"({"name":"set","image":"sheet.png","imagewidth":32,"imageheight":16,
            "tilewidth":8,"tileheight":8,"columns":4,"tilecount":8,"margin":0,"spacing":0})");
        writeFile(d.path() + "/maps/m.tmj", R"({"height":1,"width":1,"tilewidth":8,"tileheight":8,"orientation":"orthogonal",
            "tilesets":[{"firstgid":1,"source":"../art/set.tsj"}],
            "layers":[{"type":"tilelayer","name":"l","width":1,"height":1,"data":[2]}]})");
        TileMap m;
        QString err;
        QVERIFY2(TileMap::load(d.path() + "/maps/m.tmj", &m, &err), qPrintable(err));
        QCOMPARE(m.tilesets()[0].image, QStringLiteral("../art/sheet.png"));     // relative to the map, not the .tsj
        QCOMPARE(m.tilesets()[0].columns, 4);
        QVERIFY(m.tilesets()[0].source.endsWith("set.tsj"));
    }

    void imageBecomesAnIdenticalLookingMap()
    {
        QTemporaryDir d;
        makeSheet(d.path() + "/sheet.png");
        QString err;
        const TileMap m = TileMap::createFromImage(d.path() + "/m.tmj", "sheet.png", {32, 16}, "bg", &err);
        QCOMPARE(m.width(), 4);
        QCOMPARE(m.height(), 2);
        for (int i = 0; i < 8; ++i) QCOMPARE(m.cell(0, i % 4, i / 4), quint32(i + 1));   // cell i shows tile i

        TileMap bad = TileMap::createFromImage(d.path() + "/x.tmj", "sheet.png", {30, 16}, "bg", &err);
        QVERIFY(err.contains("multiple of 8"));
        TileMap huge = TileMap::createFromImage(d.path() + "/x.tmj", "sheet.png", {520, 16}, "bg", &err);
        QVERIFY(!err.isEmpty());
    }

    void problemsMirrorWhatPng2zpRefuses()
    {
        QTemporaryDir d;
        makeSheet(d.path() + "/sheet.png");
        TileMap m = TileMap::createEmpty(d.path() + "/p.tmj", "sheet.png", {32, 16}, 4, 4, "bg");
        QVERIFY(m.problems().isEmpty());
        m.setCell(0, 0, 0, 2 | kFlipD);
        m.setCell(0, 1, 0, 99);                                       // beyond the tileset's 8 tiles
        const QString all = m.problems().join('\n');
        QVERIFY2(all.contains("rotated"), qPrintable(all));
        QVERIFY2(all.contains("no tileset"), qPrintable(all));
    }

    // ---- drawing operations ----------------------------------------------------------------------------------

    void stampIsClippedAtTheEdges()
    {
        const TileMap m = TileMap::createEmpty("/x/a.tmj", "s.png", {32, 16}, 4, 4, "bg");
        Brush b;
        b.w = 2; b.h = 2; b.gids = {1, 2, 3, 4};
        QCOMPARE(stampCells(m, b, 1, 1).size(), 4);
        const auto clipped = stampCells(m, b, 3, 3);                  // only the top-left tile is on the map
        QCOMPARE(clipped.size(), 1);
        QCOMPARE(clipped[0].gid, quint32(1));
        QCOMPARE(stampCells(m, b, -5, 0).size(), 0);
    }

    void flippingABrushMirrorsThePictureAndEachTile()
    {
        Brush b;
        b.w = 2; b.h = 1; b.gids = {5, 6};
        const Brush h = b.flippedH();
        QCOMPARE(h.gids[0], quint32(6 | kFlipH));
        QCOMPARE(h.gids[1], quint32(5 | kFlipH));
        QCOMPARE(h.flippedH().gids, b.gids);                          // twice is the original

        Brush v;
        v.w = 1; v.h = 2; v.gids = {5, 0};
        const Brush fv = v.flippedV();
        QCOMPARE(fv.gids[0], quint32(0));                             // an empty cell has no flip to toggle
        QCOMPARE(fv.gids[1], quint32(5 | kFlipV));
    }

    void linesAreContinuous()
    {
        const auto pts = lineCells({0, 0}, {9, 3});
        QCOMPARE(pts.first(), QPoint(0, 0));
        QCOMPARE(pts.last(), QPoint(9, 3));
        QCOMPARE(pts.size(), 10);
        for (int i = 1; i < pts.size(); ++i) {
            QVERIFY(qAbs(pts[i].x() - pts[i - 1].x()) <= 1);
            QVERIFY(qAbs(pts[i].y() - pts[i - 1].y()) <= 1);
        }
        QCOMPARE(lineCells({2, 2}, {2, 2}).size(), 1);
        QCOMPARE(lineCells({5, 0}, {0, 0}).size(), 6);                // either direction
    }

    void rectanglesRepeatTheBrush()
    {
        const TileMap m = TileMap::createEmpty("/x/a.tmj", "s.png", {32, 16}, 8, 4, "bg");
        Brush b;
        b.w = 2; b.h = 1; b.gids = {7, 8};
        const auto cells = rectCells(m, {4, 1}, {1, 2}, b);           // corners given backwards
        QCOMPARE(cells.size(), 8);
        for (const CellChange& c : cells) QCOMPARE(c.gid, quint32((c.x - 1) % 2 == 0 ? 7 : 8));
    }

    void fillStopsAtWallsAndDoesNothingWhenThereIsNothingToChange()
    {
        TileMap m = TileMap::createEmpty("/x/a.tmj", "s.png", {32, 16}, 5, 3, "bg");
        for (int y = 0; y < 3; ++y) m.setCell(0, 2, y, 9);            // a vertical wall splits the map in two
        const auto left = fillCells(m, 0, {0, 0}, Brush::single(1));
        QCOMPARE(left.size(), 6);                                     // 2 columns x 3 rows
        for (const CellChange& c : left) QVERIFY(c.x < 2);

        TileMapDocument doc(m);
        QVERIFY(doc.edit(0, left));
        QVERIFY(!doc.edit(0, fillCells(doc.map(), 0, {0, 0}, Brush::single(1))));   // already 1s everywhere there
    }

    // ---- the document ----------------------------------------------------------------------------------------

    void aDragIsOneUndoStep()
    {
        TileMapDocument doc(TileMap::createEmpty("/x/a.tmj", "s.png", {32, 16}, 8, 8, "bg"));
        QVERIFY(!doc.isModified());
        doc.beginStroke("Pencil");
        doc.edit(0, {{0, 0, 1}});
        doc.edit(0, {{1, 0, 1}});
        doc.edit(0, {{2, 0, 1}});
        doc.endStroke();
        QVERIFY(doc.isModified());
        QCOMPARE(doc.map().cell(0, 2, 0), quint32(1));

        doc.undoStack()->undo();                                       // the whole stroke
        for (int x = 0; x < 3; ++x) QCOMPARE(doc.map().cell(0, x, 0), quint32(0));
        QVERIFY(!doc.isModified());                                    // back to the saved state
        doc.undoStack()->redo();
        QCOMPARE(doc.map().cell(0, 1, 0), quint32(1));
    }

    void modifiedFollowsSavingAndTellsTheWindow()
    {
        QTemporaryDir d;
        makeSheet(d.path() + "/sheet.png");
        TileMapDocument doc(TileMap::createEmpty(d.path() + "/m.tmj", "sheet.png", {32, 16}, 4, 4, "bg"));
        QSignalSpy spy(&doc, &TileMapDocument::modifiedChanged);
        doc.edit(0, {{0, 0, 2}});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().at(0).toBool(), true);
        doc.edit(0, {{1, 0, 2}});
        QCOMPARE(spy.count(), 1);                                      // still modified: no second notice
        QString err;
        QVERIFY2(doc.save(&err), qPrintable(err));
        QVERIFY(!doc.isModified());
        QCOMPARE(spy.last().at(0).toBool(), false);
        doc.undoStack()->undo();                                       // behind the saved state counts as modified
        QVERIFY(doc.isModified());
    }

    void editingAnImpossibleCellOrLayerIsHarmless()
    {
        TileMapDocument doc(TileMap::createEmpty("/x/a.tmj", "s.png", {32, 16}, 4, 4, "bg"));
        QVERIFY(!doc.edit(5, {{0, 0, 1}}));
        QVERIFY(!doc.edit(0, {{9, 9, 1}}));
        QVERIFY(!doc.edit(0, {{0, 0, 0}}));                           // painting what is already there
        QVERIFY(!doc.isModified());
    }

    void layersCanBeAddedRemovedAndUndone()
    {
        TileMapDocument doc(TileMap::createEmpty("/x/a.tmj", "s.png", {32, 16}, 4, 4, "bg"));
        QVERIFY(!doc.removeLayer(0));                                 // a map keeps at least one layer
        const int top = doc.addLayer("bg");                           // the name is taken: it becomes "bg 2"
        QCOMPARE(top, 1);
        QCOMPARE(doc.map().layerName(1), QStringLiteral("bg 2"));
        doc.edit(1, {{1, 1, 3}});
        doc.undoStack()->undo();
        doc.undoStack()->undo();
        QCOMPARE(doc.map().layers().size(), 1);
        doc.undoStack()->redo();
        doc.undoStack()->redo();
        QCOMPARE(doc.map().cell(1, 1, 1), quint32(3));

        QVERIFY(doc.removeLayer(1));
        QCOMPARE(doc.map().layers().size(), 1);
        doc.undoStack()->undo();                                      // comes back with its tiles
        QCOMPARE(doc.map().cell(1, 1, 1), quint32(3));
    }

    void savingTwiceDoesNotDuplicateAddedLayers()
    {
        QTemporaryDir d;
        makeSheet(d.path() + "/sheet.png");
        TileMapDocument doc(TileMap::createEmpty(d.path() + "/m.tmj", "sheet.png", {32, 16}, 4, 4, "bg"));
        doc.addLayer("fg");
        QString err;
        QVERIFY(doc.save(&err));
        doc.edit(1, {{0, 0, 1}});
        QVERIFY(doc.save(&err));
        TileMap back;
        QVERIFY(TileMap::load(d.path() + "/m.tmj", &back, &err));
        QCOMPARE(back.layers().size(), 2);
        QCOMPARE(back.cell(1, 0, 0), quint32(1));
    }

    void visibilityIsSavedButNotAnUndoStep()
    {
        QTemporaryDir d;
        makeSheet(d.path() + "/sheet.png");
        TileMapDocument doc(TileMap::createEmpty(d.path() + "/m.tmj", "sheet.png", {32, 16}, 4, 4, "bg"));
        doc.setLayerVisible(0, false);
        QVERIFY(doc.isModified());
        QVERIFY(!doc.undoStack()->canUndo());
        QString err;
        QVERIFY(doc.save(&err));
        TileMap back;
        QVERIFY(TileMap::load(d.path() + "/m.tmj", &back, &err));
        QVERIFY(!back.layers()[0].visible);
    }

    // ---- against the real converter --------------------------------------------------------------------------

    void convertedMapIsByteIdenticalToTheImageItCameFrom()
    {
        if (py_.isEmpty() || !Toolchain::pythonHasPillow(py_) || !QFileInfo::exists(png2zp_) || !QFileInfo::exists(groundPng_))
            QSKIP("needs python3 + Pillow and ZPdevtools/ZPgameTemplate beside this repo");
        QTemporaryDir d;
        QVERIFY(QFile::copy(groundPng_, d.path() + "/ground.png"));
        writeFile(d.path() + "/image.json", R"({"bg0": {"image": "ground.png"}})");
        QVERIFY2(convert(d.path() + "/image.json", d.path() + "/out_image").isEmpty(), "png2zp on the image");

        QString err;
        TileMap m = TileMap::createFromImage(d.path() + "/ground.tmj", "ground.png", {512, 512}, "bg", &err);
        QVERIFY2(!m.layers().isEmpty(), qPrintable(err));
        QVERIFY2(m.save(&err), qPrintable(err));
        writeFile(d.path() + "/tiled.json", R"({"bg0": {"tiled": "ground.tmj", "layer": "bg"}})");
        const QString out = convert(d.path() + "/tiled.json", d.path() + "/out_tiled");
        QVERIFY2(out.isEmpty(), qPrintable(out));

        QCOMPARE(readFile(d.path() + "/out_tiled/assets.bin"), readFile(d.path() + "/out_image/assets.bin"));
        QCOMPARE(readFile(d.path() + "/out_tiled/assets.h"), readFile(d.path() + "/out_image/assets.h"));
    }

    void anEditMadeInStudioReachesTheBuiltMap()
    {
        if (py_.isEmpty() || !Toolchain::pythonHasPillow(py_) || !QFileInfo::exists(png2zp_) || !QFileInfo::exists(groundPng_))
            QSKIP("needs python3 + Pillow and ZPdevtools/ZPgameTemplate beside this repo");
        QTemporaryDir d;
        QVERIFY(QFile::copy(groundPng_, d.path() + "/ground.png"));
        QString err;
        TileMapDocument doc(TileMap::createFromImage(d.path() + "/ground.tmj", "ground.png", {512, 512}, "bg", &err));
        writeFile(d.path() + "/a.json", R"({"bg0": {"tiled": "ground.tmj", "layer": "bg"}})");
        QVERIFY2(doc.save(&err), qPrintable(err));
        QVERIFY2(convert(d.path() + "/a.json", d.path() + "/before").isEmpty(), "convert before");
        const auto before = readMap0(d.path() + "/before");
        QCOMPARE(before.size(), 64 * 64);

        // Pick two cells showing different tiles: sky near the top, soil near the bottom.
        const QPoint sky(3, 2), soil(10, 60);
        QVERIFY2(before[sky.y() * 64 + sky.x()].tile != before[soil.y() * 64 + soil.x()].tile, "need two different tiles");

        // Paint the sky cell with the soil tile, mirrored.
        const quint32 soilGid = doc.map().cell(0, soil.x(), soil.y());
        QVERIFY(doc.edit(0, {{sky.x(), sky.y(), soilGid | kFlipH}}));
        QVERIFY2(doc.save(&err), qPrintable(err));
        QVERIFY2(convert(d.path() + "/a.json", d.path() + "/after").isEmpty(), "convert after");
        const auto after = readMap0(d.path() + "/after");

        const Cell painted = after[sky.y() * 64 + sky.x()];
        const Cell source = after[soil.y() * 64 + soil.x()];
        QCOMPARE(painted.tile, source.tile);                           // shows the soil tile now...
        QVERIFY2(painted.attr & 0x08, "...mirrored horizontally");     // ...with the console's h-flip bit set
        QVERIFY(!(source.attr & 0x08));
        // Nothing else changed. png2zp numbers tiles by first use in row order, and the soil tile now first appears
        // in row 2, so the ids of later tiles shift: what must hold is that the tiles were only *relabelled*, i.e.
        // cells that shared a tile before still do, cells that differed still differ, and no flag moved.
        QMap<int, int> relabel, reverse;
        for (int i = 0; i < 64 * 64; ++i) {
            if (i == sky.y() * 64 + sky.x()) continue;
            QCOMPARE(after[i].attr, before[i].attr);
            const int from = before[i].tile, to = after[i].tile;
            QVERIFY2(!relabel.contains(from) || relabel[from] == to, qPrintable(QString("tile %1 split at cell %2").arg(from).arg(i)));
            QVERIFY2(!reverse.contains(to) || reverse[to] == from, qPrintable(QString("tiles merged into %1 at cell %2").arg(to).arg(i)));
            relabel[from] = to;
            reverse[to] = from;
        }
    }
};

QTEST_APPLESS_MAIN(TstTileMap)
#include "tst_tilemap.moc"
