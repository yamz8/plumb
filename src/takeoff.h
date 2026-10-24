#pragma once

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QVariantList>
#include <QVector>

// What is being counted up: drywall in feet of wall, a slab in square feet,
// doors one by one. Every measurement belongs to one item, and an item's kind
// decides how its measurements are taken.
struct TakeoffItem {
    int id = 0;
    QString name;
    QString kind;              // "length", "area" or "count"
    QColor color;
    bool visible = true;
};

// One measurement on a sheet, in PDF points from the sheet's top left
// corner: a polyline for a length, a polygon for an area, a point for a count.
struct Measurement {
    int id = 0;
    int item = 0;
    int sheet = 0;
    QVector<QPointF> points;
};

// How long a PDF point is on site: one unit (a foot or a meter) spans
// pointsPerUnit points on the sheet.
struct SheetScale {
    double pointsPerUnit = 0;
    QString unit;              // "ft" or "m"
    QString label;             // 1/8" = 1'-0", 1:100, or "Calibrated"
    bool isSet() const { return pointsPerUnit > 0; }
    // Meters on site per point on the sheet.
    double metersPerPoint() const { return isSet() ? (unit == "ft" ? 0.3048 : 1.0) / pointsPerUnit : 0; }
};

// The takeoff of one drawing: its items, measurements, and sheet scales. It
// is kept apart from the PDF, which is never changed, keyed by a hash of its
// contents so it follows the file when it's renamed or moved. Every change
// saves at once.
class Takeoff : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY changed)
    Q_PROPERTY(int measurementTotal READ measurementTotal NOTIFY changed)
    Q_PROPERTY(QString units READ units WRITE setUnits NOTIFY changed)
    Q_PROPERTY(bool unexported READ unexported NOTIFY changed)

public:
    static constexpr int maxUndo = 200;

    explicit Takeoff(const QString &storageDirectory = {}, QObject *parent = nullptr);

    // Loads what's stored for this PDF; an empty path closes.
    bool open(const QString &pdfPath, int sheetCount);
    QString storagePath() const { return m_storagePath; }

    int revision() const { return m_revision; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    int itemCount() const { return int(m_items.size()); }
    int measurementTotal() const { return int(m_measurements.size()); }
    // "imperial" (feet) or "metric" (meters) for every total and the CSV.
    QString units() const { return m_units; }
    void setUnits(const QString &units);
    // Whether the quantities changed since they were last exported, or since
    // the drawing was opened.
    bool unexported() const { return m_unexported; }

    const QVector<TakeoffItem> &itemList() const { return m_items; }
    const TakeoffItem *item(int id) const;
    QVector<Measurement> measurementsOn(int sheet) const;
    SheetScale scaleFor(int sheet) const { return m_scales.value(sheet); }

    // Items, in order, each {id, name, kind, color, visible, key}.
    Q_INVOKABLE QVariantList items() const;
    Q_INVOKABLE QVariantMap itemInfo(int id) const;
    Q_INVOKABLE int addItem(const QString &name, const QString &kind, const QString &color = {});
    Q_INVOKABLE void updateItem(int id, const QString &name, const QString &kind, const QString &color);
    Q_INVOKABLE void setItemVisible(int id, bool visible);
    Q_INVOKABLE void removeItem(int id);
    // Moves an item to a new place in the list.
    Q_INVOKABLE void moveItem(int id, int index);
    // The next color no item uses yet, from a palette that reads on white paper.
    Q_INVOKABLE QString nextColor() const;
    Q_INVOKABLE static QStringList palette();

    Q_INVOKABLE int addMeasurement(int item, int sheet, const QVariantList &points);
    Q_INVOKABLE void removeMeasurement(int id);
    Q_INVOKABLE void setPoints(int id, const QVariantList &points);
    // Moves a measurement to another item of the same kind.
    Q_INVOKABLE void setMeasurementItem(int id, int item);
    // {id, item, sheet, kind, points, text}, or empty when there is none.
    Q_INVOKABLE QVariantMap measurement(int id) const;
    // The measurement under a point, {id, vertex}: vertex is the index of the
    // corner within tolerance points, or -1. id is -1 when nothing is there.
    Q_INVOKABLE QVariantMap hitTest(int sheet, const QPointF &point, qreal tolerance) const;
    // The nearest corner of a measurement on the sheet within tolerance
    // points, other than the one being edited, or the point itself.
    Q_INVOKABLE QPointF snap(int sheet, const QPointF &point, qreal tolerance, int exceptId = -1) const;
    Q_INVOKABLE int measurementCount(int item, int sheet = -1) const;
    Q_INVOKABLE int countOnSheet(int sheet) const;

    // An item's quantity in the chosen units, on one sheet or, with -1, on
    // all of them. Lengths and areas on sheets with no scale aren't counted.
    Q_INVOKABLE double quantity(int item, int sheet = -1) const;
    Q_INVOKABLE QString quantityText(int item, int sheet = -1) const;
    // How many of an item's measurements (or, with -1, of all of them) sit
    // on sheets with no scale, and so aren't in the totals.
    Q_INVOKABLE int unscaledCount(int item = -1) const;
    // The sheets an item is measured on, each {sheet, count, text, scaled}.
    Q_INVOKABLE QVariantList sheetBreakdown(int item) const;
    // A measurement's own quantity, as it's labeled on the plan.
    Q_INVOKABLE QString measurementText(int id) const;
    Q_INVOKABLE QString unitLabel(const QString &kind) const;
    // A length or area of points on a sheet, as it's labeled on the plan.
    Q_INVOKABLE QString formatPoints(int sheet, const QString &kind, const QVariantList &points) const;

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Sets a sheet's scale, or every sheet's.
    Q_INVOKABLE void setScale(int sheet, double pointsPerUnit, const QString &unit, const QString &label, bool allSheets);
    // Sets the scale from a line of this many points that is length units long.
    Q_INVOKABLE void calibrate(int sheet, qreal points, double length, const QString &unit, bool allSheets);
    Q_INVOKABLE QString scaleLabel(int sheet) const { return m_scales.value(sheet).label; }
    Q_INVOKABLE bool hasScale(int sheet) const { return m_scales.value(sheet).isSet(); }
    Q_INVOKABLE int scaledSheetCount() const { return int(m_scales.size()); }

    // A length typed in the unit: 12'-6", 12' 6", 12.5 or 150" in feet;
    // 3.2 or 3,2 in meters. -1 when it isn't one.
    Q_INVOKABLE static double parseLength(const QString &text, const QString &unit);
    // The common drawing scales, each {label, pointsPerUnit, unit}.
    Q_INVOKABLE static QVariantList presets();

    // Geometry, in points.
    static double pathLength(const QVector<QPointF> &points);
    static double polygonArea(const QVector<QPointF> &points);
    // A measurement's quantity in meters, square meters, or each; -1 when
    // its sheet has no scale.
    double baseQuantity(const Measurement &measurement) const;
    // Meters, square meters, or a count, in the chosen units.
    double toDisplay(const QString &kind, double base) const;
    QString formatQuantity(const QString &kind, double value) const;

    // The quantities as CSV: one row per item and sheet, then the item's
    // total. Numbers are plain (12.50), so every spreadsheet reads them.
    QString toCsv(const QStringList &sheetLabels) const;
    // Writes the CSV beside path and renames it over, so the file appears
    // whole or not at all.
    bool exportCsv(const QString &path, const QStringList &sheetLabels, QString *error);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &object);

signals:
    void changed();
    void failed(const QString &message);

private:
    void commit(const QJsonObject &before);
    void save();
    void touch();
    Measurement *findMeasurement(int id);
    TakeoffItem *findItem(int id);
    bool validPoints(const QString &kind, const QVector<QPointF> &points) const;

    QString m_directory;
    QString m_storagePath;
    int m_sheetCount = 0;
    QVector<TakeoffItem> m_items;
    QVector<Measurement> m_measurements;
    QHash<int, SheetScale> m_scales;
    QString m_units = "imperial";
    // Until someone picks the units, they follow the first scale that's set.
    bool m_unitsChosen = false;
    int m_nextId = 1;
    int m_revision = 0;
    bool m_unexported = false;
    QVector<QJsonObject> m_undo, m_redo;
};
