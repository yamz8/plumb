#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "cli.h"
#include "exportjob.h"
#include "filepicker.h"
#include "takeoff.h"
#include "theme.h"
#include "translator.h"

// Hands back a chosen file instead of showing the portal's dialog.
class FakePicker : public FilePicker {
    Q_OBJECT

public:
    QUrl next;
    QString lastTitle;
    QUrl lastSuggested;
    void openPdf(const QString &title) override {
        lastTitle = title;
        emit openSelected(next);
    }
    void saveCsv(const QUrl &suggested, const QString &title) override {
        lastTitle = title;
        lastSuggested = suggested;
        emit saveSelected(next);
    }
};

// One foot is 18 points at 1/4" = 1'-0".
constexpr double quarterInch = 18;

class PlumbTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // The command line keeps takeoffs in the user's data folder; point it
        // somewhere disposable.
        QVERIFY(m_home.isValid());
        qputenv("XDG_DATA_HOME", QFile::encodeName(m_home.path()));
    }

    void geometryOfLinesAndAreas()
    {
        QCOMPARE(Takeoff::pathLength({{0, 0}, {30, 40}, {30, 50}}), 60.0);
        // An L: a 20 by 10 bar and a 10 by 10 leg, drawn either way round.
        const QVector<QPointF> l{{0, 0}, {20, 0}, {20, 10}, {10, 10}, {10, 20}, {0, 20}};
        QCOMPARE(Takeoff::polygonArea(l), 300.0);
        QVector<QPointF> reversed(l.rbegin(), l.rend());
        QCOMPARE(Takeoff::polygonArea(reversed), 300.0);
    }

    void quantitiesFollowTheSheetScale()
    {
        QTemporaryDir dir;
        Takeoff takeoff(dir.path() + "/takeoffs");
        QVERIFY(takeoff.open(writeSet(dir.filePath("set.pdf"), 2), 2));
        const int wall = takeoff.addItem("Drywall", "length");
        const int slab = takeoff.addItem("Slab", "area");
        const int doors = takeoff.addItem("Doors", "count");
        QVERIFY(wall > 0 && slab > 0 && doors > 0);

        // Ten feet of wall, a 10 by 10 slab, and two doors on sheet 1;
        // another five feet on sheet 2, which has no scale yet.
        QVERIFY(takeoff.addMeasurement(wall, 0, points({{100, 100}, {100 + 10 * quarterInch, 100}})) > 0);
        QVERIFY(takeoff.addMeasurement(slab, 0, points({{0, 0}, {180, 0}, {180, 180}, {0, 180}})) > 0);
        QVERIFY(takeoff.addMeasurement(doors, 0, points({{50, 50}})) > 0);
        QVERIFY(takeoff.addMeasurement(doors, 1, points({{60, 60}})) > 0);
        QVERIFY(takeoff.addMeasurement(wall, 1, points({{0, 0}, {5 * quarterInch, 0}})) > 0);

        QCOMPARE(takeoff.quantity(wall), 0.0);
        QCOMPARE(takeoff.quantity(doors), 2.0);
        QCOMPARE(takeoff.unscaledCount(wall), 2);
        QCOMPARE(takeoff.unscaledCount(doors), 0);

        takeoff.setScale(0, quarterInch, "ft", "1/4\" = 1'-0\"", false);
        QCOMPARE(takeoff.units(), QString("imperial"));
        QCOMPARE(takeoff.quantity(wall), 10.0);
        QCOMPARE(takeoff.quantity(wall, 0), 10.0);
        QCOMPARE(takeoff.quantity(wall, 1), 0.0);
        QCOMPARE(takeoff.quantity(slab), 100.0);
        QCOMPARE(takeoff.unscaledCount(), 1);

        takeoff.setScale(1, quarterInch, "ft", "1/4\" = 1'-0\"", false);
        QCOMPARE(takeoff.quantity(wall), 15.0);
        QCOMPARE(takeoff.unscaledCount(), 0);

        // The same quantities in meters.
        takeoff.setUnits("metric");
        QVERIFY(qAbs(takeoff.quantity(wall) - 4.572) < 1e-9);
        QVERIFY(qAbs(takeoff.quantity(slab) - 9.290304) < 1e-9);
        QCOMPARE(takeoff.quantity(doors), 2.0);
    }

    void unitsFollowTheFirstScaleUntilChosen()
    {
        QTemporaryDir dir;
        Takeoff takeoff(dir.path());
        QVERIFY(takeoff.open(writeSet(dir.filePath("set.pdf"), 1), 1));
        takeoff.setScale(0, 28.35, "m", "1:100", true);
        QCOMPARE(takeoff.units(), QString("metric"));
        // A detail at an imperial scale later doesn't switch the totals.
        takeoff.setScale(0, quarterInch, "ft", "1/4\" = 1'-0\"", false);
        QCOMPARE(takeoff.units(), QString("metric"));
        takeoff.setUnits("imperial");
        takeoff.setScale(0, quarterInch, "m", "1:100", true);
        QCOMPARE(takeoff.units(), QString("imperial"));
    }

    void labelsReadAsOnTheDrawings()
    {
        QTemporaryDir dir;
        Takeoff takeoff(dir.path());
        QVERIFY(takeoff.open(writeSet(dir.filePath("set.pdf"), 1), 1));
        QCOMPARE(takeoff.formatPoints(0, "length", points({{0, 0}, {10, 0}})), QString("No scale"));
        takeoff.setScale(0, quarterInch, "ft", "1/4\" = 1'-0\"", true);
        QCOMPARE(takeoff.formatPoints(0, "length", points({{0, 0}, {12.5 * quarterInch, 0}})), QString("12'-6\""));
        QCOMPARE(takeoff.formatPoints(0, "area", points({{0, 0}, {180, 0}, {180, 90}, {0, 90}})), QString("50.0 sq ft"));
        takeoff.setUnits("metric");
        QCOMPARE(takeoff.formatPoints(0, "length", points({{0, 0}, {10 * quarterInch, 0}})), QString("3.05 m"));
        QCOMPARE(takeoff.unitLabel("area"), QString("m²"));
    }

    void lengthsParse()
    {
        QCOMPARE(Takeoff::parseLength("12'-6\"", "ft"), 12.5);
        QCOMPARE(Takeoff::parseLength("12' 6\"", "ft"), 12.5);
        QCOMPARE(Takeoff::parseLength("12.5", "ft"), 12.5);
        QCOMPARE(Takeoff::parseLength("150\"", "ft"), 12.5);
        QCOMPARE(Takeoff::parseLength("400'", "ft"), 400.0);
        QCOMPARE(Takeoff::parseLength("3,2", "m"), 3.2);
        QCOMPARE(Takeoff::parseLength("3.2 m", "m"), 3.2);
        QCOMPARE(Takeoff::parseLength("twelve", "ft"), -1.0);
        QCOMPARE(Takeoff::parseLength("0", "m"), -1.0);
    }

    void presetsAreAtTheirScale()
    {
        for (const QVariant &value : Takeoff::presets()) {
            const QVariantMap preset = value.toMap();
            if (preset["label"] == "1/4\" = 1'-0\"")
                QCOMPARE(preset["pointsPerUnit"].toDouble(), quarterInch);
            if (preset["label"] == "1:100")
                QVERIFY(qAbs(preset["pointsPerUnit"].toDouble() - 72 / 2.54) < 1e-6);
        }
    }

    void measurementsMustMakeSense()
    {
        QTemporaryDir dir;
        Takeoff takeoff(dir.path());
        QVERIFY(takeoff.open(writeSet(dir.filePath("set.pdf"), 1), 1));
        const int wall = takeoff.addItem("Wall", "length");
        const int slab = takeoff.addItem("Slab", "area");
        const int doors = takeoff.addItem("Doors", "count");
        QCOMPARE(takeoff.addMeasurement(wall, 0, points({{1, 1}})), -1);
        QCOMPARE(takeoff.addMeasurement(wall, 0, points({{1, 1}, {1, 1}})), -1);
        QCOMPARE(takeoff.addMeasurement(slab, 0, points({{0, 0}, {10, 0}})), -1);
        QCOMPARE(takeoff.addMeasurement(slab, 0, points({{0, 0}, {10, 0}, {20, 0}})), -1);
        QCOMPARE(takeoff.addMeasurement(doors, 0, points({{0, 0}, {1, 1}})), -1);
        QCOMPARE(takeoff.addMeasurement(doors, 5, points({{0, 0}})), -1);
        QCOMPARE(takeoff.addMeasurement(999, 0, points({{0, 0}})), -1);
        QCOMPARE(takeoff.addItem("Nothing", "volume"), -1);
        QCOMPARE(takeoff.measurementTotal(), 0);
        // A name left empty gets one.
        const int unnamed = takeoff.addItem("  ", "count");
        QCOMPARE(takeoff.itemInfo(unnamed)["name"].toString(), QString("Item 4"));
    }

    void itemsKeepTheirKindOnceMeasured()
    {
        QTemporaryDir dir;
        Takeoff takeoff(dir.path());
        QVERIFY(takeoff.open(writeSet(dir.filePath("set.pdf"), 1), 1));
        const int item = takeoff.addItem("Base", "length", "#1E6BFF");
        takeoff.updateItem(item, "Rubber base", "area", "#E5484D");
        QCOMPARE(takeoff.itemInfo(item)["kind"].toString(), QString("area"));
        takeoff.updateItem(item, "Rubber base", "length", "");
        QVERIFY(takeoff.addMeasurement(item, 0, points({{0, 0}, {50, 0}})) > 0);
        takeoff.updateItem(item, "Base", "count", "#30A46C");
        const QVariantMap info = takeoff.itemInfo(item);
        QCOMPARE(info["name"].toString(), QString("Base"));
        QCOMPARE(info["kind"].toString(), QString("length"));
        QCOMPARE(info["color"].toString(), QString("#30a46c"));
        QCOMPARE(info["key"].toString(), QString("1"));
    }

    void measurementsMoveBetweenItemsOfTheSameKind()
    {
        QTemporaryDir dir;
        Takeoff takeoff(dir.path());
        QVERIFY(takeoff.open(writeSet(dir.filePath("set.pdf"), 1), 1));
        const int a = takeoff.addItem("A", "length"), b = takeoff.addItem("B", "length"), c = takeoff.addItem("C", "area");
        const int m = takeoff.addMeasurement(a, 0, points({{0, 0}, {50, 0}}));
        takeoff.setMeasurementItem(m, c);
        QCOMPARE(takeoff.measurement(m)["item"].toInt(), a);
        takeoff.setMeasurementItem(m, b);
        QCOMPARE(takeoff.measurement(m)["item"].toInt(), b);
        QCOMPARE(takeoff.measurementCount(a), 0);
        QCOMPARE(takeoff.measurementCount(b), 1);
    }

    void removingAnItemTakesItsMeasurementsAndUndoBringsThemBack()
    {
        QTemporaryDir dir;
        Takeoff takeoff(dir.path());
        QVERIFY(takeoff.open(writeSet(dir.filePath("set.pdf"), 1), 1));
        QVERIFY(!takeoff.canUndo());
        const int item = takeoff.addItem("Doors", "count");
        takeoff.addMeasurement(item, 0, points({{1, 1}}));
        takeoff.addMeasurement(item, 0, points({{9, 9}}));
        takeoff.removeItem(item);
        QCOMPARE(takeoff.itemCount(), 0);
        QCOMPARE(takeoff.measurementTotal(), 0);
        takeoff.undo();
        QCOMPARE(takeoff.itemCount(), 1);
        QCOMPARE(takeoff.measurementTotal(), 2);
        takeoff.undo();
        QCOMPARE(takeoff.measurementTotal(), 1);
        QVERIFY(takeoff.canRedo());
        takeoff.redo();
        QCOMPARE(takeoff.measurementTotal(), 2);
        // A new change drops what could be redone.
        takeoff.undo();
        takeoff.addMeasurement(item, 0, points({{5, 5}}));
        QVERIFY(!takeoff.canRedo());
    }

    void reshapingAndMoving()
    {
        QTemporaryDir dir;
        Takeoff takeoff(dir.path());
        QVERIFY(takeoff.open(writeSet(dir.filePath("set.pdf"), 1), 1));
        const int slab = takeoff.addItem("Slab", "area");
        const int m = takeoff.addMeasurement(slab, 0, points({{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
        takeoff.setPoints(m, points({{0, 0}, {20, 0}, {20, 10}, {0, 10}}));
        QCOMPARE(Takeoff::polygonArea(toPoints(takeoff.measurement(m)["points"])), 200.0);
        // A reshape that leaves no area is refused.
        takeoff.setPoints(m, points({{0, 0}, {20, 0}}));
        QCOMPARE(toPoints(takeoff.measurement(m)["points"]).size(), 4);
        takeoff.undo();
        QCOMPARE(Takeoff::polygonArea(toPoints(takeoff.measurement(m)["points"])), 100.0);
    }

    void hitsPickTheMarkUnderThePointer()
    {
        QTemporaryDir dir;
        Takeoff takeoff(dir.path());
        QVERIFY(takeoff.open(writeSet(dir.filePath("set.pdf"), 1), 1));
        const int floor = takeoff.addItem("Floor", "area"), room = takeoff.addItem("Room", "area");
        const int doors = takeoff.addItem("Doors", "count");
        const int big = takeoff.addMeasurement(floor, 0, points({{0, 0}, {400, 0}, {400, 400}, {0, 400}}));
        const int small = takeoff.addMeasurement(room, 0, points({{100, 100}, {200, 100}, {200, 200}, {100, 200}}));
        const int door = takeoff.addMeasurement(doors, 0, points({{150, 150}}));

        auto hit = [&](double x, double y) { return takeoff.hitTest(0, QPointF(x, y), 4); };
        QCOMPARE(hit(150, 151)["id"].toInt(), door);
        QCOMPARE(hit(120, 180)["id"].toInt(), small);
        QCOMPARE(hit(300, 300)["id"].toInt(), big);
        QCOMPARE(hit(500, 500)["id"].toInt(), -1);
        QCOMPARE(hit(399, 1)["vertex"].toInt(), 1);
        QCOMPARE(hit(400, 250)["vertex"].toInt(), -1);
        QCOMPARE(hit(400, 250)["id"].toInt(), big);
        // Hidden items can't be picked.
        takeoff.setItemVisible(doors, false);
        QCOMPARE(hit(150, 151)["id"].toInt(), small);
    }

    void cornersSnapToOtherMeasurements()
    {
        QTemporaryDir dir;
        Takeoff takeoff(dir.path());
        QVERIFY(takeoff.open(writeSet(dir.filePath("set.pdf"), 1), 1));
        const int wall = takeoff.addItem("Wall", "length");
        const int m = takeoff.addMeasurement(wall, 0, points({{100, 100}, {200, 100}}));
        QCOMPARE(takeoff.snap(0, QPointF(103, 98), 5), QPointF(100, 100));
        QCOMPARE(takeoff.snap(0, QPointF(110, 98), 5), QPointF(110, 98));
        QCOMPARE(takeoff.snap(0, QPointF(103, 98), 5, m), QPointF(103, 98));
    }

    void takeoffsSaveAndComeBack()
    {
        QTemporaryDir dir;
        const QString pdf = writeSet(dir.filePath("set.pdf"), 2);
        int wall;
        {
            Takeoff takeoff(dir.path() + "/takeoffs");
            QVERIFY(takeoff.open(pdf, 2));
            wall = takeoff.addItem("Drywall, type X", "length", "#8E4EC6");
            takeoff.addMeasurement(wall, 1, points({{0, 0}, {36, 0}, {36, 18}}));
            takeoff.setScale(1, quarterInch, "ft", "1/4\" = 1'-0\"", false);
            takeoff.setUnits("metric");
            takeoff.setItemVisible(wall, false);
            QVERIFY(QFile::exists(takeoff.storagePath()));
        }
        // Moved and renamed, it's the same drawing.
        QVERIFY(QFile::rename(pdf, dir.filePath("renamed.pdf")));
        Takeoff takeoff(dir.path() + "/takeoffs");
        QVERIFY(takeoff.open(dir.filePath("renamed.pdf"), 2));
        QCOMPARE(takeoff.itemCount(), 1);
        QCOMPARE(takeoff.itemInfo(wall)["name"].toString(), QString("Drywall, type X"));
        QCOMPARE(takeoff.itemInfo(wall)["visible"].toBool(), false);
        QCOMPARE(takeoff.units(), QString("metric"));
        QCOMPARE(takeoff.scaleLabel(1), QString("1/4\" = 1'-0\""));
        QVERIFY(!takeoff.hasScale(0));
        QVERIFY(qAbs(takeoff.quantity(wall) - 3 * 0.3048) < 1e-9);
        // Nothing to undo in what was loaded, and nothing unexported yet.
        QVERIFY(!takeoff.canUndo());
        QVERIFY(!takeoff.unexported());
    }

    void anUnreadableTakeoffIsSetAside()
    {
        QTemporaryDir dir;
        const QString pdf = writeSet(dir.filePath("set.pdf"), 1);
        Takeoff first(dir.path());
        QVERIFY(first.open(pdf, 1));
        write(first.storagePath(), "{ not json");

        Takeoff takeoff(dir.path());
        QSignalSpy failed(&takeoff, &Takeoff::failed);
        QVERIFY(takeoff.open(pdf, 1));
        QCOMPARE(failed.size(), 1);
        QVERIFY(QFile::exists(takeoff.storagePath() + ".unreadable"));
        QCOMPARE(takeoff.itemCount(), 0);
    }

    void csvHasARowPerSheetAndATotal()
    {
        QTemporaryDir dir;
        Takeoff takeoff(dir.path());
        QVERIFY(takeoff.open(writeSet(dir.filePath("set.pdf"), 3), 3));
        takeoff.setScale(0, quarterInch, "ft", "1/4\" = 1'-0\"", false);
        takeoff.setScale(2, quarterInch, "ft", "1/4\" = 1'-0\"", false);
        const int wall = takeoff.addItem("Drywall, type \"X\"", "length");
        const int doors = takeoff.addItem("=HYPERLINK(\"x\")", "count");
        const int empty = takeoff.addItem("Not measured", "area");
        Q_UNUSED(empty);
        takeoff.addMeasurement(wall, 0, points({{0, 0}, {10 * quarterInch, 0}}));
        takeoff.addMeasurement(wall, 1, points({{0, 0}, {4 * quarterInch, 0}}));
        takeoff.addMeasurement(wall, 2, points({{0, 0}, {2.5 * quarterInch, 0}}));
        takeoff.addMeasurement(doors, 0, points({{1, 1}}));
        takeoff.addMeasurement(doors, 0, points({{2, 2}}));

        const QStringList lines = takeoff.toCsv({"A-101", "A-102", "A-103"}).split("\r\n", Qt::SkipEmptyParts);
        QCOMPARE(lines, QStringList({
            "Item,Kind,Sheet,Measurements,Quantity,Unit",
            "\"Drywall, type \"\"X\"\"\",Length,A-101,1,10.00,ft",
            "\"Drywall, type \"\"X\"\"\",Length,A-102,1,,No scale",
            "\"Drywall, type \"\"X\"\"\",Length,A-103,1,2.50,ft",
            "\"Drywall, type \"\"X\"\"\",Length,All sheets,3,12.50,ft",
            // A name that starts like a formula is kept as text.
            "\"'=HYPERLINK(\"\"x\"\")\",Count,A-101,2,2,ea",
            "\"'=HYPERLINK(\"\"x\"\")\",Count,All sheets,2,2,ea",
            "Not measured,Area,All sheets,0,0.00,sq ft",
        }));

        takeoff.setUnits("metric");
        QVERIFY(takeoff.toCsv({"A-101", "A-102", "A-103"}).contains("All sheets,3,3.81,m\r\n"));
    }

    void exportWritesTheCsvAndClearsUnexported()
    {
        QTemporaryDir dir;
        Takeoff takeoff(dir.path());
        QVERIFY(takeoff.open(writeSet(dir.filePath("set.pdf"), 1), 1));
        QVERIFY(!takeoff.unexported());
        const int doors = takeoff.addItem("Doors", "count");
        takeoff.addMeasurement(doors, 0, points({{1, 1}}));
        QVERIFY(takeoff.unexported());
        QString error;
        QVERIFY(takeoff.exportCsv(dir.filePath("out.csv"), {"A-101"}, &error));
        QVERIFY(!takeoff.unexported());
        QFile csv(dir.filePath("out.csv"));
        QVERIFY(csv.open(QIODevice::ReadOnly));
        QVERIFY(csv.readAll().startsWith("Item,Kind,Sheet"));
        // Somewhere it can't write.
        QVERIFY(!takeoff.exportCsv(dir.filePath("missing/out.csv"), {"A-101"}, &error));
        QVERIFY(error.contains("missing/out.csv"));
    }

    void exportJobAsksWhereThenWrites()
    {
        QTemporaryDir dir;
        FakePicker picker;
        Backend backend(&picker);
        Takeoff takeoff(dir.path() + "/takeoffs");
        ExportJob job(&backend, &takeoff, &picker);
        QSignalSpy finished(&job, &ExportJob::finished), failed(&job, &ExportJob::failed);
        QVERIFY(backend.load(QUrl::fromLocalFile(writeSet(dir.filePath("Clinic.pdf"), 1))));
        QVERIFY(takeoff.open(backend.filePath(), 1));

        job.start();
        QCOMPARE(failed.size(), 1);

        takeoff.addMeasurement(takeoff.addItem("Doors", "count"), 0, points({{1, 1}}));
        picker.next = QUrl::fromLocalFile(dir.filePath("quantities"));
        job.start();
        QCOMPARE(picker.lastSuggested.toLocalFile(), dir.filePath("Clinic quantities.csv"));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().first().toString(), dir.filePath("quantities.csv"));
        QVERIFY(QFile::exists(dir.filePath("quantities.csv")));
    }

    void backendLoadsSheetsAndRendersTiles()
    {
        QTemporaryDir dir;
        FakePicker picker;
        Backend backend(&picker);
        QSignalSpy changed(&backend, &Backend::documentChanged);
        picker.next = QUrl::fromLocalFile(writeSet(dir.filePath("set.pdf"), 3));
        backend.openDrawing();
        QCOMPARE(changed.size(), 1);
        QCOMPARE(backend.sheetCount(), 3);
        QCOMPARE(backend.sheetLabel(1), QString("2"));
        backend.setSheet(9);
        QCOMPARE(backend.sheet(), 2);

        const QImage whole = backend.render(QString("%1/0").arg(backend.generation()), QSize(600, 600));
        QCOMPARE(qMax(whole.width(), whole.height()), 600);
        const QImage tile = backend.render(QString("%1/0/4000x2600/512,512,256,128").arg(backend.generation()), {});
        QCOMPARE(tile.size(), QSize(256, 128));
        QVERIFY(backend.render("nonsense", {}).isNull());

        // A file that isn't a PDF leaves the open one alone.
        write(dir.filePath("bad.pdf"), "not a pdf");
        QVERIFY(!backend.load(QUrl::fromLocalFile(dir.filePath("bad.pdf"))));
        QCOMPARE(backend.sheetCount(), 3);
        QVERIFY(!backend.error().isEmpty());
    }

    void commandLineReportsAndExports()
    {
        QTemporaryDir dir;
        const QString pdf = writeSet(dir.filePath("set.pdf"), 2);
        {
            // Saved where the command line looks: the data folder.
            Takeoff takeoff;
            QVERIFY(takeoff.open(pdf, 2));
            QVERIFY(takeoff.storagePath().startsWith(m_home.path()));
            const int wall = takeoff.addItem("Drywall", "length");
            takeoff.setScale(0, quarterInch, "ft", "1/4\" = 1'-0\"", false);
            takeoff.addMeasurement(wall, 0, points({{0, 0}, {8 * quarterInch, 0}}));
        }
        QVERIFY(isCliCommand("quantities"));
        QVERIFY(!isCliCommand("open"));
        const QString csv = dir.filePath("q.csv");
        QCOMPARE(runCli({"plumb", "export", pdf, "-o", csv}), 0);
        QFile file(csv);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("Drywall,Length,All sheets,1,8.00,ft"));
        QCOMPARE(runCli({"plumb", "export", pdf, "-o", pdf}), 1);
        QCOMPARE(runCli({"plumb", "quantities", dir.filePath("missing.pdf")}), 1);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void englishPluralsComeFromTheCatalog()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "en\n");
        Translator translator(dir.path() + "/language");
        QCOMPARE(translator.translate("ItemPanel", "%n measurements", nullptr, 1), QString("%n measurement"));
        QCOMPARE(translator.translate("ItemPanel", "%n measurements", nullptr, 3), QString("%n measurements"));
    }

    void shippedCatalogsCoverTheSameStrings()
    {
        // Every shipped language translates the same strings, so adding a
        // string to one catalog and forgetting another fails here.
        const QDir catalogs(QFINDTESTDATA("../src/i18n"));
        const QStringList files = catalogs.entryList({"*.json"}, QDir::Files);
        auto keysOf = [&](const QString &file) {
            QFile catalog(catalogs.filePath(file));
            return catalog.open(QIODevice::ReadOnly)
                ? QJsonDocument::fromJson(catalog.readAll()).object().keys() : QStringList();
        };
        if (!files.contains("da.json"))
            QSKIP("No translations yet");
        QVERIFY(files.size() >= 50);
        const QStringList keys = keysOf("da.json");
        QVERIFY(keys.size() > 100);
        for (const QString &file : files)
            if (file != "en.json")
                QVERIFY2(keysOf(file) == keys, qPrintable(file));
    }

private:
    QTemporaryDir m_home;

    static QVariantList points(std::initializer_list<QPointF> list)
    {
        QVariantList result;
        for (const QPointF &p : list)
            result.append(p);
        return result;
    }

    static QVector<QPointF> toPoints(const QVariant &value)
    {
        QVector<QPointF> result;
        for (const QVariant &p : value.toList())
            result.append(p.toPointF());
        return result;
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    // A landscape drawing with a numbered box on each sheet.
    static QString writeSet(const QString &path, int sheets)
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::ArchD));
        writer.setPageOrientation(QPageLayout::Landscape);
        QPainter painter(&writer);
        for (int sheet = 0; sheet < sheets; ++sheet) {
            if (sheet > 0)
                writer.newPage();
            painter.drawRect(400, 400, 4000 + sheet * 500, 3000);
            painter.drawText(600, 800, QString("Sheet %1").arg(sheet + 1));
        }
        painter.end();
        return path;
    }
};

QTEST_MAIN(PlumbTests)
#include "plumb_tests.moc"
