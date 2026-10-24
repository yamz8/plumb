#include "backend.h"

#include <QFileInfo>
#include <QMutexLocker>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <utility>

#include "filepicker.h"

Backend::Backend(FilePicker *picker, QObject *parent)
    : QObject(parent), m_picker(picker), m_document(new QPdfDocument(this)) {
    if (m_picker) {
        connect(m_picker, &FilePicker::openSelected, this, &Backend::load);
        connect(m_picker, &FilePicker::failed, this, &Backend::setError);
    }
}

Backend::~Backend() {
    // Loader threads may still be rendering; wait for the last one.
    QMutexLocker locker(&m_lock);
}

void Backend::setSheet(int sheet) {
    sheet = qBound(0, sheet, qMax(0, sheetCount() - 1));
    if (sheet == m_sheet)
        return;
    m_sheet = sheet;
    emit sheetChanged();
}

QString Backend::sheetLabel(int sheet) const {
    if (sheet < 0 || sheet >= m_labels.size())
        return {};
    return m_labels.at(sheet).isEmpty() ? QString::number(sheet + 1) : m_labels.at(sheet);
}

QStringList Backend::sheetLabels() const {
    QStringList labels;
    for (int sheet = 0; sheet < sheetCount(); ++sheet)
        labels.append(sheetLabel(sheet));
    return labels;
}

QSizeF Backend::sizeOf(int sheet) const {
    return sheet >= 0 && sheet < m_sizes.size() ? m_sizes.at(sheet) : QSizeF();
}

void Backend::openDrawing() {
    if (m_picker)
        m_picker->openPdf(tr("Open Drawing"));
}

QString Backend::describe(int pdfError) const {
    switch (QPdfDocument::Error(pdfError)) {
    case QPdfDocument::Error::FileNotFound: return tr("The file could not be found.");
    case QPdfDocument::Error::InvalidFileFormat: return tr("The file is not a valid PDF.");
    case QPdfDocument::Error::IncorrectPassword: return tr("The PDF is password protected.");
    case QPdfDocument::Error::UnsupportedSecurityScheme: return tr("The PDF uses an unsupported security scheme.");
    case QPdfDocument::Error::DataNotYetAvailable: return tr("The PDF is not fully downloaded yet.");
    default: return tr("The PDF could not be read.");
    }
}

// Loads into a fresh document and swaps it in only once it opened, so a file
// that fails to load leaves whatever was open on screen.
bool Backend::load(const QUrl &url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const QString name = QFileInfo(path).fileName();
    auto *document = new QPdfDocument(this);
    const auto status = document->load(path);
    if (status != QPdfDocument::Error::None) {
        delete document;
        setError(tr("Could not open %1: %2").arg(name, describe(int(status))));
        return false;
    }
    if (document->pageCount() == 0) {
        delete document;
        setError(tr("%1 has no sheets.").arg(name));
        return false;
    }

    QStringList labels;
    QVector<QSizeF> sizes;
    for (int page = 0; page < document->pageCount(); ++page) {
        labels.append(document->pageLabel(page));
        sizes.append(document->pagePointSize(page));
    }

    QPdfDocument *replaced;
    {
        QMutexLocker locker(&m_lock);
        replaced = std::exchange(m_document, document);
        m_fileName = name;
        m_filePath = QFileInfo(path).absoluteFilePath();
        m_labels = labels;
        m_sizes = sizes;
    }
    delete replaced;
    ++m_generation;
    m_sheet = 0;
    setError({});
    emit documentChanged();
    emit sheetChanged();
    return true;
}

QSize Backend::renderSize(const QSizeF &points, const QSize &requested) {
    if (points.isEmpty())
        return {};
    QSizeF size = points;
    if (requested.width() > 0 && requested.height() > 0)
        size = points.scaled(QSizeF(requested), Qt::KeepAspectRatio);
    else if (requested.width() > 0)
        size = QSizeF(requested.width(), points.height() * requested.width() / points.width());
    else if (requested.height() > 0)
        size = QSizeF(points.width() * requested.height() / points.height(), requested.height());
    else
        size = points.scaled(QSizeF(1600, 1600), Qt::KeepAspectRatio);
    const double longEdge = qMax(size.width(), size.height());
    if (longEdge > maxRenderEdge)
        size *= maxRenderEdge / longEdge;
    return QSize(qMax(1, qRound(size.width())), qMax(1, qRound(size.height())));
}

namespace {

// Renders the clip of a page drawn at full size. Qt PDF (6.11) scales a
// clipped render by (width - 1) / width, from QRect::right(); asking for a
// page that much bigger cancels it, so tiles line up with whole-page renders.
QImage renderClip(QPdfDocument *document, int page, const QSize &full, const QRect &clip) {
    if (clip == QRect(QPoint(), full) || clip.width() < 2 || clip.height() < 2)
        return document->render(page, full);
    QPdfDocumentRenderOptions options;
    options.setScaledSize(QSize(qRound(full.width() * clip.width() / (clip.width() - 1.0)),
                                qRound(full.height() * clip.height() / (clip.height() - 1.0))));
    options.setScaledClipRect(clip);
    return document->render(page, clip.size(), options);
}

}

QImage Backend::render(const QString &id, const QSize &requested) {
    const QStringList parts = id.split('/');
    if (parts.size() != 2 && parts.size() != 4)
        return {};
    const int sheet = parts.at(1).toInt();
    QSize full;
    QRect clip;
    if (parts.size() == 4) {
        const QStringList size = parts.at(2).split('x'), rect = parts.at(3).split(',');
        if (size.size() != 2 || rect.size() != 4)
            return {};
        full = QSize(size[0].toInt(), size[1].toInt());
        clip = QRect(rect[0].toInt(), rect[1].toInt(), rect[2].toInt(), rect[3].toInt());
        if (full.isEmpty() || full.width() > 200000 || full.height() > 200000 || clip.isEmpty()
            || clip.width() > 4096 || clip.height() > 4096)
            return {};
    }

    QMutexLocker locker(&m_lock);
    if (sheet < 0 || sheet >= m_sizes.size())
        return {};
    if (full.isEmpty()) {
        full = renderSize(m_sizes.at(sheet), requested);
        clip = QRect(QPoint(), full);
    }
    return renderClip(m_document, sheet, full, clip);
}

void Backend::setError(const QString &error) {
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}
