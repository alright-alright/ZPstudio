#pragma once
// The list of errors and warnings from a build. Activating a row asks the window to open the file at that line.

#include "diagnostics.h"

#include <QTreeWidget>

class ProblemsPanel : public QTreeWidget {
    Q_OBJECT
public:
    explicit ProblemsPanel(QWidget* parent = nullptr);

    void clearAll();
    void add(const zps::Diagnostic& d);
    int errorCount() const { return errors_; }
    int warningCount() const { return warnings_; }

signals:
    void countsChanged(int errors, int warnings);
    void openRequested(const QString& file, int line);

private:
    int errors_ = 0, warnings_ = 0;
};
