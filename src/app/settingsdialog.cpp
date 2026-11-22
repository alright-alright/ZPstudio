#include "settingsdialog.h"

#include "theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

using zps::Toolchain;

SettingsDialog::SettingsDialog(const Toolchain& t, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Settings: where the tools are"));
    setMinimumWidth(620);
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    root->addLayout(form);

    devtools_ = addRow(form, tr("ZPdevtools folder"), t.devtoolsDir, true);
    python_   = addRow(form, tr("Python 3"), t.python, false);
    compiler_ = new QComboBox(this);
    compiler_->addItems({tr("Automatic (zpcc if built, else def88186cc)"), tr("zpcc (clang)"), tr("def88186cc (portable C89)")});
    compiler_->setCurrentIndex(int(t.compilerPref));
    form->addRow(tr("C compiler"), compiler_);
    emulator_ = addRow(form, tr("Emulator (zeropoint_sdl)"), t.emulator, false);
    bootRom_  = addRow(form, tr("Boot ROM (optional)"), t.bootRom, false, tr("Boot ROM (*.bin);;All files (*)"));
    apuBios_  = addRow(form, tr("APU BIOS (optional)"), t.apuBios, false, tr("APU BIOS (*.bin);;All files (*)"));
    template_ = addRow(form, tr("Game template folder"), t.templateDir, true);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    root->addWidget(status_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* detect = buttons->addButton(tr("Detect"), QDialogButtonBox::ActionRole);
    detect->setToolTip(tr("Look for the tools next to this one (~/Code/ZPdevtools, ~/Code/ZeroPoint, ...)"));
    connect(detect, &QPushButton::clicked, this, &SettingsDialog::detect);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(compiler_, &QComboBox::currentIndexChanged, this, &SettingsDialog::validate);
    root->addWidget(buttons);
    validate();
}

QLineEdit* SettingsDialog::addRow(QFormLayout* form, const QString& label, const QString& value,
                                  bool folder, const QString& filter)
{
    auto* edit = new QLineEdit(value, this);
    auto* browse = new QPushButton(tr("Browse…"), this);
    auto* row = new QHBoxLayout;
    row->addWidget(edit, 1);
    row->addWidget(browse);
    form->addRow(label, row);
    connect(edit, &QLineEdit::textChanged, this, &SettingsDialog::validate);
    connect(browse, &QPushButton::clicked, this, [=] {
        const QString start = edit->text();
        const QString picked = folder ? QFileDialog::getExistingDirectory(this, label, start)
                                      : QFileDialog::getOpenFileName(this, label, start, filter);
        if (!picked.isEmpty()) edit->setText(picked);
    });
    return edit;
}

Toolchain SettingsDialog::toolchain() const
{
    Toolchain t;
    t.devtoolsDir = devtools_->text().trimmed();
    t.python = python_->text().trimmed();
    t.emulator = emulator_->text().trimmed();
    t.bootRom = bootRom_->text().trimmed();
    t.apuBios = apuBios_->text().trimmed();
    t.templateDir = template_->text().trimmed();
    t.compilerPref = static_cast<Toolchain::Compiler>(compiler_->currentIndex());
    return t;
}

void SettingsDialog::detect()
{
    const Toolchain found = Toolchain::detect(devtools_->text().trimmed());
    // Only fill what is empty: never overwrite something the user typed.
    auto fill = [](QLineEdit* e, const QString& v) { if (e->text().trimmed().isEmpty() && !v.isEmpty()) e->setText(v); };
    fill(devtools_, found.devtoolsDir);
    fill(python_, found.python);
    fill(emulator_, found.emulator);
    fill(template_, found.templateDir);
    validate();
}

void SettingsDialog::validate()
{
    const Toolchain t = toolchain();
    const QStringList build = t.buildProblems();
    const QStringList run = t.runProblems();
    QStringList lines;
    for (const QString& p : build) lines << QStringLiteral("✗ ") + p;
    for (const QString& p : run) lines << QStringLiteral("✗ ") + p;
    if (t.templateDir.isEmpty()) lines << tr("✗ No game template folder: New Project needs a ZPgameTemplate checkout.");
    if (build.isEmpty()) lines.prepend(tr("✓ Ready to build (compiler: %1).").arg(t.resolvedCompiler()));
    if (t.bootRom.isEmpty()) lines << tr("• No Boot ROM: games run on the emulator's built-in boot, which behaves slightly differently from real hardware.");
    status_->setText(lines.join(QLatin1Char('\n')));
    status_->setStyleSheet(QStringLiteral("color: %1;").arg(QString::fromLatin1(build.isEmpty() ? theme::kGood : theme::kWarn)));
}
