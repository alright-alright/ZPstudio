#include "problemspanel.h"

#include "theme.h"

#include <QApplication>
#include <QFileInfo>
#include <QHeaderView>
#include <QStyle>

using zps::Diagnostic;

ProblemsPanel::ProblemsPanel(QWidget* parent) : QTreeWidget(parent)
{
    setColumnCount(3);
    setHeaderLabels({tr("Problem"), tr("Where"), tr("Step")});
    setRootIsDecorated(false);
    setUniformRowHeights(true);
    setAlternatingRowColors(true);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) {
        const QString file = it->data(0, Qt::UserRole).toString();
        if (!file.isEmpty()) emit openRequested(file, it->data(0, Qt::UserRole + 1).toInt());
    });
}

void ProblemsPanel::clearAll()
{
    clear();
    errors_ = warnings_ = 0;
    emit countsChanged(0, 0);
}

void ProblemsPanel::add(const Diagnostic& d)
{
    auto* it = new QTreeWidgetItem(this);
    QStyle::StandardPixmap icon = QStyle::SP_MessageBoxInformation;
    switch (d.severity) {
    case Diagnostic::Severity::Error:   icon = QStyle::SP_MessageBoxCritical; ++errors_; break;
    case Diagnostic::Severity::Warning: icon = QStyle::SP_MessageBoxWarning;  ++warnings_; break;
    case Diagnostic::Severity::Note: break;
    }
    it->setIcon(0, QApplication::style()->standardIcon(icon));
    it->setText(0, d.message);
    it->setToolTip(0, d.message);

    if (!d.file.isEmpty()) {
        it->setText(1, d.line > 0 ? QStringLiteral("%1:%2").arg(QFileInfo(d.file).fileName()).arg(d.line)
                                  : QFileInfo(d.file).fileName());
        it->setToolTip(1, d.file);
    }
    if (d.hasLocation()) {                       // only rows with a real location jump anywhere
        it->setData(0, Qt::UserRole, d.file);
        it->setData(0, Qt::UserRole + 1, d.line);
    }
    it->setText(2, d.step);
    if (d.severity == Diagnostic::Severity::Error) it->setForeground(0, QColor(theme::kBad));
    emit countsChanged(errors_, warnings_);
}
