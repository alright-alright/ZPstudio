#include "assetpanel.h"

#include "assetanalysis.h"
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
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch(1);
    root->addWidget(buttons_);
    connect(add, &QPushButton::clicked, this, &AssetPanel::chooseAndAdd);
    connect(remove, &QPushButton::clicked, this, &AssetPanel::removeSelected);

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
