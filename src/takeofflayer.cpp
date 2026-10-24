#include "takeofflayer.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>

namespace {

QVector<QPointF> pointsOf(const QVariant &value) {
    QVector<QPointF> points;
    for (const QVariant &point : value.toList())
        points.append(point.toPointF());
    return points;
}

// The point halfway along a polyline, where its length is labeled.
QPointF midway(const QVector<QPointF> &points) {
    const double half = Takeoff::pathLength(points) / 2;
    double walked = 0;
    for (int i = 1; i < points.size(); ++i) {
        const QLineF segment(points.at(i - 1), points.at(i));
        if (walked + segment.length() >= half && segment.length() > 0)
            return segment.pointAt((half - walked) / segment.length());
        walked += segment.length();
    }
    return points.isEmpty() ? QPointF() : points.first();
}

// The centroid of a polygon, where its area is labeled; the middle of its
// bounds when the centroid falls outside it, as it can for an L or a U.
QPointF labelPoint(const QVector<QPointF> &points) {
    const QPolygonF polygon(points);
    double twice = 0, cx = 0, cy = 0;
    for (int i = 0; i < points.size(); ++i) {
        const QPointF &a = points.at(i), &b = points.at((i + 1) % points.size());
        const double cross = a.x() * b.y() - b.x() * a.y();
        twice += cross;
        cx += (a.x() + b.x()) * cross;
        cy += (a.y() + b.y()) * cross;
    }
    if (qAbs(twice) > 1e-9) {
        const QPointF centroid(cx / (3 * twice), cy / (3 * twice));
        if (polygon.containsPoint(centroid, Qt::OddEvenFill))
            return centroid;
    }
    return polygon.boundingRect().center();
}

void paintHandles(QPainter *painter, const QVector<QPointF> &points, const QColor &color) {
    painter->setPen(QPen(color, 1.5));
    painter->setBrush(Qt::white);
    for (const QPointF &p : points)
        painter->drawRect(QRectF(p - QPointF(4, 4), QSizeF(8, 8)));
}

}

TakeoffLayer::TakeoffLayer(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &TakeoffLayer::changed, this, [this] { update(); });
}

void TakeoffLayer::setTakeoff(Takeoff *takeoff) {
    if (m_takeoff == takeoff)
        return;
    if (m_takeoff)
        disconnect(m_takeoff, nullptr, this, nullptr);
    m_takeoff = takeoff;
    if (m_takeoff)
        connect(m_takeoff, &Takeoff::changed, this, [this] { update(); });
    emit changed();
}

static QFont labelFont(const QPainter *painter) {
    QFont font = painter->font();
    font.setPixelSize(11);
    font.setWeight(QFont::DemiBold);
    return font;
}

QRectF TakeoffLayer::labelRect(const QPainter *painter, const Label &label) const {
    const QFontMetricsF metrics(labelFont(painter));
    const qreal width = metrics.horizontalAdvance(label.text) + 12, height = metrics.height() + 4;
    return QRectF(label.center - QPointF(width / 2, height / 2), QSizeF(width, height));
}

void TakeoffLayer::paintLabel(QPainter *painter, const QRectF &box, const Label &label) {
    painter->setFont(labelFont(painter));
    painter->setPen(QPen(label.color, 1));
    painter->setBrush(QColor(0x17, 0x17, 0x1a, 235));
    painter->drawRoundedRect(box, 3, 3);
    painter->setPen(Qt::white);
    painter->drawText(box, Qt::AlignCenter, label.text);
}

void TakeoffLayer::paintMeasurement(QPainter *painter, const QString &kind, const QColor &color,
                                    const QVector<QPointF> &sheetPoints, bool selected, const QString &label,
                                    int priority, QVector<Label> *labels) {
    QVector<QPointF> points;
    for (const QPointF &p : sheetPoints)
        points.append(toView(p));
    if (points.isEmpty())
        return;

    if (kind == "count") {
        const QPointF p = points.first();
        if (selected) {
            painter->setPen(QPen(color, 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(p, 12, 12);
        }
        painter->setPen(QPen(Qt::white, 2));
        painter->setBrush(color);
        painter->drawEllipse(p, 7, 7);
        return;
    }

    if (kind == "area") {
        QColor fill = color;
        fill.setAlpha(selected ? 90 : 60);
        painter->setPen(QPen(color, selected ? 3 : 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(fill);
        painter->drawPolygon(QPolygonF(points));
    } else {
        painter->setPen(QPen(color, selected ? 4.5 : 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPolyline(QPolygonF(points));
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        for (const QPointF &p : {points.first(), points.last()})
            painter->drawEllipse(p, 4, 4);
    }
    if (selected)
        paintHandles(painter, points, color);
    if (!label.isEmpty())
        labels->append({kind == "area" ? labelPoint(points) : midway(points), label, color, priority});
}

void TakeoffLayer::paintDraft(QPainter *painter, QVector<Label> *labels) {
    const QString kind = m_draft.value("kind").toString();
    QVector<QPointF> sheetPoints = pointsOf(m_draft.value("points"));
    if (sheetPoints.isEmpty())
        return;
    const bool hasCursor = m_draft.contains("cursor");
    const QPointF cursor = m_draft.value("cursor").toPointF();
    const bool closing = m_draft.value("closing").toBool();
    const QColor color = kind == "calibrate" ? m_accent : QColor(m_draft.value("color").toString());

    QVector<QPointF> points;
    for (const QPointF &p : sheetPoints)
        points.append(toView(p));
    const QPointF end = closing ? points.first() : toView(cursor);
    QVector<QPointF> withCursor = points;
    if (hasCursor)
        withCursor.append(end);

    if (kind == "area" && withCursor.size() >= 3) {
        QColor fill = color;
        fill.setAlpha(45);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawPolygon(QPolygonF(withCursor));
        // The edge that will close the shape.
        painter->setPen(QPen(color, 1.5, Qt::DashLine));
        painter->drawLine(withCursor.last(), withCursor.first());
    }
    painter->setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPolyline(QPolygonF(points));
    if (hasCursor) {
        painter->setPen(QPen(color, 2, Qt::DashLine, Qt::RoundCap));
        painter->drawLine(points.last(), end);
    }
    paintHandles(painter, points, color);
    if (closing) {
        painter->setPen(QPen(color, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(points.first(), 10, 10);
    }

    // The running length or area, beside the pointer.
    if (m_takeoff && kind != "calibrate" && hasCursor) {
        QVariantList list;
        for (const QPointF &p : sheetPoints)
            list.append(p);
        if (!closing)
            list.append(cursor);
        if ((kind == "length" && list.size() >= 2) || (kind == "area" && list.size() >= 3)) {
            labels->append({end + QPointF(0, -22), m_takeoff->formatPoints(m_sheet, kind, list), color, 3});
        }
    }
}

void TakeoffLayer::paint(QPainter *painter) {
    if (!m_takeoff || m_sheet < 0)
        return;
    painter->setRenderHint(QPainter::Antialiasing);

    // Areas under lines under counts, and the item being measured above the
    // rest, so the marks being worked on are never buried.
    struct Entry {
        Measurement measurement;
        const TakeoffItem *item;
    };
    QVector<Entry> entries;
    for (const Measurement &m : m_takeoff->measurementsOn(m_sheet)) {
        const TakeoffItem *item = m_takeoff->item(m.item);
        if (item && item->visible)
            entries.append({m, item});
    }
    const auto layer = [this](const Entry &e) {
        const int kind = e.item->kind == "area" ? 0 : e.item->kind == "length" ? 1 : 2;
        return kind * 2 + (e.item->id == m_activeItem) + (e.measurement.id == m_selected) * 10;
    };
    std::stable_sort(entries.begin(), entries.end(),
                     [&](const Entry &a, const Entry &b) { return layer(a) < layer(b); });

    const int previewId = m_preview.value("id", -1).toInt();
    QVector<Label> labels;
    for (const Entry &entry : entries) {
        const Measurement &m = entry.measurement;
        const bool selected = m.id == m_selected;
        QVector<QPointF> points = m.points;
        QString label;
        if (m.id == previewId) {
            points = pointsOf(m_preview.value("points"));
            QVariantList list;
            for (const QPointF &p : points)
                list.append(p);
            label = m_takeoff->formatPoints(m_sheet, entry.item->kind, list);
        } else if (m_labels || selected) {
            label = m_takeoff->measurementText(m.id);
        }
        const int priority = m.id == previewId || selected ? 2 : entry.item->id == m_activeItem ? 1 : 0;
        paintMeasurement(painter, entry.item->kind, entry.item->color, points, selected, label, priority, &labels);
    }
    paintDraft(painter, &labels);

    // Labels go on top of every mark. Where two would overlap, the one being
    // worked on wins and the other waits until there's room, or L hides them.
    std::stable_sort(labels.begin(), labels.end(), [](const Label &a, const Label &b) { return a.priority > b.priority; });
    QVector<QRectF> placed;
    for (const Label &label : labels) {
        const QRectF box = labelRect(painter, label);
        const bool clear = std::none_of(placed.begin(), placed.end(),
                                        [&](const QRectF &other) { return other.adjusted(-2, -2, 2, 2).intersects(box); });
        if (!clear && label.priority < 2)
            continue;
        placed.append(box);
        paintLabel(painter, box, label);
    }
}
