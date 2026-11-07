#pragma once

#include <QObject>
#include <QPointer>
#include <QWidget>

class QShortcut;
class MainWindow;

class InstanceObj : public QObject
{
public:
    explicit InstanceObj(QWidget *pNotepad);
    ~InstanceObj() override;

public slots:
    void doMainWork();
    void quickDesensitize();
    void quickRestore();
    void reloadShortcuts();

private:
    void quickApply(bool reverse);
    void createShortcut(QShortcut *&target, const QString &sequence, bool reverse);

    QWidget *m_pNotepad = nullptr;
    QPointer<MainWindow> m_pMainWindow;
    QShortcut *m_hotkeyForward = nullptr;
    QShortcut *m_hotkeyReverse = nullptr;

private:
    InstanceObj(const InstanceObj &other) = delete;
    InstanceObj &operator=(const InstanceObj &other) = delete;
};
