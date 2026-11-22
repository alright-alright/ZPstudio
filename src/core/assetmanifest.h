#pragma once
// assets.json (the format png2zp reads; see ZPdevtools/docs/tools/png2zp.md) as something the UI can edit.
// The document is kept as raw JSON and only the keys Studio understands are touched, so a hand-edited
// manifest with options Studio does not know about (flips, fill, tiled layers...) survives a save.
// Note: QJsonObject writes keys in alphabetical order, so a save may reorder a hand-written file.

#include <QJsonObject>
#include <QList>
#include <QString>

namespace zps {

struct AssetEntry {
    enum class Role { Background0, Background1, Sprite, Font, Frame, TextBox };
    Role role = Role::Sprite;
    QString name;          // sprite / box name, or the role's label
    QString image;         // PNG path relative to the manifest; empty for tiled maps and text boxes
    QString tiled;         // Tiled map path (backgrounds only); such layers are not analysed
    int index = -1;        // position inside "sprites" / "boxes"
    int frameW = 0, frameH = 0;   // sprite frame size in tiles (0 = whole image)

    static QString roleLabel(Role r);
    QString label() const;                 // "Background 0", "Sprite hero", ...
    bool isSingleSlot() const { return role != Role::Sprite && role != Role::TextBox; }
};

class AssetManifest {
public:
    static bool load(const QString& path, AssetManifest* out, QString* error);
    bool save(QString* error) const;

    const QString& path() const { return path_; }
    QString dir() const;
    const QJsonObject& json() const { return json_; }

    QList<AssetEntry> entries() const;
    int quantize() const { return json_.value(QStringLiteral("quantize")).toInt(0); }

    // Edits (in memory; call save()). Backgrounds/font/frame hold one asset each: setting replaces it.
    void setBackground(AssetEntry::Role bg, const QString& imageRel);
    void addSprite(const QString& name, const QString& imageRel, int frameW = 0, int frameH = 0);
    void setFont(const QString& imageRel);
    void setFrame(const QString& imageRel);
    void setQuantize(int colours);                 // 0 removes it
    bool remove(const AssetEntry& e);
    QString uniqueSpriteName(const QString& base) const;

    static AssetManifest empty(const QString& path);

private:
    QString path_;
    QJsonObject json_;
};

} // namespace zps
