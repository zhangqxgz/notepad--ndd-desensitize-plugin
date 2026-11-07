#include "editoractions.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QWidget>
#include <QApplication>
#include <QEventLoop>
#include <QTabWidget>
#include <qsciscintilla.h>

namespace
{
const int kMaxApplyChars = 30 * 1024 * 1024;

QTabWidget *findTabWidget(QsciScintilla *editor)
{
    QWidget *parent = editor != nullptr ? editor->parentWidget() : nullptr;
    while (parent != nullptr)
    {
        QTabWidget *tabWidget = qobject_cast<QTabWidget *>(parent);
        if (tabWidget != nullptr)
        {
            return tabWidget;
        }
        parent = parent->parentWidget();
    }
    return nullptr;
}
}

bool EditorActions::ensureLoaded(QsciScintilla *editor)
{
    if (editor == nullptr)
    {
        return false;
    }
    if (!editor->text().isEmpty())
    {
        return true;
    }

    QTabWidget *tabWidget = findTabWidget(editor);
    if (tabWidget == nullptr)
    {
        return false;
    }
    const int index = tabWidget->indexOf(editor);
    if (index < 0)
    {
        return false;
    }

    const int originalIndex = tabWidget->currentIndex();
    if (originalIndex != index)
    {
        tabWidget->setCurrentIndex(index);
    }
    for (int wait = 0; wait < 40 && editor->text().isEmpty(); ++wait)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    if (originalIndex >= 0 && originalIndex != index && originalIndex < tabWidget->count())
    {
        tabWidget->setCurrentIndex(originalIndex);
    }
    return !editor->text().isEmpty();
}

EditorActions::Result EditorActions::apply(QsciScintilla *editor,
                                           const QVector<Mapping> &mappings,
                                           bool caseSensitive,
                                           bool reverse,
                                           bool selectionOnly)
{
    Result result;
    if (editor == nullptr)
    {
        result.error = QStringLiteral("当前没有打开的文本编辑器。");
        return result;
    }
    if (editor->isReadOnly())
    {
        result.error = QStringLiteral("当前文档是只读模式（大文件快速模式或只读打开），无法执行替换。");
        return result;
    }

    const bool selectionOnlyActual = selectionOnly && editor->hasSelectedText();
    QString docBefore;
    QString source;
    if (selectionOnlyActual)
    {
        docBefore = editor->text();
        source = editor->selectedText();
    }
    else
    {
        source = editor->text();
        docBefore = source;
    }

    if (source.isEmpty())
    {
        result.emptyDocument = true;
        result.error = QStringLiteral("当前文档为空，未做修改。");
        return result;
    }
    if (source.size() > kMaxApplyChars)
    {
        result.error = QStringLiteral("文本过大（超过 30M 字符），请先切换到普通模式或缩小处理范围。");
        return result;
    }

    QHash<int, int> hitCounts;
    int totalHits = 0;
    const QString transformed = Desensitizer::apply(source, mappings, caseSensitive, reverse, &hitCounts, &totalHits);
    if (transformed == source)
    {
        result.noMatch = true;
        result.error = reverse ? QStringLiteral("未发现可还原的内容。") : QStringLiteral("未发现可脱敏的内容。");
        return result;
    }

    const QByteArray utf8 = transformed.toUtf8();
    editor->beginUndoAction();
    if (!selectionOnlyActual)
    {
        editor->SendScintilla(static_cast<unsigned int>(QsciScintillaBase::SCI_SELECTALL));
    }
    editor->SendScintilla(static_cast<unsigned int>(QsciScintillaBase::SCI_REPLACESEL), utf8.constData());
    editor->endUndoAction();

    bool writeOk = editor->text() != docBefore;
    if (!writeOk)
    {
        const QString fullResult = selectionOnlyActual
                                       ? Desensitizer::apply(docBefore, mappings, caseSensitive, reverse)
                                       : transformed;
        editor->setText(fullResult);
        writeOk = editor->text() != docBefore;
    }
    if (!writeOk)
    {
        result.error = QStringLiteral("写入编辑器失败。");
        return result;
    }

    int hitKeys = 0;
    for (int count : hitCounts)
    {
        if (count > 0)
        {
            ++hitKeys;
        }
    }

    result.ok = true;
    result.writeOk = true;
    result.totalHits = totalHits;
    result.hitKeys = hitKeys;
    return result;
}

QVector<QsciScintilla *> EditorActions::collectOpenEditors()
{
    QVector<QsciScintilla *> editors;
    QSet<QsciScintilla *> seen;
    const QWidgetList topLevels = QApplication::topLevelWidgets();
    for (QWidget *topLevel : topLevels)
    {
        const QList<QTabWidget *> tabWidgets = topLevel->findChildren<QTabWidget *>();
        for (QTabWidget *tabWidget : tabWidgets)
        {
            for (int index = 0; index < tabWidget->count(); ++index)
            {
                QWidget *page = tabWidget->widget(index);
                QsciScintilla *editor = qobject_cast<QsciScintilla *>(page);
                if (editor == nullptr || seen.contains(editor))
                {
                    continue;
                }
                if (editor->property("filePath").isValid() || editor->property("newfile").isValid())
                {
                    seen.insert(editor);
                    editors.append(editor);
                }
            }
        }
    }
    return editors;
}

QString EditorActions::editorPath(QsciScintilla *editor)
{
    if (editor == nullptr)
    {
        return QString();
    }
    return editor->property("filePath").toString();
}

QString EditorActions::editorLabel(QsciScintilla *editor)
{
    const QString path = editorPath(editor);
    if (!path.isEmpty())
    {
        return QFileInfo(path).fileName();
    }
    return QStringLiteral("未命名文档");
}
