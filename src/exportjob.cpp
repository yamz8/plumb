#include "exportjob.h"

#include <QFileInfo>

#include "backend.h"
#include "filepicker.h"
#include "takeoff.h"

ExportJob::ExportJob(Backend *backend, Takeoff *takeoff, FilePicker *picker, QObject *parent)
    : QObject(parent), m_backend(backend), m_takeoff(takeoff), m_picker(picker) {
    if (m_picker)
        connect(m_picker, &FilePicker::saveSelected, this, &ExportJob::exportTo);
}

void ExportJob::start() {
    if (m_backend->filePath().isEmpty() || !m_picker)
        return;
    if (m_takeoff->measurementTotal() == 0) {
        emit failed(tr("There are no quantities to export yet: measure something first."));
        return;
    }
    const QFileInfo source(m_backend->filePath());
    const QString suggested = source.absolutePath() + "/" + tr("%1 quantities.csv").arg(source.completeBaseName());
    m_picker->saveCsv(QUrl::fromLocalFile(suggested), tr("Export Quantities"));
}

void ExportJob::exportTo(const QUrl &url) {
    if (m_backend->filePath().isEmpty())
        return;
    QString output = url.toLocalFile();
    if (!output.endsWith(".csv", Qt::CaseInsensitive))
        output += ".csv";
    QString error;
    if (m_takeoff->exportCsv(output, m_backend->sheetLabels(), &error))
        emit finished(output);
    else
        emit failed(error);
}
