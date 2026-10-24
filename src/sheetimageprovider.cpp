#include "sheetimageprovider.h"

#include "backend.h"

SheetImageProvider::SheetImageProvider(Backend *backend)
    // Large sheets take a moment to render; never block the window on one.
    : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading),
      m_backend(backend) {}

QImage SheetImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    const QImage image = m_backend->render(id, requestedSize);
    if (size)
        *size = image.size();
    return image;
}
