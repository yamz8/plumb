#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSizeF>
#include <QUrl>
#include <QVector>

class FilePicker;
class QPdfDocument;

// The open drawing: its sheets, which one is on screen, and what went wrong.
// Sheet images come from render(), which the image provider calls from Qt
// Quick's loader threads.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fileName READ fileName NOTIFY documentChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentChanged)
    Q_PROPERTY(int sheetCount READ sheetCount NOTIFY documentChanged)
    Q_PROPERTY(int generation READ generation NOTIFY documentChanged)
    Q_PROPERTY(int sheet READ sheet WRITE setSheet NOTIFY sheetChanged)
    Q_PROPERTY(QSizeF sheetSize READ sheetSize NOTIFY sheetChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    // Renders never exceed this many pixels on their long edge, which keeps a
    // full sheet at high zoom to about 250 MB rather than gigabytes.
    static constexpr int maxRenderEdge = 8000;

    explicit Backend(FilePicker *picker = nullptr, QObject *parent = nullptr);
    ~Backend() override;

    QString fileName() const { return m_fileName; }
    QString filePath() const { return m_filePath; }
    int sheetCount() const { return m_labels.size(); }
    int generation() const { return m_generation; }
    int sheet() const { return m_sheet; }
    QSizeF sheetSize() const { return sizeOf(m_sheet); }
    QString error() const { return m_error; }

    void setSheet(int sheet);

    Q_INVOKABLE QString sheetLabel(int sheet) const;
    Q_INVOKABLE QSizeF sizeOf(int sheet) const;
    QStringList sheetLabels() const;

    Q_INVOKABLE void openDrawing();
    Q_INVOKABLE bool load(const QUrl &url);
    Q_INVOKABLE void nextSheet() { setSheet(m_sheet + 1); }
    Q_INVOKABLE void previousSheet() { setSheet(m_sheet - 1); }
    Q_INVOKABLE void clearError() { setError({}); }
    // Shows a problem from elsewhere in the app in the window.
    Q_INVOKABLE void reportError(const QString &message) { setError(message); }

    // Thread-safe. id is "<generation>/<sheet>" for a whole sheet within
    // requested, or "<generation>/<sheet>/<W>x<H>/<x>,<y>,<w>,<h>" for a tile:
    // that clip of the sheet drawn W by H pixels.
    QImage render(const QString &id, const QSize &requested);

    // The pixel size a sheet of this many points renders at within requested.
    static QSize renderSize(const QSizeF &points, const QSize &requested);

signals:
    void documentChanged();
    void sheetChanged();
    void errorChanged();

private:
    void setError(const QString &error);
    QString describe(int pdfError) const;

    FilePicker *m_picker = nullptr;

    // Guards the document and the tables render() reads off the GUI thread.
    mutable QMutex m_lock;
    QPdfDocument *m_document = nullptr;
    QStringList m_labels;
    QVector<QSizeF> m_sizes;

    QString m_fileName, m_filePath;
    int m_generation = 0;
    int m_sheet = 0;
    QString m_error;
};
