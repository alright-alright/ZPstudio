#include "assetanalysis.h"
#include "tilemap.h"

#include <QImage>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

using namespace zps;

namespace {

void writeText(const QString& path, const QByteArray& text)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
}

// An indexed PNG filled with `colours` distinct colours in 8x8 blocks, one colour per block.
void indexedPng(const QString& path, int w, int h, int colours)
{
    QImage img(w, h, QImage::Format_Indexed8);
    QList<QRgb> table;
    for (int i = 0; i < colours; ++i) table << qRgb((i * 8) & 0xF8, ((i * 8) >> 5 << 3) & 0xF8, 0);
    if (table.isEmpty()) table << qRgb(0, 0, 0);
    img.setColorTable(table);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) img.setPixel(x, y, ((x / 8) + (y / 8) * (w / 8)) % table.size());
    QVERIFY(img.save(path));
}

AssetManifest manifestIn(const QString& dir, const QByteArray& json)
{
    writeText(dir + "/assets.json", json);
    AssetManifest m;
    QString err;
    if (!AssetManifest::load(dir + "/assets.json", &m, &err)) qFatal("%s", qPrintable(err));
    return m;
}

} // namespace

class TstAssets : public QObject {
    Q_OBJECT
private slots:
    void entriesAreListed()
    {
        QTemporaryDir d;
        const auto m = manifestIn(d.path(), R"({
            "bg0": {"image": "ground.png"},
            "sprites": [{"name": "hero", "image": "hero.png", "frame_w": 2, "frame_h": 2}, {"name": "bird", "image": "bird.png"}],
            "font": {"image": "font.png", "first": 32},
            "frame": "frame.png",
            "boxes": [{"name": "msg", "w": 28, "h": 5}]
        })");
        const auto e = m.entries();
        QCOMPARE(e.size(), 6);
        QCOMPARE(e[0].role, AssetEntry::Role::Background0);
        QCOMPARE(e[1].name, QStringLiteral("hero"));
        QCOMPARE(e[1].frameW, 2);
        QCOMPARE(e[3].role, AssetEntry::Role::Font);
        QCOMPARE(e[4].image, QStringLiteral("frame.png"));     // "frame" is a bare string
        QCOMPARE(e[5].role, AssetEntry::Role::TextBox);
    }

    void saveKeepsKeysStudioDoesNotKnow()
    {
        QTemporaryDir d;
        auto m = manifestIn(d.path(), R"({"bg1": {"tiled": "level.tmj", "layer": "fg", "flips": true}, "rom_base": "0x110000", "future": [1,2]})");
        m.setBackground(AssetEntry::Role::Background0, "ground.png");
        m.setQuantize(32);
        QString err;
        QVERIFY(m.save(&err));

        AssetManifest back;
        QVERIFY(AssetManifest::load(d.path() + "/assets.json", &back, &err));
        QCOMPARE(back.json().value("rom_base").toString(), QStringLiteral("0x110000"));
        QCOMPARE(back.json().value("future").toArray().size(), 2);
        QVERIFY(back.json().value("bg1").toObject().value("flips").toBool());    // untouched layer
        QCOMPARE(back.json().value("bg0").toObject().value("image").toString(), QStringLiteral("ground.png"));
        QCOMPARE(back.quantize(), 32);
    }

    void replacingABackgroundKeepsItsOptionsButDropsTheTiledMap()
    {
        QTemporaryDir d;
        auto m = manifestIn(d.path(), R"({"bg1": {"tiled": "level.tmj", "layer": "fg", "flips": true}})");
        m.setBackground(AssetEntry::Role::Background1, "clouds.png");
        const QJsonObject o = m.json().value("bg1").toObject();
        QCOMPARE(o.value("image").toString(), QStringLiteral("clouds.png"));
        QVERIFY(!o.contains("tiled"));
        QVERIFY(!o.contains("layer"));
        QVERIFY(o.value("flips").toBool());
    }

    void spriteNamesAreCIdentifiersAndUnique()
    {
        QTemporaryDir d;
        auto m = manifestIn(d.path(), R"({"sprites": [{"name": "hero", "image": "a.png"}]})");
        QCOMPARE(m.uniqueSpriteName("hero"), QStringLiteral("hero_2"));
        QCOMPARE(m.uniqueSpriteName("my sprite-1"), QStringLiteral("my_sprite_1"));
        QCOMPARE(m.uniqueSpriteName("3d"), QStringLiteral("s3d"));
        QCOMPARE(m.uniqueSpriteName("Hero"), QStringLiteral("Hero_2"));       // names compare case-insensitively
    }

    void removeEntries()
    {
        QTemporaryDir d;
        auto m = manifestIn(d.path(), R"({"bg0": {"image": "g.png"}, "sprites": [{"name":"a","image":"a.png"},{"name":"b","image":"b.png"}]})");
        auto e = m.entries();
        QVERIFY(m.remove(e[1]));                                // sprite a
        QCOMPARE(m.entries().size(), 2);
        QCOMPARE(m.entries()[1].name, QStringLiteral("b"));
        QVERIFY(m.remove(m.entries()[1]));
        QVERIFY(!m.json().contains("sprites"));                 // an empty list is dropped, not left as []
        QVERIFY(m.remove(m.entries()[0]));
        QVERIFY(!m.json().contains("bg0"));
    }

    void countsColoursAndTiles()
    {
        QTemporaryDir d;
        indexedPng(d.path() + "/ground.png", 32, 16, 4);          // 8 tiles, 4 colours, tiles repeat
        const auto r = analyseAssets(manifestIn(d.path(), R"({"bg0": {"image": "ground.png"}})"));
        QVERIFY(r.ok());
        QCOMPARE(r.entries.size(), 1);
        QVERIFY(r.entries[0].analysed);
        QCOMPARE(r.entries[0].width, 32);
        QCOMPARE(r.entries[0].colours, 4);
        QCOMPARE(r.entries[0].tiles, 4);                          // 4 distinct block colours, repeated over 8 cells
        QCOMPARE(r.budgets[0].used, 4);                           // colours
        QCOMPARE(r.budgets[0].limit, 240);
        QCOMPARE(r.budgets[1].used, 4);                           // bank 0 tiles
    }

    void sharedColoursCountOnceAcrossImages()
    {
        QTemporaryDir d;
        indexedPng(d.path() + "/a.png", 24, 8, 3);                // three 8x8 blocks, one colour each
        indexedPng(d.path() + "/b.png", 24, 8, 3);                // same palette as a.png
        const auto r = analyseAssets(manifestIn(d.path(), R"({"bg0": {"image": "a.png"}, "bg1": {"image": "b.png"}})"));
        QCOMPARE(r.budgets[0].used, 3);
    }

    void spriteCellsAndBanks()
    {
        QTemporaryDir d;
        indexedPng(d.path() + "/hero.png", 16, 16, 2);            // 4 cells
        const auto r = analyseAssets(manifestIn(d.path(), R"({"sprites": [{"name": "hero", "image": "hero.png"}]})"));
        QCOMPARE(r.entries[0].cells, 4);
        QCOMPARE(r.budgets.last().label, QStringLiteral("Sprite cells"));
        QCOMPARE(r.budgets.last().used, 4);
        QCOMPARE(r.budgets[2].label, QStringLiteral("Tiles: sprites"));
        QVERIFY(r.budgets[2].used > 0);
    }

    void reportsErrorsBeforeABuildWould()
    {
        QTemporaryDir d;
        indexedPng(d.path() + "/odd.png", 10, 10, 2);             // not a multiple of 8
        QImage rgb(16, 16, QImage::Format_RGB32);
        rgb.fill(Qt::red);
        QVERIFY(rgb.save(d.path() + "/rgb.png"));
        const auto r = analyseAssets(manifestIn(d.path(),
            R"({"bg0": {"image": "odd.png"}, "bg1": {"image": "rgb.png"}, "sprites": [{"name":"x","image":"gone.png"}]})"));
        QVERIFY(!r.ok());
        QVERIFY(r.entries[0].errors.join(' ').contains("multiple of 8"));
        QVERIFY(r.entries[1].errors.join(' ').contains("indexed"));
        QVERIFY(r.entries[2].errors.join(' ').contains("not found"));

        // With automatic colour reduction on, the RGB image is acceptable.
        const auto q = analyseAssets(manifestIn(d.path(), R"({"quantize": 32, "bg1": {"image": "rgb.png"}})"));
        QVERIFY(q.entries[0].ok());
        QVERIFY(q.budgets[0].estimate);
    }

    void aTilemapIsCostedLikeThePictureItCameFrom()
    {
        QTemporaryDir d;
        indexedPng(d.path() + "/ground.png", 32, 16, 4);
        const auto fromImage = analyseAssets(manifestIn(d.path(), R"({"bg0": {"image": "ground.png"}})"));

        QString err;
        TileMap m = TileMap::createFromImage(d.path() + "/ground.tmj", "ground.png", {32, 16}, "bg", &err);
        QVERIFY2(m.save(&err), qPrintable(err));
        const auto fromMap = analyseAssets(manifestIn(d.path(), R"({"bg0": {"tiled": "ground.tmj", "layer": "bg"}})"));

        QVERIFY2(fromMap.ok(), qPrintable(fromMap.entries[0].errors.join(' ')));
        QVERIFY(fromMap.entries[0].analysed);
        QCOMPARE(fromMap.entries[0].width, 32);
        QCOMPARE(fromMap.entries[0].colours, fromImage.entries[0].colours);
        QCOMPARE(fromMap.entries[0].tiles, fromImage.entries[0].tiles);
        for (int i = 0; i < fromImage.budgets.size(); ++i)
            QCOMPARE(fromMap.budgets[i].used, fromImage.budgets[i].used);
    }

    void aTilemapCostsOnlyTheTilesItUses()
    {
        QTemporaryDir d;
        indexedPng(d.path() + "/sheet.png", 32, 16, 8);                  // 8 distinct tiles
        QString err;
        TileMap m = TileMap::createEmpty(d.path() + "/m.tmj", "sheet.png", {32, 16}, 4, 4, "bg");
        m.setCell(0, 0, 0, 1);
        m.setCell(0, 1, 0, 2);
        m.setCell(0, 2, 0, 2 | kFlipH);                                  // a flipped copy is not a new tile here
        QVERIFY(m.save(&err));
        const auto r = analyseAssets(manifestIn(d.path(), R"({"bg1": {"tiled": "m.tmj", "layer": "bg"}})"));
        QVERIFY2(r.ok(), qPrintable(r.entries[0].errors.join(' ')));
        QCOMPARE(r.entries[0].tiles, 3);                                 // tile 1, tile 2 and the blank one for empty cells
        QCOMPARE(r.budgets[3].label, QStringLiteral("Tiles: background 1"));
        QCOMPARE(r.budgets[3].used, 3);
        QCOMPARE(r.budgets[1].used, 0);
    }

    void aTilemapProblemsAreReportedBeforeABuild()
    {
        QTemporaryDir d;
        indexedPng(d.path() + "/sheet.png", 32, 16, 8);
        QString err;
        TileMap m = TileMap::createEmpty(d.path() + "/m.tmj", "sheet.png", {32, 16}, 4, 4, "bg");
        m.setCell(0, 0, 0, 2 | kFlipD);
        QVERIFY(m.save(&err));
        const auto rotated = analyseAssets(manifestIn(d.path(), R"({"bg0": {"tiled": "m.tmj", "layer": "bg"}})"));
        QVERIFY(!rotated.ok());
        QVERIFY(rotated.entries[0].errors.join(' ').contains("rotated"));

        const auto wrongLayer = analyseAssets(manifestIn(d.path(), R"({"bg0": {"tiled": "m.tmj", "layer": "nope"}})"));
        QVERIFY(wrongLayer.entries[0].errors.join(' ').contains("nope"));

        const auto missing = analyseAssets(manifestIn(d.path(), R"({"bg0": {"tiled": "gone.tmj"}})"));
        QVERIFY(!missing.ok());
    }

    void reductionCapsWhatAnRgbImageCosts()
    {
        QTemporaryDir d;
        QImage many(8 * 16, 8 * 16, QImage::Format_RGB32);
        for (int y = 0; y < many.height(); ++y)
            for (int x = 0; x < many.width(); ++x) many.setPixel(x, y, qRgb((x * 2) & 0xF8, (y * 2) & 0xF8, ((x + y) * 8) & 0xF8));
        QVERIFY(many.save(d.path() + "/many.png"));
        const auto r = analyseAssets(manifestIn(d.path(), R"({"quantize": 240, "bg0": {"image": "many.png"}})"));
        QVERIFY(r.budgets[0].used <= 240);                        // reduction caps the cost at `quantize`
    }
};

QTEST_MAIN(TstAssets)
#include "tst_assets.moc"
