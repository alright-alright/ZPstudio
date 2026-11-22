#include "newprojectdialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStandardPaths>
#include <QVBoxLayout>

NewProjectDialog::NewProjectDialog(const QString& templateDir, QWidget* parent)
    : QDialog(parent), templateDir_(templateDir)
{
    setWindowTitle(tr("New Game"));
    setMinimumWidth(520);
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    root->addLayout(form);

    name_ = new QLineEdit(QStringLiteral("my_game"), this);
    name_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[A-Za-z0-9_-]*")), this));
    name_->setToolTip(tr("Letters, digits, - and _ (it becomes the folder and ROM file name)."));
    form->addRow(tr("Name"), name_);

    title_ = new QLineEdit(this);
    title_->setPlaceholderText(tr("Shown by the console; defaults to the name in capitals"));
    form->addRow(tr("Game title"), title_);

    location_ = new QLineEdit(QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                                  .filePath(QStringLiteral("ZeroPointGames")), this);
    auto* browse = new QPushButton(tr("Browse…"), this);
    auto* row = new QHBoxLayout;
    row->addWidget(location_, 1);
    row->addWidget(browse);
    form->addRow(tr("Create in"), row);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Where should the game folder go?"), location_->text());
        if (!d.isEmpty()) location_->setText(d);
    });

    preview_ = new QLabel(this);
    preview_->setWordWrap(true);
    preview_->setTextFormat(Qt::PlainText);
    root->addWidget(preview_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(tr("Create"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    for (QLineEdit* e : {name_, location_}) connect(e, &QLineEdit::textChanged, this, &NewProjectDialog::update);
    update();
}

zps::CreateOptions NewProjectDialog::options() const
{
    zps::CreateOptions o;
    o.templateDir = templateDir_;
    o.targetDir = QDir(location_->text().trimmed()).filePath(name_->text().trimmed());
    o.name = name_->text().trimmed();
    o.title = title_->text().trimmed();
    return o;
}

void NewProjectDialog::update()
{
    const zps::CreateOptions o = options();
    const QFileInfo target(o.targetDir);
    QString problem;
    if (o.name.isEmpty()) problem = tr("Give the game a name.");
    else if (location_->text().trimmed().isEmpty()) problem = tr("Choose where to create it.");
    else if (target.exists() && !QDir(o.targetDir).isEmpty()) problem = tr("That folder already has files in it.");
    preview_->setText(problem.isEmpty() ? tr("Will create %1\nwith the template's code and art, ready to build.").arg(QDir::toNativeSeparators(o.targetDir))
                                        : problem);
    ok_->setEnabled(problem.isEmpty());
}
