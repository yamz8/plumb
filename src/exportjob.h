#pragma once

#include <QObject>
#include <QUrl>

class Backend;
class FilePicker;
class Takeoff;

// Exports the quantities from the window: asks where through the file
// picker, then writes the CSV.
class ExportJob : public QObject {
    Q_OBJECT

public:
    ExportJob(Backend *backend, Takeoff *takeoff, FilePicker *picker, QObject *parent = nullptr);

    Q_INVOKABLE void start();
    void exportTo(const QUrl &url);

signals:
    void finished(const QString &path);
    void failed(const QString &message);

private:
    Backend *m_backend;
    Takeoff *m_takeoff;
    FilePicker *m_picker;
};
