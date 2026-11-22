#include "newmapdialog.h"

#include "tilemap.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace zps;

NewMapDialog::NewMapDialog(const QString& assetsDir, QWidget* parent) : QDialog(parent), assetsDir_(assetsDir)
{
    setWindowTitle(tr("New Tilemap"));
    setMinimumWidth(480);
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    root->addLayout(form);

    slot_ = new QComboBox(this);
    slot_->addItem(tr("Background 1 (front layer, see-through)"), int(AssetEntry::Role::Background1));
    slot_->addItem(tr("Background 0 (back layer)"), int(AssetEntry::Role::Background0));
    form->addRow(tr("For"), slot_);

    sheet_ = new QComboBox(this);
    sheet_->setObjectName(QStringLiteral("sheetBox"));
    const QStringList pngs = QDir(assetsDir).entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    for (const QString& f : pngs) sheet_->addItem(f, QDir(assetsDir).filePath(f));
    auto* browse = new QPushButton(tr("Browse…"), this);
    auto* row = new QHBoxLayout;
    row->addWidget(sheet_, 1);
    row->addWidget(browse);
    form->addRow(tr("Tile sheet"), row);

    width_ = new QSpinBox(this);
    height_ = new QSpinBox(this);
    for (QSpinBox* s : {width_, height_}) { s->setRange(1, kMapCells); s->setValue(kMapCells); s->setSuffix(tr(" cells")); }
    auto* size = new QHBoxLayout;
    size->addWidget(width_);
    size->addWidget(new QLabel(QStringLiteral("×"), this));
    size->addWidget(height_);
    size->addStretch(1);
    form->addRow(tr("Size"), size);

    name_ = new QLineEdit(QStringLiteral("level"), this);
    layer_ = new QLineEdit(QStringLiteral("layer"), this);
    name_->setObjectName(QStringLiteral("mapName"));
    layer_->setObjectName(QStringLiteral("layerName"));
    name_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[A-Za-z0-9_-]*")), this));
    form->addRow(tr("File name"), name_);
    form->addRow(tr("Layer name"), layer_);

    message_ = new QLabel(this);
    message_->setWordWrap(true);
    root->addWidget(message_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(tr("Create"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, &NewMapDialog::browse);
    connect(sheet_, &QComboBox::currentIndexChanged, this, &NewMapDialog::validate);
    connect(name_, &QLineEdit::textChanged, this, &NewMapDialog::validate);
    connect(layer_, &QLineEdit::textChanged, this, &NewMapDialog::validate);
    validate();
}

QString NewMapDialog::sheetPath() const { return sheet_->currentData().toString(); }

void NewMapDialog::browse()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("Choose the tile sheet"), assetsDir_, tr("PNG images (*.png)"));
    if (f.isEmpty()) return;
    int idx = sheet_->findData(f);
    if (idx < 0) { sheet_->addItem(QFileInfo(f).fileName(), f); idx = sheet_->count() - 1; }
    sheet_->setCurrentIndex(idx);
}

void NewMapDialog::validate()
{
    QString problem;
    const QString path = sheetPath();
    const QImage img(path);
    if (path.isEmpty()) problem = tr("Choose a tile sheet: a PNG of 8×8 tiles to paint with.");
    else if (img.isNull()) problem = tr("That image cannot be read.");
    else if (img.width() % kCellPixels || img.height() % kCellPixels)
        problem = tr("The sheet is %1×%2: it has to be a multiple of 8.").arg(img.width()).arg(img.height());
    else if (name_->text().trimmed().isEmpty()) problem = tr("Give the map a file name.");
    else if (QFileInfo::exists(QDir(assetsDir_).filePath(name_->text().trimmed() + QStringLiteral(".tmj"))))
        problem = tr("There is already a %1.tmj; pick another name.").arg(name_->text().trimmed());
    else if (layer_->text().trimmed().isEmpty()) problem = tr("Give the layer a name.");
    message_->setText(problem.isEmpty() ? tr("Creates %1.tmj in the art folder and uses it for this background.").arg(name_->text().trimmed())
                                        : problem);
    ok_->setEnabled(problem.isEmpty());
}

NewMapDialog::Result NewMapDialog::result() const
{
    Result r;
    r.slot = static_cast<AssetEntry::Role>(slot_->currentData().toInt());
    r.tilesetFile = sheetPath();
    r.width = width_->value();
    r.height = height_->value();
    r.name = name_->text().trimmed();
    r.layer = layer_->text().trimmed();
    return r;
}
