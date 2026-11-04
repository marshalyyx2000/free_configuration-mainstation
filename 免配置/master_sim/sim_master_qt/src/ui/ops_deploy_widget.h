#ifndef OPS_DEPLOY_WIDGET_H
#define OPS_DEPLOY_WIDGET_H

#include "ui/lab_worker.h"

#include <QWidget>

#include <optional>

class SharedConnectionSettings;
class LabWorker;
class QLineEdit;
class QCheckBox;
class QTextEdit;
class QLabel;

class OpsDeployWidget : public QWidget {
    Q_OBJECT
public:
    explicit OpsDeployWidget(SharedConnectionSettings* settings, LabWorker* worker, QWidget* parent = nullptr);

signals:
    void requestJob(LabJobKind kind);
    void modelOutputsChanged();

private slots:
    void onProbe();
    void pickScd();
    void openOutDir();
    void clearLog();
    void appendLog(const QString& line);
    void onWorkerFinished(bool ok, double genSec, bool hasGenSec);
    void refreshModelInfo(std::optional<double> measuredSec = std::nullopt);

private:
    void setBusy(bool busy);
    void startJob(LabJobKind kind);

    SharedConnectionSettings* m_settings = nullptr;
    LabWorker* m_worker = nullptr;
    QLineEdit* m_hostEdit = nullptr;
    QLineEdit* m_maintPortEdit = nullptr;
    QLineEdit* m_mmsPortEdit = nullptr;
    QLineEdit* m_scdEdit = nullptr;
    QCheckBox* m_waitGenconfig = nullptr;
    QCheckBox* m_mmsAfterFull = nullptr;
    QTextEdit* m_log = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_modelSize = nullptr;
    QLabel* m_genconfigTime = nullptr;
    std::optional<double> m_lastGenconfigSec;
    bool m_busy = false;
};

#endif
