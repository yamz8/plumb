#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QVariantMap>

#include "takeoff.h"

// Draws one sheet's measurements over the view. It covers only the viewport
// and maps sheet points onto it itself, so lines stay the same width at any
// zoom and zooming far into a big sheet never asks for a texture the size of
// the whole zoomed sheet.
class TakeoffLayer : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Takeoff *takeoff READ takeoff WRITE setTakeoff NOTIFY changed)
    Q_PROPERTY(int sheet MEMBER m_sheet NOTIFY changed)
    // Where the sheet's top left corner is in this item, and its zoom.
    Q_PROPERTY(QPointF origin MEMBER m_origin NOTIFY changed)
    Q_PROPERTY(qreal pixelsPerPoint MEMBER m_pixelsPerPoint NOTIFY changed)
    Q_PROPERTY(int selected MEMBER m_selected NOTIFY changed)
    // The item being measured; its marks are drawn above the others.
    Q_PROPERTY(int activeItem MEMBER m_activeItem NOTIFY changed)
    Q_PROPERTY(bool labels MEMBER m_labels NOTIFY changed)
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY changed)
    // A measurement being moved or reshaped, drawn where it's going:
    // {id, points}, or empty.
    Q_PROPERTY(QVariantMap preview MEMBER m_preview NOTIFY changed)
    // A measurement being taken: {kind, color, points, cursor, closing}, or
    // empty. kind "calibrate" draws the calibration line.
    Q_PROPERTY(QVariantMap draft MEMBER m_draft NOTIFY changed)

public:
    explicit TakeoffLayer(QQuickItem *parent = nullptr);

    Takeoff *takeoff() const { return m_takeoff; }
    void setTakeoff(Takeoff *takeoff);
    void paint(QPainter *painter) override;

signals:
    void changed();

private:
    QPointF toView(const QPointF &point) const { return m_origin + point * m_pixelsPerPoint; }
    // A quantity to label, placed once every mark is drawn.
    struct Label {
        QPointF center;
        QString text;
        QColor color;
        int priority;
    };

    void paintMeasurement(QPainter *painter, const QString &kind, const QColor &color, const QVector<QPointF> &points,
                          bool selected, const QString &label, int priority, QVector<Label> *labels);
    void paintDraft(QPainter *painter, QVector<Label> *labels);
    QRectF labelRect(const QPainter *painter, const Label &label) const;
    void paintLabel(QPainter *painter, const QRectF &box, const Label &label);

    QPointer<Takeoff> m_takeoff;
    int m_sheet = -1;
    QPointF m_origin;
    qreal m_pixelsPerPoint = 1;
    int m_selected = -1;
    int m_activeItem = -1;
    bool m_labels = true;
    QColor m_accent{0xFF, 0xD6, 0x0A};
    QVariantMap m_preview;
    QVariantMap m_draft;
};
