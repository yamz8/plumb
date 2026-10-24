#include "takeoff.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineF>
#include <QLocale>
#include <QPolygonF>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace {

constexpr double pointsPerInch = 72;
constexpr double inchesPerMeter = 39.3700787;
constexpr double metersPerFoot = 0.3048;
const QStringList kinds{"length", "area", "count"};

QJsonArray pointsJson(const QVector<QPointF> &points) {
    QJsonArray list;
    for (const QPointF &p : points)
        list.append(QJsonArray{qRound(p.x() * 100) / 100.0, qRound(p.y() * 100) / 100.0});
    return list;
}

QVector<QPointF> toPoints(const QVariantList &list) {
    QVector<QPointF> points;
    for (const QVariant &point : list)
        points.append(point.toPointF());
    return points;
}

QVariantList toVariants(const QVector<QPointF> &points) {
    QVariantList list;
    for (const QPointF &p : points)
        list.append(p);
    return list;
}

// Distance from p to the segment ab.
qreal distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b) {
    const QPointF ab = b - a;
    const qreal length = QPointF::dotProduct(ab, ab);
    const qreal t = length > 0 ? qBound(0.0, QPointF::dotProduct(p - a, ab) / length, 1.0) : 0;
    return QLineF(p, a + t * ab).length();
}

// A field as a spreadsheet reads it: quoted when it must be, and never taken
// for a formula, since item names come from whoever typed them.
QString csvField(QString value) {
    if (!value.isEmpty() && QString("=+-@").contains(value.front()))
        value.prepend('\'');
    if (value.contains(',') || value.contains('"') || value.contains('\n') || value.contains('\r'))
        value = '"' + value.replace("\"", "\"\"") + '"';
    return value;
}

QString plainNumber(double value, int decimals) {
    return QString::number(value, 'f', decimals);
}

}

Takeoff::Takeoff(const QString &storageDirectory, QObject *parent)
    : QObject(parent),
      m_directory(storageDirectory.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/plumb/takeoffs"
                      : storageDirectory) {}

bool Takeoff::open(const QString &pdfPath, int sheetCount) {
    m_items.clear();
    m_measurements.clear();
    m_scales.clear();
    m_undo.clear();
    m_redo.clear();
    m_units = QLocale().measurementSystem() == QLocale::MetricSystem ? "metric" : "imperial";
    m_unitsChosen = false;
    m_nextId = 1;
    m_sheetCount = sheetCount;
    m_storagePath.clear();
    m_unexported = false;
    ++m_revision;

    QFile pdf(pdfPath);
    if (pdfPath.isEmpty() || !pdf.open(QIODevice::ReadOnly)) {
        emit changed();
        return pdfPath.isEmpty();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&pdf);
    m_storagePath = m_directory + "/" + hash.result().toHex() + ".json";

    QFile stored(m_storagePath);
    if (stored.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(stored.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            // Keep the unreadable file for whoever wants to look at it, and
            // start fresh beside it rather than write over it.
            stored.close();
            QFile::remove(m_storagePath + ".unreadable");
            QFile::rename(m_storagePath, m_storagePath + ".unreadable");
            emit failed(tr("The takeoff saved for %1 could not be read; it was set aside.")
                            .arg(QFileInfo(pdfPath).fileName()));
        } else {
            fromJson(document.object());
        }
    }
    emit changed();
    return true;
}

void Takeoff::setUnits(const QString &units) {
    if ((units != "imperial" && units != "metric") || (units == m_units && m_unitsChosen))
        return;
    m_units = units;
    m_unitsChosen = true;
    touch();
}

const TakeoffItem *Takeoff::item(int id) const {
    for (const TakeoffItem &item : m_items)
        if (item.id == id)
            return &item;
    return nullptr;
}

TakeoffItem *Takeoff::findItem(int id) {
    for (TakeoffItem &item : m_items)
        if (item.id == id)
            return &item;
    return nullptr;
}

Measurement *Takeoff::findMeasurement(int id) {
    for (Measurement &measurement : m_measurements)
        if (measurement.id == id)
            return &measurement;
    return nullptr;
}

QVector<Measurement> Takeoff::measurementsOn(int sheet) const {
    QVector<Measurement> list;
    for (const Measurement &measurement : m_measurements)
        if (measurement.sheet == sheet)
            list.append(measurement);
    return list;
}

QStringList Takeoff::palette() {
    // Distinct from each other and from black linework on white paper.
    return {"#E5484D", "#1E6BFF", "#30A46C", "#F76B15", "#8E4EC6",
            "#12A594", "#D6409F", "#9E6C41", "#3E63DD", "#7C8C1F"};
}

QString Takeoff::nextColor() const {
    const QStringList colors = palette();
    QHash<QString, int> uses;
    for (const TakeoffItem &item : m_items)
        ++uses[item.color.name(QColor::HexRgb).toUpper()];
    // The least used color, earliest in the palette first.
    QString best = colors.first();
    int fewest = INT_MAX;
    for (const QString &color : colors) {
        const int count = uses.value(color.toUpper());
        if (count < fewest) {
            fewest = count;
            best = color;
        }
    }
    return best;
}

QVariantMap Takeoff::itemInfo(int id) const {
    const TakeoffItem *found = item(id);
    if (!found)
        return {};
    const int index = int(found - m_items.data());
    return {{"id", found->id},
            {"name", found->name},
            {"kind", found->kind},
            {"color", found->color.name(QColor::HexRgb)},
            {"visible", found->visible},
            // Items one to nine are picked with the number keys.
            {"key", index < 9 ? QString::number(index + 1) : QString()}};
}

QVariantList Takeoff::items() const {
    QVariantList list;
    for (const TakeoffItem &item : m_items)
        list.append(itemInfo(item.id));
    return list;
}

int Takeoff::addItem(const QString &name, const QString &kind, const QString &color) {
    if (m_storagePath.isEmpty() || !kinds.contains(kind))
        return -1;
    const QString trimmed = name.trimmed();
    const QJsonObject before = toJson();
    TakeoffItem item{m_nextId++,
                     trimmed.isEmpty() ? tr("Item %1").arg(m_items.size() + 1) : trimmed,
                     kind,
                     QColor(color).isValid() ? QColor(color) : QColor(nextColor()),
                     true};
    m_items.append(item);
    commit(before);
    return item.id;
}

void Takeoff::updateItem(int id, const QString &name, const QString &kind, const QString &color) {
    TakeoffItem *found = findItem(id);
    if (!found)
        return;
    const QString trimmed = name.trimmed();
    // Measurements are taken for their kind: a length can't become an area.
    const QString nextKind = kinds.contains(kind) && measurementCount(id) == 0 ? kind : found->kind;
    const QColor nextColor = QColor(color).isValid() ? QColor(color) : found->color;
    if ((trimmed.isEmpty() || trimmed == found->name) && nextKind == found->kind && nextColor == found->color)
        return;
    const QJsonObject before = toJson();
    if (!trimmed.isEmpty())
        found->name = trimmed;
    found->kind = nextKind;
    found->color = nextColor;
    commit(before);
}

void Takeoff::setItemVisible(int id, bool visible) {
    TakeoffItem *found = findItem(id);
    if (!found || found->visible == visible)
        return;
    // Hiding an item only changes the view, so it isn't something to undo.
    found->visible = visible;
    ++m_revision;
    save();
    emit changed();
}

void Takeoff::removeItem(int id) {
    if (!item(id))
        return;
    const QJsonObject before = toJson();
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [id](const TakeoffItem &i) { return i.id == id; }),
                  m_items.end());
    m_measurements.erase(std::remove_if(m_measurements.begin(), m_measurements.end(),
                                        [id](const Measurement &m) { return m.item == id; }),
                         m_measurements.end());
    commit(before);
}

void Takeoff::moveItem(int id, int index) {
    const TakeoffItem *found = item(id);
    if (!found)
        return;
    const int from = int(found - m_items.data());
    index = qBound(0, index, int(m_items.size()) - 1);
    if (from == index)
        return;
    const QJsonObject before = toJson();
    m_items.move(from, index);
    commit(before);
}

bool Takeoff::validPoints(const QString &kind, const QVector<QPointF> &points) const {
    for (const QPointF &p : points)
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
            return false;
    if (kind == "count")
        return points.size() == 1;
    if (kind == "length")
        return points.size() >= 2 && pathLength(points) >= 0.5;
    return points.size() >= 3 && polygonArea(points) >= 0.5;
}

int Takeoff::addMeasurement(int itemId, int sheet, const QVariantList &list) {
    const TakeoffItem *found = item(itemId);
    const QVector<QPointF> points = toPoints(list);
    if (m_storagePath.isEmpty() || !found || sheet < 0 || sheet >= m_sheetCount || !validPoints(found->kind, points))
        return -1;
    const QJsonObject before = toJson();
    const Measurement measurement{m_nextId++, itemId, sheet, points};
    m_measurements.append(measurement);
    // Measuring an item that's hidden shows it again, so the new mark isn't
    // made unseen.
    if (TakeoffItem *target = findItem(itemId))
        target->visible = true;
    commit(before);
    return measurement.id;
}

void Takeoff::removeMeasurement(int id) {
    if (!findMeasurement(id))
        return;
    const QJsonObject before = toJson();
    m_measurements.erase(std::remove_if(m_measurements.begin(), m_measurements.end(),
                                        [id](const Measurement &m) { return m.id == id; }),
                         m_measurements.end());
    commit(before);
}

void Takeoff::setPoints(int id, const QVariantList &list) {
    Measurement *found = findMeasurement(id);
    const QVector<QPointF> points = toPoints(list);
    if (!found || points == found->points)
        return;
    const TakeoffItem *owner = item(found->item);
    if (!owner || !validPoints(owner->kind, points))
        return;
    const QJsonObject before = toJson();
    found->points = points;
    commit(before);
}

void Takeoff::setMeasurementItem(int id, int itemId) {
    Measurement *found = findMeasurement(id);
    const TakeoffItem *target = item(itemId);
    if (!found || !target || found->item == itemId)
        return;
    const TakeoffItem *owner = item(found->item);
    if (!owner || owner->kind != target->kind)
        return;
    const QJsonObject before = toJson();
    found->item = itemId;
    if (TakeoffItem *shown = findItem(itemId))
        shown->visible = true;
    commit(before);
}

QVariantMap Takeoff::measurement(int id) const {
    for (const Measurement &m : m_measurements) {
        if (m.id != id)
            continue;
        const TakeoffItem *owner = item(m.item);
        return {{"id", m.id},
                {"item", m.item},
                {"sheet", m.sheet},
                {"kind", owner ? owner->kind : QString()},
                {"points", toVariants(m.points)},
                {"text", measurementText(id)}};
    }
    return {};
}

QVariantMap Takeoff::hitTest(int sheet, const QPointF &point, qreal tolerance) const {
    // Marks first, top to bottom: counts, lines, and the corners and edges of
    // areas. Only then the inside of an area, smallest first, so a room
    // inside a floor can still be picked.
    const QVector<Measurement> list = measurementsOn(sheet);
    for (int i = list.size() - 1; i >= 0; --i) {
        const Measurement &m = list.at(i);
        const TakeoffItem *owner = item(m.item);
        if (!owner || !owner->visible)
            continue;
        for (int v = 0; v < m.points.size(); ++v)
            if (QLineF(point, m.points.at(v)).length() <= tolerance * (owner->kind == "count" ? 1.4 : 1))
                return {{"id", m.id}, {"vertex", owner->kind == "count" ? -1 : v}};
        const int segments = owner->kind == "area" ? m.points.size() : m.points.size() - 1;
        for (int s = 0; s < segments; ++s)
            if (distanceToSegment(point, m.points.at(s), m.points.at((s + 1) % m.points.size())) <= tolerance)
                return {{"id", m.id}, {"vertex", -1}};
    }
    int best = -1;
    double smallest = 0;
    for (const Measurement &m : list) {
        const TakeoffItem *owner = item(m.item);
        if (!owner || !owner->visible || owner->kind != "area")
            continue;
        if (QPolygonF(m.points).containsPoint(point, Qt::OddEvenFill)) {
            const double area = polygonArea(m.points);
            if (best < 0 || area < smallest) {
                best = m.id;
                smallest = area;
            }
        }
    }
    return {{"id", best}, {"vertex", -1}};
}

QPointF Takeoff::snap(int sheet, const QPointF &point, qreal tolerance, int exceptId) const {
    QPointF best = point;
    qreal nearest = tolerance;
    for (const Measurement &m : m_measurements) {
        if (m.sheet != sheet || m.id == exceptId)
            continue;
        const TakeoffItem *owner = item(m.item);
        if (!owner || !owner->visible || owner->kind == "count")
            continue;
        for (const QPointF &corner : m.points) {
            const qreal distance = QLineF(point, corner).length();
            if (distance <= nearest) {
                nearest = distance;
                best = corner;
            }
        }
    }
    return best;
}

int Takeoff::measurementCount(int itemId, int sheet) const {
    int count = 0;
    for (const Measurement &m : m_measurements)
        count += m.item == itemId && (sheet < 0 || m.sheet == sheet);
    return count;
}

int Takeoff::countOnSheet(int sheet) const {
    int count = 0;
    for (const Measurement &m : m_measurements)
        count += m.sheet == sheet;
    return count;
}

double Takeoff::pathLength(const QVector<QPointF> &points) {
    double length = 0;
    for (int i = 1; i < points.size(); ++i)
        length += QLineF(points.at(i - 1), points.at(i)).length();
    return length;
}

// The shoelace formula, which holds for any simple polygon, convex or not.
double Takeoff::polygonArea(const QVector<QPointF> &points) {
    double twice = 0;
    for (int i = 0; i < points.size(); ++i) {
        const QPointF &a = points.at(i), &b = points.at((i + 1) % points.size());
        twice += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(twice) / 2;
}

double Takeoff::baseQuantity(const Measurement &measurement) const {
    const TakeoffItem *owner = item(measurement.item);
    if (!owner)
        return -1;
    if (owner->kind == "count")
        return 1;
    const double perPoint = m_scales.value(measurement.sheet).metersPerPoint();
    if (perPoint <= 0)
        return -1;
    return owner->kind == "length" ? pathLength(measurement.points) * perPoint
                                   : polygonArea(measurement.points) * perPoint * perPoint;
}

double Takeoff::toDisplay(const QString &kind, double base) const {
    if (kind == "count" || m_units == "metric")
        return base;
    return kind == "length" ? base / metersPerFoot : base / (metersPerFoot * metersPerFoot);
}

QString Takeoff::unitLabel(const QString &kind) const {
    // Units read the same in every language, as on the drawings.
    if (kind == "count")
        return tr("ea");
    if (m_units == "metric")
        return kind == "length" ? "m" : "m²";
    return kind == "length" ? "ft" : "sq ft";
}

QString Takeoff::formatQuantity(const QString &kind, double value) const {
    const int decimals = kind == "count" ? 0 : m_units == "metric" && kind == "length" ? 2 : 1;
    return QLocale().toString(value, 'f', decimals) + " " + unitLabel(kind);
}

double Takeoff::quantity(int itemId, int sheet) const {
    const TakeoffItem *owner = item(itemId);
    if (!owner)
        return 0;
    double total = 0;
    for (const Measurement &m : m_measurements) {
        if (m.item != itemId || (sheet >= 0 && m.sheet != sheet))
            continue;
        const double base = baseQuantity(m);
        if (base > 0)
            total += base;
    }
    return toDisplay(owner->kind, total);
}

QString Takeoff::quantityText(int itemId, int sheet) const {
    const TakeoffItem *owner = item(itemId);
    return owner ? formatQuantity(owner->kind, quantity(itemId, sheet)) : QString();
}

int Takeoff::unscaledCount(int itemId) const {
    int count = 0;
    for (const Measurement &m : m_measurements)
        count += (itemId < 0 || m.item == itemId) && baseQuantity(m) < 0;
    return count;
}

QVariantList Takeoff::sheetBreakdown(int itemId) const {
    QVariantList list;
    for (int sheet = 0; sheet < m_sheetCount; ++sheet) {
        const int count = measurementCount(itemId, sheet);
        if (count == 0)
            continue;
        const TakeoffItem *owner = item(itemId);
        const bool scaled = (owner && owner->kind == "count") || hasScale(sheet);
        list.append(QVariantMap{{"sheet", sheet}, {"count", count}, {"scaled", scaled},
                                {"text", scaled ? quantityText(itemId, sheet) : tr("No scale")}});
    }
    return list;
}

QString Takeoff::measurementText(int id) const {
    for (const Measurement &m : m_measurements) {
        if (m.id != id)
            continue;
        const TakeoffItem *owner = item(m.item);
        if (!owner)
            return {};
        return formatPoints(m.sheet, owner->kind, toVariants(m.points));
    }
    return {};
}

QString Takeoff::formatPoints(int sheet, const QString &kind, const QVariantList &list) const {
    const QVector<QPointF> points = toPoints(list);
    if (kind == "count")
        return {};
    const double perPoint = m_scales.value(sheet).metersPerPoint();
    if (perPoint <= 0)
        return tr("No scale");
    if (kind == "length") {
        const double meters = pathLength(points) * perPoint;
        if (m_units == "metric")
            return formatQuantity(kind, meters);
        // Feet and inches, to the nearest inch, as dimensions read on drawings.
        const qint64 inches = qRound64(meters / metersPerFoot * 12);
        return QString("%1'-%2\"").arg(QLocale().toString(inches / 12)).arg(inches % 12);
    }
    return formatQuantity(kind, toDisplay(kind, polygonArea(points) * perPoint * perPoint));
}

void Takeoff::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo.append(toJson());
    fromJson(m_undo.takeLast());
    touch();
}

void Takeoff::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append(toJson());
    fromJson(m_redo.takeLast());
    touch();
}

void Takeoff::setScale(int sheet, double pointsPerUnit, const QString &unit, const QString &label, bool allSheets) {
    if (pointsPerUnit <= 0 || !std::isfinite(pointsPerUnit) || (unit != "ft" && unit != "m") || m_storagePath.isEmpty()
        || (!allSheets && (sheet < 0 || sheet >= m_sheetCount)))
        return;
    const QJsonObject before = toJson();
    const bool first = m_scales.isEmpty();
    const SheetScale scale{pointsPerUnit, unit, label};
    if (allSheets)
        for (int i = 0; i < m_sheetCount; ++i)
            m_scales[i] = scale;
    else
        m_scales[sheet] = scale;
    // Totals read in the units the drawings are in, unless someone chose.
    if (first && !m_unitsChosen)
        m_units = unit == "m" ? "metric" : "imperial";
    commit(before);
}

void Takeoff::calibrate(int sheet, qreal points, double length, const QString &unit, bool allSheets) {
    if (points <= 0 || length <= 0)
        return;
    setScale(sheet, points / length, unit, tr("Calibrated"), allSheets);
}

double Takeoff::parseLength(const QString &text, const QString &unit) {
    QString value = text.trimmed();
    value.replace(',', '.');
    bool ok = false;
    if (unit == "m") {
        value.remove(QRegularExpression("\\s*m$"));
        const double meters = value.toDouble(&ok);
        return ok && meters > 0 ? meters : -1;
    }
    // Feet and inches, either part optional, inches possibly fractional.
    static const QRegularExpression imperial(
        "^(?:(\\d+(?:\\.\\d+)?)\\s*(?:'|ft)?)?\\s*-?\\s*(?:(\\d+(?:\\.\\d+)?)\\s*(?:\"|in))?$");
    if (value.contains('\'') || value.contains('"') || value.contains("ft") || value.contains("in")) {
        // A number with only an inch mark is all inches.
        if (!value.contains('\'') && !value.contains("ft")) {
            QString inches = value;
            inches.remove(QRegularExpression("\\s*(\"|in)$"));
            const double total = inches.toDouble(&ok) / 12;
            return ok && total > 0 ? total : -1;
        }
        const auto match = imperial.match(value);
        if (!match.hasMatch() || (match.captured(1).isEmpty() && match.captured(2).isEmpty()))
            return -1;
        const double feet = match.captured(1).toDouble();
        const double inches = match.captured(2).toDouble();
        const double total = feet + inches / 12;
        return total > 0 ? total : -1;
    }
    const double feet = value.toDouble(&ok);
    return ok && feet > 0 ? feet : -1;
}

QVariantList Takeoff::presets() {
    QVariantList list;
    // Architectural and engineering: a fraction of an inch on paper per foot
    // on site, or an inch per so many feet.
    const QList<std::pair<QString, double>> imperial{
        {"1/16\" = 1'-0\"", 1.0 / 16}, {"3/32\" = 1'-0\"", 3.0 / 32}, {"1/8\" = 1'-0\"", 1.0 / 8},
        {"3/16\" = 1'-0\"", 3.0 / 16}, {"1/4\" = 1'-0\"", 1.0 / 4}, {"3/8\" = 1'-0\"", 3.0 / 8},
        {"1/2\" = 1'-0\"", 1.0 / 2}, {"3/4\" = 1'-0\"", 3.0 / 4}, {"1\" = 1'-0\"", 1},
        {"1\" = 10'", 0.1}, {"1\" = 20'", 0.05}, {"1\" = 30'", 1.0 / 30}, {"1\" = 40'", 0.025},
        {"1\" = 50'", 0.02}, {"1\" = 100'", 0.01}};
    for (const auto &[label, inches] : imperial)
        list.append(QVariantMap{{"label", label}, {"pointsPerUnit", inches * pointsPerInch}, {"unit", "ft"}});
    // Metric: one meter on site is 1/n meters on paper.
    for (const int n : {10, 20, 25, 50, 75, 100, 200, 250, 500, 1000})
        list.append(QVariantMap{{"label", QString("1:%1").arg(n)},
                                {"pointsPerUnit", inchesPerMeter * pointsPerInch / n},
                                {"unit", "m"}});
    return list;
}

QString Takeoff::toCsv(const QStringList &sheetLabels) const {
    const auto kindName = [](const QString &kind) {
        return kind == "length" ? tr("Length") : kind == "area" ? tr("Area") : tr("Count");
    };
    // ASCII units, so a spreadsheet that guesses the encoding still reads them.
    const auto csvUnit = [this](const QString &kind) {
        if (kind == "count")
            return QString("ea");
        if (m_units == "metric")
            return kind == "length" ? QString("m") : QString("m2");
        return kind == "length" ? QString("ft") : QString("sq ft");
    };
    QStringList lines{QStringList{tr("Item"), tr("Kind"), tr("Sheet"), tr("Measurements"), tr("Quantity"), tr("Unit")}
                          .join(',')};
    for (const TakeoffItem &item : m_items) {
        const int decimals = item.kind == "count" ? 0 : 2;
        const QString name = csvField(item.name), kind = csvField(kindName(item.kind));
        for (int sheet = 0; sheet < m_sheetCount; ++sheet) {
            const int count = measurementCount(item.id, sheet);
            if (count == 0)
                continue;
            const bool scaled = item.kind == "count" || hasScale(sheet);
            lines.append(QStringList{name, kind, csvField(sheetLabels.value(sheet, QString::number(sheet + 1))),
                                     QString::number(count),
                                     scaled ? plainNumber(quantity(item.id, sheet), decimals) : QString(),
                                     scaled ? csvUnit(item.kind) : csvField(tr("No scale"))}
                             .join(','));
        }
        lines.append(QStringList{name, kind, csvField(tr("All sheets")), QString::number(measurementCount(item.id)),
                                 plainNumber(quantity(item.id), decimals), csvUnit(item.kind)}
                         .join(','));
    }
    return lines.join("\r\n") + "\r\n";
}

bool Takeoff::exportCsv(const QString &path, const QStringList &sheetLabels, QString *error) {
    QSaveFile file(path);
    const QByteArray csv = toCsv(sheetLabels).toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(csv) != csv.size() || !file.commit()) {
        if (error)
            *error = tr("Could not write %1.").arg(path);
        return false;
    }
    if (m_unexported) {
        m_unexported = false;
        emit changed();
    }
    return true;
}

QJsonObject Takeoff::toJson() const {
    QJsonArray items;
    for (const TakeoffItem &item : m_items)
        items.append(QJsonObject{{"id", item.id}, {"name", item.name}, {"kind", item.kind},
                                 {"color", item.color.name(QColor::HexRgb)}, {"visible", item.visible}});
    QJsonArray measurements;
    for (const Measurement &m : m_measurements)
        // Sheets are numbered from 1, as everywhere else people see them.
        measurements.append(QJsonObject{{"id", m.id}, {"item", m.item}, {"sheet", m.sheet + 1},
                                        {"points", pointsJson(m.points)}});
    QJsonObject scales;
    for (int sheet = 0; sheet < m_sheetCount; ++sheet) {
        const SheetScale scale = m_scales.value(sheet);
        if (scale.isSet())
            scales[QString::number(sheet + 1)] =
                QJsonObject{{"points_per_unit", scale.pointsPerUnit}, {"unit", scale.unit}, {"label", scale.label}};
    }
    QJsonObject object{{"version", 1}, {"next_id", m_nextId}, {"units", m_units}, {"items", items},
                       {"measurements", measurements}, {"scales", scales}};
    if (m_unitsChosen)
        object["units_chosen"] = true;
    return object;
}

void Takeoff::fromJson(const QJsonObject &object) {
    m_items.clear();
    m_measurements.clear();
    m_scales.clear();
    m_nextId = qMax(1, object["next_id"].toInt(1));
    const QString units = object["units"].toString();
    if (units == "imperial" || units == "metric")
        m_units = units;
    m_unitsChosen = object["units_chosen"].toBool();

    for (const auto &value : object["items"].toArray()) {
        const QJsonObject entry = value.toObject();
        TakeoffItem item{entry["id"].toInt(), entry["name"].toString(), entry["kind"].toString(),
                         QColor(entry["color"].toString()), entry["visible"].toBool(true)};
        if (item.id <= 0 || !kinds.contains(item.kind) || this->item(item.id))
            continue;
        if (!item.color.isValid())
            item.color = QColor(nextColor());
        m_nextId = qMax(m_nextId, item.id + 1);
        m_items.append(item);
    }
    for (const auto &value : object["measurements"].toArray()) {
        const QJsonObject entry = value.toObject();
        Measurement m{entry["id"].toInt(), entry["item"].toInt(), entry["sheet"].toInt() - 1, {}};
        for (const auto &point : entry["points"].toArray())
            m.points.append(QPointF(point.toArray().at(0).toDouble(), point.toArray().at(1).toDouble()));
        const TakeoffItem *owner = item(m.item);
        if (m.id <= 0 || !owner || m.sheet < 0 || m.sheet >= m_sheetCount || !validPoints(owner->kind, m.points))
            continue;
        m_nextId = qMax(m_nextId, m.id + 1);
        m_measurements.append(m);
    }
    const QJsonObject scales = object["scales"].toObject();
    for (auto it = scales.begin(); it != scales.end(); ++it) {
        const int sheet = it.key().toInt() - 1;
        const QJsonObject scale = it.value().toObject();
        const double perUnit = scale["points_per_unit"].toDouble();
        const QString unit = scale["unit"].toString();
        if (sheet >= 0 && sheet < m_sheetCount && perUnit > 0 && (unit == "ft" || unit == "m"))
            m_scales[sheet] = {perUnit, unit, scale["label"].toString()};
    }
}

void Takeoff::commit(const QJsonObject &before) {
    m_undo.append(before);
    if (m_undo.size() > maxUndo)
        m_undo.removeFirst();
    m_redo.clear();
    touch();
}

void Takeoff::touch() {
    ++m_revision;
    m_unexported = true;
    save();
    emit changed();
}

// Written beside the target and renamed over it, so a crash mid-save never
// leaves half a file.
void Takeoff::save() {
    if (m_storagePath.isEmpty())
        return;
    QSaveFile file(m_storagePath);
    if (!QDir().mkpath(m_directory) || !file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Compact)) < 0 || !file.commit())
        emit failed(tr("Could not save the takeoff to %1.").arg(m_storagePath));
}
