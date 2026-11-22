#pragma once
// New Project: copy a game template (ZPgameTemplate) into a fresh folder and give it a zpstudio.json.
// Adopt: give an existing template-shaped folder (a clone of ZPgameTemplate, say) a zpstudio.json.

#include "projectconfig.h"

#include <QString>

namespace zps {

struct CreateOptions {
    QString templateDir;     // a ZPgameTemplate checkout
    QString targetDir;       // must not exist or be empty
    QString name;            // output basename (letters, digits, - and _); derived from the folder if empty
    QString title;           // ROM title; defaults to the name in capitals
    QString developer = QStringLiteral("ZP");
};

// Copies src/ and assets/ (nothing else: no build output, git data or helper scripts).
bool createProject(const CreateOptions& options, ProjectConfig* out, QString* error);

// Writes zpstudio.json into a folder that ProjectConfig::looksLikeTemplate().
bool adoptProject(const QString& dir, ProjectConfig* out, QString* error);

} // namespace zps
