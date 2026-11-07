#pragma once

#include <QString>
#include <QVector>
#include "desensitizer.h"

class QsciScintilla;
class QWidget;

namespace EditorActions
{
struct Result
{
    bool ok = false;
    bool writeOk = false;
    bool noMatch = false;
    bool emptyDocument = false;
    QString error;
    int totalHits = 0;
    int hitKeys = 0;
};

Result apply(QsciScintilla *editor,
             const QVector<Mapping> &mappings,
             bool caseSensitive,
             bool reverse,
             bool selectionOnly);

QVector<QsciScintilla *> collectOpenEditors(QWidget *mainWindow);
QString editorLabel(QsciScintilla *editor);
QString editorPath(QsciScintilla *editor);
}
