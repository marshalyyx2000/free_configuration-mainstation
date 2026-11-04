#include "ui/master_sim_shell.h"
#include "ui/mms_master_widget.h"
#include "ui/ops_deploy_widget.h"
#include "ui/lab_worker.h"
#include "settings/shared_connection_settings.h"

#include <QTabWidget>
#include <QThread>

MasterSimShell::MasterSimShell(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("免配置仿真主站"));
    resize(1280, 820);

    m_settings = new SharedConnectionSettings(this);

    m_workerThread = new QThread(this);
    m_worker = new LabWorker(m_settings, nullptr);
    m_worker->moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_workerThread->start();

    auto* tabs = new QTabWidget(this);
    m_ops = new OpsDeployWidget(m_settings, m_worker, tabs);
    m_mms = new MmsMasterWidget(m_settings, tabs);
    tabs->addTab(m_ops, tr("运维 / 下装"));
    tabs->addTab(m_mms, tr("61850 主站"));
    setCentralWidget(tabs);
    connect(tabs, &QTabWidget::currentChanged, this, [this, tabs](int index) {
        if (tabs->widget(index) == m_mms)
            m_mms->connectSubstation();
    });

    connect(m_ops, &OpsDeployWidget::requestJob, m_worker, &LabWorker::runJob, Qt::QueuedConnection);
    connect(m_ops, &OpsDeployWidget::modelOutputsChanged, m_mms, &MmsMasterWidget::reloadModelData);
    connect(m_worker, &LabWorker::modelOutputsChanged, m_mms, &MmsMasterWidget::reloadModelData);
}

bool MasterSimShell::runSelfTest()
{
    return m_mms && m_mms->runSelfTest();
}
