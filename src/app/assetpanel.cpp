#include "assetpanel.h"

#include "assetanalysis.h"
#include "newmapdialog.h"
#include "tilemap.h"
#include "theme.h"

#include <QCheckBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

using namespace zps;

namespace {

const QStringList kKinds{QStringLiteral("Sprite"), QStringLiteral("Background 0 (back layer)"),
                         QStringLiteral("Background 1 (front layer)"), QStringLiteral("Font"),
                         QStringLiteral("Box border (one 8x8 tile)")};

QIcon thumbnail(const QString& file)
{
    QImage img(file);
    if (img.isNull()) return {};
    // Nearest-neighbour: pixel art must stay crisp.
    return QIcon(QPixmap::fromImage(img.scaled(48, 48, Qt::KeepAspectRatio, Qt::FastTransformation)));
}

} // namespace

AssetPanel::AssetPanel(QWidget* parent) : QWidget(parent)
{
    setAcceptDrops(true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    message_ = new QLabel(this);
    message_->setWordWrap(true);
    message_->setTextFormat(Qt::PlainText);
    root->addWidget(message_);

    list_ = new QListWidget(this);
    list_->setIconSize({48, 48});
    list_->setSpacing(2);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(list_, 1);

    quantize_ = new QCheckBox(tr("Reduce colours automatically (accept full-colour PNGs)"), this);
    quantize_->setToolTip(tr("Without this, every PNG must already be an indexed (palette) image.\n"
                             "With it, png2zp reduces each image to at most 48 colours."));
    connect(quantize_, &QCheckBox::clicked, this, &AssetPanel::setQuantize);
    root->addWidget(quantize_);

    auto* budgets = new QWidget(this);
    budgetGrid_ = new QGridLayout(budgets);
    budgetGrid_->setContentsMargins(0, 4, 0, 4);
    budgetGrid_->setColumnStretch(1, 1);
    root->addWidget(budgets);

    buttons_ = new QWidget(this);
    auto* row = new QHBoxLayout(buttons_);
    row->setContentsMargins(0, 0, 0, 0);
    auto* add = new QPushButton(tr("Add Image…"), buttons_);
    add->setObjectName(QStringLiteral("primary"));
    auto* remove = new QPushButton(tr("Remove"), buttons_);
    auto* edit = new QPushButton(tr("Edit Tilemap"), buttons_);
    edit->setToolTip(tr("Select a background, then edit it as a map of tiles (a picture is converted first)"));
    auto* newMap = new QPushButton(tr("New Tilemap…"), buttons_);
    row->addWidget(add);
    row->addWidget(remove);
    row->addWidget(edit);
    row->addWidget(newMap);
    row->addStretch(1);
    root->addWidget(buttons_);
    connect(add, &QPushButton::clicked, this, &AssetPanel::chooseAndAdd);
    connect(remove, &QPushButton::clicked, this, &AssetPanel::removeSelected);
    connect(edit, &QPushButton::clicked, this, &AssetPanel::editSelectedAsTilemap);
    connect(newMap, &QPushButton::clicked, this, &AssetPanel::newTilemap);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { editSelectedAsTilemap(); });

    setProject(std::nullopt);
}

void AssetPanel::setProject(const std::optional<ProjectConfig>& project)
{
    project_ = project;
    refresh();
}

void AssetPanel::refresh()
{
    list_->clear();
    shown_.clear();
    while (QLayoutItem* it = budgetGrid_->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    manifestOk_ = false;

    const bool have = project_.has_value();
    buttons_->setEnabled(have);
    quantize_->setEnabled(have);
    if (!have) {
        message_->setText(tr("Open or create a project to see its art."));
        return;
    }

    QString err;
    if (!AssetManifest::load(project_->manifestPath(), &manifest_, &err)) {
        message_->setText(err);
        buttons_->setEnabled(false);
        quantize_->setEnabled(false);
        return;
    }
    manifestOk_ = true;
    quantize_->setChecked(manifest_.quantize() > 0);

    const AssetReport report = analyseAssets(manifest_);
    message_->setText(report.ok() ? tr("Drop PNG files here to add them.")
                                  : tr("%n problem(s) to fix before this will build.", nullptr, report.errorCount()));

    const QDir dir(manifest_.dir());
    for (const EntryReport& r : report.entries) {
        QString detail;
        if (!r.entry.image.isEmpty()) detail = r.entry.image;
        else if (!r.entry.tiled.isEmpty()) detail = r.entry.tiled;
        if (r.analysed) {
            detail += tr("  ·  %1×%2  ·  %3 colours  ·  %4 tiles").arg(r.width).arg(r.height).arg(r.colours).arg(r.tiles);
        }
        auto* item = new QListWidgetItem(r.entry.label() + QLatin1Char('\n') + detail, list_);
        if (!r.entry.image.isEmpty()) item->setIcon(thumbnail(dir.filePath(r.entry.image)));
        QStringList tips = r.errors + r.notes;
        if (!r.errors.isEmpty()) item->setForeground(QColor(theme::kBad));
        if (!tips.isEmpty()) item->setToolTip(tips.join(QLatin1Char('\n')));
        shown_ << r.entry;
    }

    int row = 0;
    for (const Budget& b : report.budgets) {
        auto* label = new QLabel(b.estimate ? tr("%1 (est.)").arg(b.label) : b.label);
        auto* bar = new QProgressBar;
        bar->setRange(0, b.limit);
        bar->setValue(qMin(b.used, b.limit));
        bar->setFormat(QStringLiteral("%1 / %2").arg(b.used).arg(b.limit));
        bar->setProperty("over", b.over());
        bar->style()->unpolish(bar);   // re-evaluate the stylesheet for the "over" property
        bar->style()->polish(bar);
        if (b.over()) bar->setToolTip(tr("Over the console's limit: the build will refuse this."));
        budgetGrid_->addWidget(label, row, 0);
        budgetGrid_->addWidget(bar, row, 1);
        ++row;
    }
}

bool AssetPanel::saveManifest()
{
    QString err;
    if (!manifest_.save(&err)) {
        QMessageBox::critical(this, tr("Cannot save assets.json"), err);
        return false;
    }
    emit manifestChanged();
    return true;
}

void AssetPanel::setQuantize(bool on)
{
    if (!manifestOk_) return;
    manifest_.setQuantize(on ? 48 : 0);
    if (saveManifest()) refresh();
}

QString AssetPanel::copyIntoAssets(const QString& file)
{
    const QDir assets(manifest_.dir());
    const QFileInfo src(file);
    // Already inside the assets folder: reference it where it is.
    if (QDir::cleanPath(src.absolutePath()).startsWith(QDir::cleanPath(assets.absolutePath()) + QLatin1Char('/')))
        return assets.relativeFilePath(src.absoluteFilePath());

    QString name = src.fileName();
    for (int n = 2; QFileInfo::exists(assets.filePath(name)); ++n)
        name = QStringLiteral("%1_%2.%3").arg(src.completeBaseName()).arg(n).arg(src.suffix());
    if (!QFile::copy(src.absoluteFilePath(), assets.filePath(name))) {
        QMessageBox::critical(this, tr("Cannot copy image"), tr("Could not copy %1 into %2.").arg(file, assets.absolutePath()));
        return {};
    }
    return name;
}

// A background, the font and the box border hold one image each: ask before replacing it.
bool AssetPanel::confirmReplace(zps::AssetEntry::Role role)
{
    if (role == AssetEntry::Role::Sprite) return true;
    for (const AssetEntry& e : manifest_.entries()) {
        if (e.role != role) continue;
        const auto answer = QMessageBox::question(this, tr("Replace %1?").arg(AssetEntry::roleLabel(role)),
            tr("The project already has a %1 (%2). Replace it?").arg(AssetEntry::roleLabel(role), e.image.isEmpty() ? e.tiled : e.image));
        return answer == QMessageBox::Yes;
    }
    return true;
}

void AssetPanel::addImages(const QStringList& files)
{
    if (!manifestOk_) return;
    bool changed = false;
    for (const QString& file : files) {
        const QImage img(file);
        if (img.isNull()) {
            QMessageBox::warning(this, tr("Not an image"), tr("%1 could not be read as an image.").arg(QFileInfo(file).fileName()));
            continue;
        }
        // A good first guess saves a click: an 8x8 image is a box border, a big one a background.
        int guess = 0;
        if (img.width() == 8 && img.height() == 8) guess = 4;
        else if (img.width() >= 128 && img.height() >= 128) guess = 1;

        bool ok = false;
        const QString kind = QInputDialog::getItem(this, tr("What is %1?").arg(QFileInfo(file).fileName()),
                                                   tr("Use this image as:"), kKinds, guess, false, &ok);
        if (!ok) continue;
        const int k = int(kKinds.indexOf(kind));

        const AssetEntry::Role role = k == 1 ? AssetEntry::Role::Background0 : k == 2 ? AssetEntry::Role::Background1
                                      : k == 3 ? AssetEntry::Role::Font : k == 4 ? AssetEntry::Role::Frame : AssetEntry::Role::Sprite;
        if (!confirmReplace(role)) continue;

        const QString rel = copyIntoAssets(file);
        if (rel.isEmpty()) continue;
        switch (role) {
        case AssetEntry::Role::Sprite:
            manifest_.addSprite(manifest_.uniqueSpriteName(QFileInfo(file).completeBaseName()), rel);
            break;
        case AssetEntry::Role::Background0:
        case AssetEntry::Role::Background1: manifest_.setBackground(role, rel); break;
        case AssetEntry::Role::Font:  manifest_.setFont(rel); break;
        case AssetEntry::Role::Frame: manifest_.setFrame(rel); break;
        case AssetEntry::Role::TextBox: break;
        }
        changed = true;
    }
    if (changed && saveManifest()) refresh();
}

void AssetPanel::chooseAndAdd()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add images"), QString(), tr("PNG images (*.png)"));
    if (!files.isEmpty()) addImages(files);
}

void AssetPanel::removeSelected()
{
    const int row = list_->currentRow();
    if (!manifestOk_ || row < 0 || row >= shown_.size()) return;
    const AssetEntry e = shown_[row];
    const auto answer = QMessageBox::question(this, tr("Remove from the project?"),
        tr("Remove %1 from assets.json? The image file itself is kept.").arg(e.label()));
    if (answer != QMessageBox::Yes) return;
    if (manifest_.remove(e) && saveManifest()) refresh();
}

void AssetPanel::dragEnterEvent(QDragEnterEvent* e)
{
    if (!manifestOk_ || !e->mimeData()->hasUrls()) return;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && u.toLocalFile().endsWith(QLatin1String(".png"), Qt::CaseInsensitive)) {
            e->acceptProposedAction();
            return;
        }
}

void AssetPanel::dropEvent(QDropEvent* e)
{
    QStringList files;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && u.toLocalFile().endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
            files << u.toLocalFile();
    e->acceptProposedAction();
    addImages(files);
}

// ---------------------------------------------------------------- tilemaps

namespace {
// <dir>/<base>.tmj, or <base>_2.tmj... if that exists.
QString freeMapPath(const QDir& dir, const QString& base)
{
    QString name = base + QStringLiteral(".tmj");
    for (int n = 2; QFileInfo::exists(dir.filePath(name)); ++n) name = QStringLiteral("%1_%2.tmj").arg(base).arg(n);
    return name;
}
} // namespace

void AssetPanel::editSelectedAsTilemap()
{
    const int row = list_->currentRow();
    if (!manifestOk_ || row < 0 || row >= shown_.size()) {
        QMessageBox::information(this, tr("Edit as a tilemap"), tr("Select Background 0 or Background 1 in the list first."));
        return;
    }
    const AssetEntry e = shown_[row];
    if (e.role != AssetEntry::Role::Background0 && e.role != AssetEntry::Role::Background1) {
        QMessageBox::information(this, tr("Edit as a tilemap"),
                                 tr("Only the two scrolling backgrounds are tilemaps. Select Background 0 or Background 1."));
        return;
    }
    const QDir base(manifest_.dir());
    if (!e.tiled.isEmpty()) { emit openMapRequested(base.filePath(e.tiled)); return; }
    if (e.image.isEmpty()) return;

    const auto answer = QMessageBox::question(this, tr("Edit %1 as a tilemap?").arg(e.label()),
        tr("%1 becomes a map of its 8×8 tiles, so any square of it can be repainted with tiles from the same picture.\n\n"
           "Nothing changes on screen, and %1 is kept. The map is saved next to it as a .tmj file (Tiled can open it too).")
            .arg(e.image));
    if (answer != QMessageBox::Yes) return;

    const QImage img(base.filePath(e.image));
    if (img.isNull()) {
        QMessageBox::critical(this, tr("Cannot read the picture"), e.image);
        return;
    }
    const QDir imageDir = QFileInfo(base.filePath(e.image)).absoluteDir();
    const QString tmjName = freeMapPath(imageDir, QFileInfo(e.image).completeBaseName());
    QString err;
    TileMap map = TileMap::createFromImage(imageDir.filePath(tmjName), QFileInfo(e.image).fileName(), img.size(),
                                           QStringLiteral("background"), &err);
    if (map.layers().isEmpty()) {
        QMessageBox::critical(this, tr("Cannot make a tilemap"), err);
        return;
    }
    if (!map.save(&err)) {
        QMessageBox::critical(this, tr("Cannot save the map"), err);
        return;
    }
    manifest_.setTiledBackground(e.role, base.relativeFilePath(imageDir.filePath(tmjName)), QStringLiteral("background"));
    if (saveManifest()) refresh();
    emit openMapRequested(imageDir.filePath(tmjName));
}

void AssetPanel::newTilemap()
{
    if (!manifestOk_) return;
    NewMapDialog dlg(manifest_.dir(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    const NewMapDialog::Result r = dlg.result();
    if (!confirmReplace(r.slot)) return;

    const QString sheetRel = copyIntoAssets(r.tilesetFile);
    if (sheetRel.isEmpty()) return;
    const QDir base(manifest_.dir());
    const QImage sheet(base.filePath(sheetRel));
    const QString tmjName = freeMapPath(base, r.name);
    TileMap map = TileMap::createEmpty(base.filePath(tmjName), sheetRel, sheet.size(), r.width, r.height, r.layer);
    QString err;
    if (!map.save(&err)) {
        QMessageBox::critical(this, tr("Cannot save the map"), err);
        return;
    }
    manifest_.setTiledBackground(r.slot, tmjName, r.layer);
    if (saveManifest()) refresh();
    emit openMapRequested(base.filePath(tmjName));
}
