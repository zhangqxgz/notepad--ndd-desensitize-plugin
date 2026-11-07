#include "mainwindow.h"
#include "editoractions.h"
#include "logutil.h"
#include "mappingstore.h"
#include "settingsstore.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSet>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <qsciscintilla.h>

#include <functional>
#include <algorithm>

extern QWidget *s_pMainNotepad;
extern std::function<QsciScintilla *(QWidget *)> s_getCurEdit;

namespace
{
QString sequenceToText(const QKeySequence &sequence)
{
    if (sequence.isEmpty())
    {
        return QString();
    }
    return QKeySequence(sequence[0]).toString(QKeySequence::PortableText);
}

QString randomToken(int length)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    QString token;
    token.reserve(length);
    for (int i = 0; i < length; ++i)
    {
        token.append(QLatin1Char(alphabet[QRandomGenerator::global()->bounded(static_cast<int>(sizeof(alphabet)) - 1)]));
    }
    return token;
}

QTableWidgetItem *makeCheckItem(bool checked)
{
    QTableWidgetItem *item = new QTableWidgetItem();
    item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    return item;
}

QTableWidgetItem *makeTextItem(const QString &text)
{
    return new QTableWidgetItem(text);
}
}

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("脱敏处理器 - 映射配置"));
    setWindowFlag(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(760, 520);

    QVBoxLayout *rootLayout = new QVBoxLayout(this);

    QHBoxLayout *toolLayout = new QHBoxLayout();
    const char *toolNames[] = {"添加行", "删除行", "上移", "下移", "生成随机串", "批量粘贴", "导入", "导出"};
    for (const char *name : toolNames)
    {
        QPushButton *button = new QPushButton(QString::fromUtf8(name), this);
        toolLayout->addWidget(button);
        const QString text = QString::fromUtf8(name);
        if (text == QStringLiteral("添加行"))
        {
            connect(button, &QPushButton::clicked, this, &MainWindow::addRow);
        }
        else if (text == QStringLiteral("删除行"))
        {
            connect(button, &QPushButton::clicked, this, &MainWindow::removeRows);
        }
        else if (text == QStringLiteral("上移"))
        {
            connect(button, &QPushButton::clicked, this, &MainWindow::moveUp);
        }
        else if (text == QStringLiteral("下移"))
        {
            connect(button, &QPushButton::clicked, this, &MainWindow::moveDown);
        }
        else if (text == QStringLiteral("生成随机串"))
        {
            connect(button, &QPushButton::clicked, this, &MainWindow::generateRandom);
        }
        else if (text == QStringLiteral("批量粘贴"))
        {
            connect(button, &QPushButton::clicked, this, &MainWindow::pasteBatch);
        }
        else if (text == QStringLiteral("导入"))
        {
            connect(button, &QPushButton::clicked, this, &MainWindow::importFile);
        }
        else
        {
            connect(button, &QPushButton::clicked, this, &MainWindow::exportFile);
        }
    }
    toolLayout->addStretch();
    rootLayout->addLayout(toolLayout);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("启用") << QStringLiteral("原文") << QStringLiteral("替换为"));
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->setDefaultSectionSize(24);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked
                             | QAbstractItemView::EditKeyPressed);
    connect(m_table, &QTableWidget::itemChanged, this, &MainWindow::onItemChanged);
    rootLayout->addWidget(m_table, 1);

    QHBoxLayout *optionLayout = new QHBoxLayout();
    m_caseSensitive = new QCheckBox(QStringLiteral("区分大小写"), this);
    m_caseSensitive->setChecked(true);
    m_selectedOnly = new QCheckBox(QStringLiteral("仅处理选中文本"), this);
    QPushButton *configDirButton = new QPushButton(QStringLiteral("打开配置目录"), this);
    optionLayout->addWidget(m_caseSensitive);
    optionLayout->addWidget(m_selectedOnly);
    optionLayout->addStretch();
    optionLayout->addWidget(configDirButton);
    connect(configDirButton, &QPushButton::clicked, this, &MainWindow::openConfigDir);
    rootLayout->addLayout(optionLayout);

    QHBoxLayout *shortcutLayout = new QHBoxLayout();
    QLabel *hotkeyLabel = new QLabel(QStringLiteral("快捷键：脱敏"), this);
    m_hotkeyForward = new QKeySequenceEdit(this);
    QLabel *hotkeyRestoreLabel = new QLabel(QStringLiteral("还原"), this);
    m_hotkeyReverse = new QKeySequenceEdit(this);
    QPushButton *resetShortcutButton = new QPushButton(QStringLiteral("恢复默认"), this);
    m_silent = new QCheckBox(QStringLiteral("静默提示"), this);
    shortcutLayout->addWidget(hotkeyLabel);
    shortcutLayout->addWidget(m_hotkeyForward);
    shortcutLayout->addWidget(hotkeyRestoreLabel);
    shortcutLayout->addWidget(m_hotkeyReverse);
    shortcutLayout->addWidget(resetShortcutButton);
    shortcutLayout->addWidget(m_silent);
    shortcutLayout->addStretch();
    rootLayout->addLayout(shortcutLayout);

    connect(m_hotkeyForward, &QKeySequenceEdit::keySequenceChanged, this, &MainWindow::saveSettingsFromUi);
    connect(m_hotkeyReverse, &QKeySequenceEdit::keySequenceChanged, this, &MainWindow::saveSettingsFromUi);
    connect(m_silent, &QCheckBox::toggled, this, &MainWindow::saveSettingsFromUi);
    connect(resetShortcutButton, &QPushButton::clicked, this, &MainWindow::resetShortcuts);

    QHBoxLayout *actionLayout = new QHBoxLayout();
    QPushButton *forwardButton = new QPushButton(QStringLiteral("脱敏"), this);
    QPushButton *reverseButton = new QPushButton(QStringLiteral("还原"), this);
    QPushButton *allForwardButton = new QPushButton(QStringLiteral("全部打开文件脱敏"), this);
    QPushButton *allReverseButton = new QPushButton(QStringLiteral("全部打开文件还原"), this);
    QPushButton *saveButton = new QPushButton(QStringLiteral("保存配置"), this);
    actionLayout->addWidget(forwardButton);
    actionLayout->addWidget(reverseButton);
    actionLayout->addWidget(allForwardButton);
    actionLayout->addWidget(allReverseButton);
    actionLayout->addWidget(saveButton);
    m_status = new QLabel(this);
    actionLayout->addWidget(m_status, 1);
    rootLayout->addLayout(actionLayout);

    connect(forwardButton, &QPushButton::clicked, this, &MainWindow::applyForward);
    connect(reverseButton, &QPushButton::clicked, this, &MainWindow::applyReverse);
    connect(allForwardButton, &QPushButton::clicked, this, &MainWindow::applyAllForward);
    connect(allReverseButton, &QPushButton::clicked, this, &MainWindow::applyAllReverse);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveConfig);
    connect(m_caseSensitive, &QCheckBox::toggled, this, &MainWindow::saveSettingsFromUi);
    connect(m_selectedOnly, &QCheckBox::toggled, this, &MainWindow::saveSettingsFromUi);

    QLabel *hint = new QLabel(QStringLiteral("提示：快捷键作用于当前文件；「全部打开文件」会处理所有已打开标签页（各自可用 Ctrl+Z 撤销）。配置自动保存到 %1")
                                  .arg(MappingStore::configFilePath()),
                              this);
    hint->setWordWrap(true);
    rootLayout->addWidget(hint);

    loadSettingsToUi();

    QString error;
    const QVector<Mapping> mappings = MappingStore::load(&error);
    if (!error.isEmpty())
    {
        setStatus(error);
    }
    setMappings(mappings, false);
    setStatus(QStringLiteral("共 %1 条映射").arg(mappings.size()));
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveConfig();
    QWidget::closeEvent(event);
}

QVector<Mapping> MainWindow::collectMappings() const
{
    QVector<Mapping> mappings;
    mappings.reserve(m_table->rowCount());
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        Mapping mapping;
        QTableWidgetItem *enabledItem = m_table->item(row, 0);
        QTableWidgetItem *fromItem = m_table->item(row, 1);
        QTableWidgetItem *toItem = m_table->item(row, 2);
        mapping.enabled = enabledItem == nullptr || enabledItem->checkState() == Qt::Checked;
        mapping.from = fromItem == nullptr ? QString() : fromItem->text();
        mapping.to = toItem == nullptr ? QString() : toItem->text();
        if (mapping.from.isEmpty() && mapping.to.isEmpty())
        {
            continue;
        }
        mappings.append(mapping);
    }
    return mappings;
}

void MainWindow::setMappings(const QVector<Mapping> &mappings, bool append)
{
    m_loading = true;
    if (!append)
    {
        m_table->setRowCount(0);
    }
    for (const Mapping &mapping : mappings)
    {
        appendRow(mapping);
    }
    m_loading = false;
}

void MainWindow::appendRow(const Mapping &mapping)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, makeCheckItem(mapping.enabled));
    m_table->setItem(row, 1, makeTextItem(mapping.from));
    m_table->setItem(row, 2, makeTextItem(mapping.to));
}

void MainWindow::appendMappings(const QVector<Mapping> &mappings)
{
    if (mappings.isEmpty())
    {
        return;
    }
    setMappings(mappings, true);
    markAsChanged();
}

void MainWindow::markAsChanged()
{
    if (m_loading)
    {
        return;
    }
    QString error;
    if (!MappingStore::save(collectMappings(), &error))
    {
        setStatus(error);
    }
    else
    {
        setStatus(QStringLiteral("已自动保存 %1 条映射").arg(m_table->rowCount()));
    }
}

void MainWindow::setStatus(const QString &text)
{
    m_status->setText(text);
}

void MainWindow::addRow()
{
    m_loading = true;
    appendRow(Mapping());
    m_loading = false;
    const int row = m_table->rowCount() - 1;
    m_table->setCurrentCell(row, 1);
    m_table->editItem(m_table->item(row, 1));
    markAsChanged();
}

void MainWindow::removeRows()
{
    QList<int> rows;
    const QList<QTableWidgetItem *> selected = m_table->selectedItems();
    for (QTableWidgetItem *item : selected)
    {
        if (!rows.contains(item->row()))
        {
            rows.append(item->row());
        }
    }
    if (rows.isEmpty() && m_table->currentRow() >= 0)
    {
        rows.append(m_table->currentRow());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
    {
        m_table->removeRow(row);
    }
    markAsChanged();
}

void MainWindow::moveUp()
{
    const int row = m_table->currentRow();
    if (row <= 0)
    {
        return;
    }
    m_loading = true;
    QTableWidgetItem *enabledItem = m_table->takeItem(row, 0);
    QTableWidgetItem *fromItem = m_table->takeItem(row, 1);
    QTableWidgetItem *toItem = m_table->takeItem(row, 2);
    m_table->removeRow(row);
    m_table->insertRow(row - 1);
    m_table->setItem(row - 1, 0, enabledItem);
    m_table->setItem(row - 1, 1, fromItem);
    m_table->setItem(row - 1, 2, toItem);
    m_table->setCurrentCell(row - 1, m_table->currentColumn());
    m_loading = false;
    markAsChanged();
}

void MainWindow::moveDown()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_table->rowCount() - 1)
    {
        return;
    }
    m_loading = true;
    QTableWidgetItem *enabledItem = m_table->takeItem(row, 0);
    QTableWidgetItem *fromItem = m_table->takeItem(row, 1);
    QTableWidgetItem *toItem = m_table->takeItem(row, 2);
    m_table->removeRow(row);
    m_table->insertRow(row + 1);
    m_table->setItem(row + 1, 0, enabledItem);
    m_table->setItem(row + 1, 1, fromItem);
    m_table->setItem(row + 1, 2, toItem);
    m_table->setCurrentCell(row + 1, m_table->currentColumn());
    m_loading = false;
    markAsChanged();
}

void MainWindow::generateRandom()
{
    QList<int> rows;
    const QList<QTableWidgetItem *> selected = m_table->selectedItems();
    for (QTableWidgetItem *item : selected)
    {
        if (item->column() == 2 && !rows.contains(item->row()))
        {
            rows.append(item->row());
        }
    }
    if (rows.isEmpty())
    {
        if (m_table->currentRow() < 0)
        {
            setStatus(QStringLiteral("请先选择要生成替换串的行。"));
            return;
        }
        rows.append(m_table->currentRow());
    }

    QSet<QString> used;
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        used.insert(m_table->item(row, 1) != nullptr ? m_table->item(row, 1)->text().toLower() : QString());
        used.insert(m_table->item(row, 2) != nullptr ? m_table->item(row, 2)->text().toLower() : QString());
    }

    m_loading = true;
    for (int row : rows)
    {
        QString token;
        do
        {
            token = randomToken(12);
        } while (used.contains(token));
        used.insert(token);
        if (m_table->item(row, 2) == nullptr)
        {
            m_table->setItem(row, 2, makeTextItem(token));
        }
        else
        {
            m_table->item(row, 2)->setText(token);
        }
    }
    m_loading = false;
    markAsChanged();
    setStatus(QStringLiteral("已生成 %1 个随机替换串").arg(rows.size()));
}

void MainWindow::pasteBatch()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("批量粘贴映射"));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *tip = new QLabel(QStringLiteral("每行一条，格式如 abc->aldjfakldfj 或 abc,aldjfakldfj（支持 ->、=>、Tab、逗号、= 分隔，# 开头为注释）"), &dialog);
    tip->setWordWrap(true);
    QPlainTextEdit *editor = new QPlainTextEdit(&dialog);
    editor->setPlaceholderText(QStringLiteral("abc->aldjfakldfj\n姓名,张*三"));
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(tip);
    layout->addWidget(editor, 1);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    int skipped = 0;
    const QVector<Mapping> mappings = MappingStore::parseBatchText(editor->toPlainText(), &skipped);
    if (mappings.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("批量粘贴"), QStringLiteral("没有解析到有效映射。"));
        return;
    }
    appendMappings(mappings);
    setStatus(QStringLiteral("批量添加 %1 条映射，跳过 %2 行").arg(mappings.size()).arg(skipped));
}

void MainWindow::importFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入映射"), QString(),
        QStringLiteral("映射文件 (*.json *.csv *.txt);;JSON (*.json);;CSV (*.csv);;文本 (*.txt);;所有文件 (*)"));
    if (path.isEmpty())
    {
        return;
    }

    QString error;
    const QVector<Mapping> mappings = MappingStore::importFrom(path, &error);
    if (mappings.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("导入失败"), error.isEmpty() ? QStringLiteral("文件为空。") : error);
        return;
    }
    appendMappings(mappings);
    setStatus(QStringLiteral("已从 %1 导入 %2 条映射").arg(QFileInfo(path).fileName()).arg(mappings.size()));
}

void MainWindow::exportFile()
{
    const QVector<Mapping> mappings = collectMappings();
    if (mappings.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("导出"), QStringLiteral("当前没有可导出的映射。"));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出映射"),
                                                      QStringLiteral("mappings.json"),
                                                      QStringLiteral("JSON (*.json);;CSV (*.csv);;文本 (*.txt)"));
    if (path.isEmpty())
    {
        return;
    }

    QString error;
    if (!MappingStore::exportTo(path, mappings, &error))
    {
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        return;
    }
    setStatus(QStringLiteral("已导出 %1 条映射到 %2").arg(mappings.size()).arg(QFileInfo(path).fileName()));
}

void MainWindow::saveConfig()
{
    const QVector<Mapping> mappings = collectMappings();
    QString error;
    if (!MappingStore::save(mappings, &error))
    {
        setStatus(error);
        return;
    }
    setStatus(QStringLiteral("已保存 %1 条映射到配置文件").arg(mappings.size()));
}

void MainWindow::openConfigDir()
{
    const QFileInfo info(MappingStore::configFilePath());
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

void MainWindow::onItemChanged(QTableWidgetItem *item)
{
    Q_UNUSED(item);
    markAsChanged();
}

void MainWindow::applyForward()
{
    applyText(false);
}

void MainWindow::applyReverse()
{
    applyText(true);
}

void MainWindow::applyText(bool reverse)
{
    const QString actionName = reverse ? QStringLiteral("还原") : QStringLiteral("脱敏");
    logDebug(QStringLiteral("[%1] clicked").arg(actionName));

    const QVector<Mapping> mappings = collectMappings();
    if (mappings.isEmpty())
    {
        logDebug(QStringLiteral("[%1] no mappings, abort").arg(actionName));
        QMessageBox::information(this, QStringLiteral("脱敏处理器"), QStringLiteral("映射列表为空，请先添加映射。"));
        return;
    }

    const bool caseSensitive = m_caseSensitive->isChecked();
    const ValidationResult validation = Desensitizer::validate(mappings, caseSensitive);
    if (!validation.errors.isEmpty())
    {
        logDebug(QStringLiteral("[%1] validation errors: %2").arg(actionName, validation.errors.join(QStringLiteral(" | "))));
        QMessageBox::critical(this, QStringLiteral("映射配置有误"), validation.errors.join(QLatin1Char('\n')));
        return;
    }
    if (!validation.warnings.isEmpty())
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, QStringLiteral("映射配置提醒"),
            validation.warnings.join(QLatin1Char('\n')) + QStringLiteral("\n\n是否继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes)
        {
            logDebug(QStringLiteral("[%1] user cancelled on warnings").arg(actionName));
            return;
        }
    }

    QsciScintilla *editor = s_getCurEdit(s_pMainNotepad);
    logDebug(QStringLiteral("[%1] mappings=%2 editor=%3").arg(actionName).arg(mappings.size())
                 .arg(reinterpret_cast<quintptr>(editor), 0, 16));

    const EditorActions::Result result =
        EditorActions::apply(editor, mappings, caseSensitive, reverse, m_selectedOnly->isChecked());

    if (!result.ok)
    {
        if (result.noMatch)
        {
            const QString tip =
                reverse
                    ? QStringLiteral("未发现可还原的内容。\n\n已启用 %1 条映射，请检查：\n1) 映射行是否勾选启用\n2) 原文是否与文档一致\n3) 区分大小写开关是否合适")
                          .arg(mappings.size())
                    : QStringLiteral("未发现可脱敏的内容。\n\n已启用 %1 条映射，请检查：\n1) 映射行是否勾选启用\n2) 原文是否与文档一致\n3) 区分大小写开关是否合适")
                          .arg(mappings.size());
            setStatus(result.error);
            logDebug(QStringLiteral("[%1] no match").arg(actionName));
            QMessageBox::information(this, QStringLiteral("脱敏处理器"), tip);
        }
        else
        {
            setStatus(result.error);
            logDebug(QStringLiteral("[%1] failed: %2").arg(actionName, result.error));
            QMessageBox::warning(this, QStringLiteral("脱敏处理器"), result.error);
        }
        return;
    }

    const QString summary = QStringLiteral("%1完成：共替换 %2 处，命中 %3 个关键词（可 Ctrl+Z 撤销）")
                                .arg(actionName)
                                .arg(result.totalHits)
                                .arg(result.hitKeys);
    logDebug(QStringLiteral("[%1] done: %2").arg(actionName, summary));
    setStatus(summary);
    QMessageBox::information(this, QStringLiteral("脱敏处理器"), summary);
}

void MainWindow::applyAllForward()
{
    applyToOpenDocuments(false);
}

void MainWindow::applyAllReverse()
{
    applyToOpenDocuments(true);
}

void MainWindow::applyToOpenDocuments(bool reverse)
{
    const QString actionName = reverse ? QStringLiteral("还原") : QStringLiteral("脱敏");
    const QVector<Mapping> mappings = collectMappings();
    if (mappings.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("脱敏处理器"), QStringLiteral("映射列表为空，请先添加映射。"));
        return;
    }

    const bool caseSensitive = m_caseSensitive->isChecked();
    const ValidationResult validation = Desensitizer::validate(mappings, caseSensitive);
    if (!validation.errors.isEmpty())
    {
        QMessageBox::critical(this, QStringLiteral("映射配置有误"), validation.errors.join(QLatin1Char('\n')));
        return;
    }
    if (!validation.warnings.isEmpty())
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, QStringLiteral("映射配置提醒"),
            validation.warnings.join(QLatin1Char('\n')) + QStringLiteral("\n\n是否继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes)
        {
            return;
        }
    }

    const QVector<QsciScintilla *> editors = EditorActions::collectOpenEditors();
    logDebug(QStringLiteral("[全部%1] 枚举到 %2 个标签页").arg(actionName).arg(editors.size()));
    for (QsciScintilla *editor : editors)
    {
        logDebug(QStringLiteral("[全部%1] %2 | path=%3 | len=%4 | readonly=%5")
                     .arg(actionName, EditorActions::editorLabel(editor), EditorActions::editorPath(editor))
                     .arg(editor->text().size())
                     .arg(editor->isReadOnly()));
    }
    if (editors.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("脱敏处理器"), QStringLiteral("当前没有打开的文档。"));
        return;
    }

    QStringList labels;
    int readableCount = 0;
    for (QsciScintilla *editor : editors)
    {
        QString label = EditorActions::editorLabel(editor);
        if (editor->isReadOnly())
        {
            label += QStringLiteral("（只读）");
        }
        else
        {
            ++readableCount;
        }
        labels << label;
    }

    const int previewCount = qMin(labels.size(), 20);
    QStringList preview = labels.mid(0, previewCount);
    if (labels.size() > previewCount)
    {
        preview << QStringLiteral("…… 另有 %1 个文档").arg(labels.size() - previewCount);
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("全部打开文件%1").arg(actionName),
        QStringLiteral("将对当前打开的 %1 个文档执行%2（含未保存内容，可处理的文档 %3 个）：\n\n%4\n\n"
                       "NDD 恢复的懒加载标签页会自动加载后再处理。\n"
                       "每个文档切换过去后可按 Ctrl+Z 撤销；未保存的修改需要手动保存。\n\n是否继续？")
            .arg(editors.size())
            .arg(actionName)
            .arg(readableCount)
            .arg(preview.join(QLatin1Char('\n'))),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
    {
        return;
    }

    int changedCount = 0;
    int noMatchCount = 0;
    int readOnlyCount = 0;
    int emptyCount = 0;
    int failedCount = 0;
    int totalHits = 0;
    QStringList failedDetails;

    for (QsciScintilla *editor : editors)
    {
        const QString label = EditorActions::editorLabel(editor);
        if (editor->isReadOnly())
        {
            ++readOnlyCount;
            continue;
        }

        if (editor->text().isEmpty())
        {
            EditorActions::ensureLoaded(editor);
        }
        if (editor->text().isEmpty())
        {
            ++emptyCount;
            continue;
        }

        const EditorActions::Result result =
            EditorActions::apply(editor, mappings, caseSensitive, reverse, false);
        if (result.ok)
        {
            ++changedCount;
            totalHits += result.totalHits;
        }
        else if (result.emptyDocument)
        {
            ++emptyCount;
        }
        else if (result.noMatch)
        {
            ++noMatchCount;
        }
        else
        {
            ++failedCount;
            failedDetails << QStringLiteral("%1：%2").arg(label, result.error);
        }
    }

    QStringList lines;
    lines << QStringLiteral("%1完成：处理 %2 个文档，共替换 %3 处")
                 .arg(actionName)
                 .arg(changedCount)
                 .arg(totalHits);
    if (noMatchCount > 0)
    {
        lines << QStringLiteral("无命中：%1 个").arg(noMatchCount);
    }
    if (readOnlyCount > 0)
    {
        lines << QStringLiteral("只读跳过：%1 个").arg(readOnlyCount);
    }
    if (emptyCount > 0)
    {
        lines << QStringLiteral("空文档跳过：%1 个").arg(emptyCount);
    }
    if (failedCount > 0)
    {
        lines << QStringLiteral("失败：%1 个").arg(failedCount);
        lines << failedDetails.mid(0, 10).join(QLatin1Char('\n'));
    }
    lines << QStringLiteral("未保存的文档请手动保存；每个文档可用 Ctrl+Z 撤销。");

    const QString summary = lines.join(QLatin1Char('\n'));
    logDebug(QStringLiteral("[全部%1] %2").arg(actionName, summary));
    setStatus(QStringLiteral("全部%1：处理 %2 个文档，替换 %3 处").arg(actionName).arg(changedCount).arg(totalHits));

    if (failedCount > 0)
    {
        QMessageBox::warning(this, QStringLiteral("全部打开文件%1").arg(actionName), summary);
    }
    else
    {
        QMessageBox::information(this, QStringLiteral("全部打开文件%1").arg(actionName), summary);
    }
}

void MainWindow::resetShortcuts()
{
    m_loadingSettings = true;
    m_hotkeyForward->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+D")));
    m_hotkeyReverse->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+R")));
    m_loadingSettings = false;
    saveSettingsFromUi();
}

void MainWindow::loadSettingsToUi()
{
    m_loadingSettings = true;
    m_settings = SettingsStore::load();
    m_caseSensitive->setChecked(m_settings.caseSensitive);
    m_selectedOnly->setChecked(m_settings.selectionOnly);
    m_silent->setChecked(m_settings.silent);
    m_hotkeyForward->setKeySequence(QKeySequence(m_settings.hotkeyDesensitize));
    m_hotkeyReverse->setKeySequence(QKeySequence(m_settings.hotkeyRestore));
    m_loadingSettings = false;
}

void MainWindow::saveSettingsFromUi()
{
    if (m_loadingSettings)
    {
        return;
    }

    AppSettings settings;
    settings.hotkeyDesensitize = sequenceToText(m_hotkeyForward->keySequence());
    settings.hotkeyRestore = sequenceToText(m_hotkeyReverse->keySequence());
    settings.caseSensitive = m_caseSensitive->isChecked();
    settings.selectionOnly = m_selectedOnly->isChecked();
    settings.silent = m_silent->isChecked();

    if (!settings.hotkeyDesensitize.isEmpty() && settings.hotkeyDesensitize == settings.hotkeyRestore)
    {
        QMessageBox::warning(this, QStringLiteral("快捷键冲突"),
                             QStringLiteral("脱敏和还原不能使用同一个快捷键，已恢复为上一次的设置。"));
        loadSettingsToUi();
        return;
    }

    m_settings = settings;
    QString error;
    if (!SettingsStore::save(settings, &error))
    {
        setStatus(error);
        return;
    }
    emit shortcutsChanged();
}

