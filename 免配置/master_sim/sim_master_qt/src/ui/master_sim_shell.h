#ifndef MASTER_SIM_SHELL_H
#define MASTER_SIM_SHELL_H

#include <QMainWindow>

class SharedConnectionSettings;
class MmsMasterWidget;
class OpsDeployWidget;
class LabWorker;
class QThread;

class MasterSimShell : public QMainWindow {
    Q_OBJECT
public:
    explicit MasterSimShell(QWidget* parent = nullptr);
    bool runSelfTest();

private:
    SharedConnectionSettings* m_settings = nullptr;
    LabWorker* m_worker = nullptr;
    QThread* m_workerThread = nullptr;
    OpsDeployWidget* m_ops = nullptr;
    MmsMasterWidget* m_mms = nullptr;
};

#endif
