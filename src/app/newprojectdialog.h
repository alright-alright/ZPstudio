#pragma once
// "New Game": a name, a folder, a title. Hands back what createProject() needs.

#include "projectcreator.h"

#include <QDialog>

class QLabel;
class QLineEdit;

class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    NewProjectDialog(const QString& templateDir, QWidget* parent = nullptr);
    zps::CreateOptions options() const;

private:
    void update();

    QString templateDir_;
    QLineEdit *name_, *location_, *title_;
    QLabel* preview_;
    class QPushButton* ok_;
};
