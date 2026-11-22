#include "emulatorpane.h"

#include "theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QStackedLayout>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef ZPSTUDIO_HAVE_EMULATOR
#include "emulatorwidget.h"
#endif

EmulatorPane::EmulatorPane(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    auto* screen = new QWidget(this);
    stack_ = new QStackedLayout(screen);
    idle_ = new QLabel(available() ? tr("Press ▶ Run to play your game here.")
                                   : tr("This Studio was built without the emulator.\nRun opens the game in its own window."),
                       screen);
    idle_->setAlignment(Qt::AlignCenter);
    idle_->setWordWrap(true);
    idle_->setStyleSheet(QStringLiteral("color: %1;").arg(QString::fromLatin1(theme::kMuted)));
    stack_->addWidget(idle_);
    root->addWidget(screen, 1);

    auto* row = new QHBoxLayout;
    restart_ = new QToolButton(this);
    restart_->setText(tr("Restart"));
    restart_->setToolTip(tr("Soft reset: like pressing the console's reset button"));
    powerCycle_ = new QToolButton(this);
    powerCycle_->setText(tr("Power cycle"));
    powerCycle_->setToolTip(tr("Hard reset: switch the console off and on (replays the boot)"));
    mute_ = new QToolButton(this);
    mute_->setText(tr("Mute"));
    mute_->setCheckable(true);
    volume_ = new QSlider(Qt::Horizontal, this);
    volume_->setRange(0, 100);
    volume_->setMaximumWidth(90);
    volume_->setToolTip(tr("Volume"));
    row->addWidget(restart_);
    row->addWidget(powerCycle_);
    row->addStretch(1);
    row->addWidget(mute_);
    row->addWidget(volume_);
    root->addLayout(row);

    auto* info = new QHBoxLayout;
    focus_ = new QLabel(this);
    speed_ = new QLabel(this);
    speed_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    info->addWidget(focus_, 1);
    info->addWidget(speed_);
    root->addLayout(info);

    QSettings s;
    volume_->setValue(s.value(QStringLiteral("game/volume"), 80).toInt());
    mute_->setChecked(s.value(QStringLiteral("game/mute"), false).toBool());

    host_ = screen;
#ifdef ZPSTUDIO_HAVE_EMULATOR
    // The emulator widget (and the audio device it opens) is only made on the first Run: opening the sound card can
    // stall, and that must never be able to hold up the window appearing.
    connect(volume_, &QSlider::valueChanged, this, [this](int v) {
        if (emu_) emu_->setAudioVolume(v);
        QSettings().setValue(QStringLiteral("game/volume"), v);
    });
    connect(mute_, &QToolButton::toggled, this, [this](bool m) {
        if (emu_) emu_->setAudioEnabled(!m);
        QSettings().setValue(QStringLiteral("game/mute"), m);
    });
#endif
    updateControls();
}

EmulatorPane::~EmulatorPane() { stopGame(); }

bool EmulatorPane::available()
{
#ifdef ZPSTUDIO_HAVE_EMULATOR
    return true;
#else
    return false;
#endif
}

bool EmulatorPane::running() const
{
#ifdef ZPSTUDIO_HAVE_EMULATOR
    return emu_ && emu_->isActive();
#else
    return false;
#endif
}

QWidget* EmulatorPane::screen() const
{
#ifdef ZPSTUDIO_HAVE_EMULATOR
    return emu_;
#else
    return nullptr;
#endif
}

#ifdef ZPSTUDIO_HAVE_EMULATOR
void EmulatorPane::ensureEmulator()
{
    if (emu_) return;
    emu_ = new EmulatorWidget(host_);
    emu_->installEventFilter(this);               // to tell the user where the keyboard is
    stack_->addWidget(emu_);
    emu_->setAudioVolume(volume_->value());
    emu_->setAudioEnabled(!mute_->isChecked());
    connect(restart_, &QToolButton::clicked, emu_, &EmulatorWidget::reset);
    connect(powerCycle_, &QToolButton::clicked, emu_, &EmulatorWidget::hardReset);
    connect(emu_, &EmulatorWidget::fpsChanged, this, &EmulatorPane::showSpeed);
}
#endif

bool EmulatorPane::play(const zps::Toolchain& tc, const QString& romPath, QString* error)
{
#ifdef ZPSTUDIO_HAVE_EMULATOR
    ensureEmulator();
    // Boot ROM / APU BIOS apply on the next load; empty means the emulator's built-in boot.
    emu_->setBootROMPath(tc.bootRom);
    emu_->setApuBiosPath(tc.apuBios);
    if (!emu_->loadROM(romPath)) {
        if (error) *error = tr("The emulator could not load %1.\nIf you set a Boot ROM or APU BIOS in Settings, check those files "
                               "too: it refuses a ROM its Boot ROM cannot verify.").arg(romPath);
        updateControls();
        return false;
    }
    stack_->setCurrentWidget(emu_);
    emu_->start();                                // also takes the keyboard
    showFocus(true);
    updateControls();
    emit stateChanged();
    return true;
#else
    Q_UNUSED(tc) Q_UNUSED(romPath)
    if (error) *error = tr("This Studio was built without the emulator.");
    return false;
#endif
}

void EmulatorPane::stopGame()
{
#ifdef ZPSTUDIO_HAVE_EMULATOR
    if (!emu_ || !emu_->isActive()) return;
    emu_->stop();                                 // tears the machine down; the last frame stays on screen
    updateControls();
    focus_->setText(tr("Stopped (the last frame is kept; press ▶ Run to play again)"));
    focus_->setStyleSheet(QStringLiteral("color: %1;").arg(QString::fromLatin1(theme::kMuted)));
    emit stateChanged();
#endif
}

void EmulatorPane::updateControls()
{
    const bool on = running();
    restart_->setEnabled(on);
    powerCycle_->setEnabled(on);
    mute_->setEnabled(available());
    volume_->setEnabled(available());
    if (!on) {
        focus_->clear();
        speed_->clear();
    }
}

void EmulatorPane::showFocus(bool game)
{
    // Keys only reach the game while it has the focus; otherwise they type into the editor.
    focus_->setText(game ? tr("Keyboard: the game") : tr("Keyboard: editor (click the game to play)"));
    focus_->setStyleSheet(QStringLiteral("color: %1;").arg(QString::fromLatin1(game ? theme::kGood : theme::kMuted)));
}

void EmulatorPane::showSpeed(int fps)
{
    // fps is out of a full-speed 60, so 60 means the console is running in real time.
    speed_->setText(fps > 0 ? tr("%1 fps · %2%").arg(fps).arg(fps * 100 / 60) : QString());
    const bool slow = fps > 0 && fps < 55;
    speed_->setStyleSheet(QStringLiteral("color: %1;").arg(QString::fromLatin1(slow ? theme::kWarn : theme::kMuted)));
    speed_->setToolTip(slow ? tr("The console is running slower than real time. A Debug build of Studio does this; use Release.") : QString());
}

bool EmulatorPane::eventFilter(QObject* watched, QEvent* event)
{
#ifdef ZPSTUDIO_HAVE_EMULATOR
    if (watched == emu_ && running()) {
        if (event->type() == QEvent::FocusIn) showFocus(true);
        else if (event->type() == QEvent::FocusOut) showFocus(false);
    }
#endif
    return QWidget::eventFilter(watched, event);
}
