#pragma once
// Where the tools live. Validates as you type and says exactly what is missing and how to fix it.

#include "toolchain.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const zps::Toolchain& current, QWidget* parent = nullptr);
    zps::Toolchain toolchain() const;

private:
    QLineEdit* addRow(class QFormLayout* form, const QString& label, const QString& value,
                      bool folder, const QString& filter = QString());
    void detect();
    void validate();

    QLineEdit *devtools_, *python_, *emulator_, *bootRom_, *apuBios_, *template_;
    QComboBox* compiler_;
    QLabel* status_;
};
