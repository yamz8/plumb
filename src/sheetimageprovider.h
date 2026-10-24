#pragma once

#include <QQuickImageProvider>

class Backend;

// Serves image://sheets/<generation>/<sheet> from the backend. The
// generation changes with every load, so Qt Quick never shows a cached
// render of a drawing that's no longer open.
class SheetImageProvider : public QQuickImageProvider {
public:
    explicit SheetImageProvider(Backend *backend);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    Backend *m_backend;
};
