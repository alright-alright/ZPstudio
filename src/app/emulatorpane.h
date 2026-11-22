#pragma once
// The game, inside the window: the emulator's own widget plus the few controls a developer wants while testing
// (restart, power-cycle, mute, volume) and a plain statement of where the keyboard is going.
//
// Compiled with the emulator only when CMake found a ZeroPoint checkout (ZPSTUDIO_HAVE_EMULATOR); otherwise
// available() is false and the window falls back to the external zeropoint_sdl process.

#include "toolchain.h"

#include <QWidget>

class QLabel;
class QSlider;
class QStackedLayout;
class QToolButton;
#ifdef ZPSTUDIO_HAVE_EMULATOR
class EmulatorWidget;
#endif

class EmulatorPane : public QWidget {
    Q_OBJECT
public:
    explicit EmulatorPane(QWidget* parent = nullptr);
    ~EmulatorPane() override;

    static bool available();

    // Loads the cartridge on a fresh machine (the real Boot ROM / APU BIOS when the toolchain names them) and runs it.
    bool play(const zps::Toolchain& tc, const QString& romPath, QString* error);
    void stopGame();
    bool running() const;

    // The widget the game is drawn in: what a test grabs to see the picture. Null without the emulator, and until
    // the first game is played (it is created then).
    QWidget* screen() const;

signals:
    void stateChanged();                       // a game started or stopped

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateControls();
#ifdef ZPSTUDIO_HAVE_EMULATOR
    void ensureEmulator();
#endif
    void showFocus(bool game);
    void showSpeed(int fps);

    QStackedLayout* stack_ = nullptr;
    QWidget* host_ = nullptr;                  // the area the game widget lives in
    QLabel* idle_ = nullptr;
    QLabel* focus_ = nullptr;
    QLabel* speed_ = nullptr;
    QToolButton *restart_ = nullptr, *powerCycle_ = nullptr, *mute_ = nullptr;
    QSlider* volume_ = nullptr;
#ifdef ZPSTUDIO_HAVE_EMULATOR
    EmulatorWidget* emu_ = nullptr;
#endif
};
