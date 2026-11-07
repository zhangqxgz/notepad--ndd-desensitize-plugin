#include "instanceobj.h"
#include "editoractions.h"
#include "logutil.h"
#include "mainwindow.h"
#include "mappingstore.h"
#include "settingsstore.h"

#include <QApplication>
#include <QCursor>
#include <QKeySequence>
#include <QMessageBox>
#include <QShortcut>
#include <QTimer>
#include <QToolTip>
#include <qsciscintilla.h>

#include <functional>

extern QWidget *s_pMainNotepad;
extern std::function<QsciScintilla *(QWidget *)> s_getCurEdit;

namespace
{
void showToast(QWidget *anchor, const QString &text)
{
    if (anchor == nullptr)
    {
        QToolTip::showText(QCursor::pos(), text);
        return;
    }
    const QPoint position =
        anchor->mapToGlobal(QPoint(anchor->width() / 2 - 120, anchor->height() - 80));
    QToolTip::showText(position, text, anchor, QRect(), 2600);
}
}

InstanceObj::InstanceObj(QWidget *pNotepad)
    : QObject(pNotepad)
{
    m_pNotepad = pNotepad;
    reloadShortcuts();
}

InstanceObj::~InstanceObj() = default;

void InstanceObj::doMainWork()
{
    if (m_pMainWindow.isNull())
    {
        m_pMainWindow = new MainWindow(m_pNotepad);
        connect(m_pMainWindow, &MainWindow::shortcutsChanged, this, &InstanceObj::reloadShortcuts);
    }
    m_pMainWindow->show();
    m_pMainWindow->raise();
    m_pMainWindow->activateWindow();
}

void InstanceObj::reloadShortcuts()
{
    delete m_hotkeyForward;
    m_hotkeyForward = nullptr;
    delete m_hotkeyReverse;
    m_hotkeyReverse = nullptr;

    const AppSettings settings = SettingsStore::load();
    createShortcut(m_hotkeyForward, settings.hotkeyDesensitize, false);
    createShortcut(m_hotkeyReverse, settings.hotkeyRestore, true);
    logDebug(QStringLiteral("[快捷键] 已注册：脱敏=%1 还原=%2")
                 .arg(settings.hotkeyDesensitize.isEmpty() ? QStringLiteral("无") : settings.hotkeyDesensitize,
                      settings.hotkeyRestore.isEmpty() ? QStringLiteral("无") : settings.hotkeyRestore));
}

void InstanceObj::createShortcut(QShortcut *&target, const QString &sequence, bool reverse)
{
    if (sequence.trimmed().isEmpty())
    {
        return;
    }

    QShortcut *shortcut = new QShortcut(QKeySequence(sequence), m_pNotepad);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, reverse]() { quickApply(reverse); });
    target = shortcut;
}

void InstanceObj::quickDesensitize()
{
    quickApply(false);
}

void InstanceObj::quickRestore()
{
    quickApply(true);
}

void InstanceObj::quickApply(bool reverse)
{
    const QString actionName = reverse ? QStringLiteral("还原") : QStringLiteral("脱敏");
    const AppSettings settings = SettingsStore::load();
    const QVector<Mapping> mappings = MappingStore::load();

    if (mappings.isEmpty())
    {
        logDebug(QStringLiteral("[快捷键%1] 映射列表为空").arg(actionName));
        QMessageBox::information(m_pNotepad, QStringLiteral("脱敏处理器"),
                                 QStringLiteral("映射列表为空，请先在插件窗口中配置映射。"));
        return;
    }

    const ValidationResult validation = Desensitizer::validate(mappings, settings.caseSensitive);
    if (!validation.errors.isEmpty())
    {
        logDebug(QStringLiteral("[快捷键%1] 映射校验失败：%2").arg(actionName, validation.errors.join(QStringLiteral(" | "))));
        QMessageBox::critical(m_pNotepad, QStringLiteral("映射配置有误"),
                              validation.errors.join(QLatin1Char('\n')));
        return;
    }
    if (!validation.warnings.isEmpty())
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            m_pNotepad, QStringLiteral("映射配置提醒"),
            validation.warnings.join(QLatin1Char('\n')) + QStringLiteral("\n\n是否继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes)
        {
            return;
        }
    }

    QsciScintilla *editor = s_getCurEdit(s_pMainNotepad);
    const EditorActions::Result result =
        EditorActions::apply(editor, mappings, settings.caseSensitive, reverse, settings.selectionOnly);

    if (!result.ok)
    {
        logDebug(QStringLiteral("[快捷键%1] 未完成：%2").arg(actionName, result.error));
        if (settings.silent && result.noMatch)
        {
            showToast(editor, result.error);
        }
        else if (result.noMatch)
        {
            QMessageBox::information(m_pNotepad, QStringLiteral("脱敏处理器"), result.error);
        }
        else
        {
            QMessageBox::warning(m_pNotepad, QStringLiteral("脱敏处理器"), result.error);
        }
        return;
    }

    const QString summary = QStringLiteral("%1完成：共替换 %2 处，命中 %3 个关键词（可 Ctrl+Z 撤销）")
                                .arg(actionName)
                                .arg(result.totalHits)
                                .arg(result.hitKeys);
    logDebug(QStringLiteral("[快捷键%1] %2").arg(actionName, summary));
    if (settings.silent)
    {
        showToast(editor, summary);
    }
    else
    {
        QMessageBox::information(m_pNotepad, QStringLiteral("脱敏处理器"), summary);
    }
}
