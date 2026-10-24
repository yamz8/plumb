#include "cli.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "takeoff.h"

namespace {

void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonObject &object) { fputs(QJsonDocument(object).toJson().constData(), stdout); }

int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

struct Command {
    QCommandLineParser parser;
    Command(const QString &name, const QString &description) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name);
        parser.addPositionalArgument("drawing", "PDF drawing", "<drawing.pdf>");
    }
    void json() { parser.addOption({"json", "Print the result as JSON"}); }
    bool wantsJson() const { return parser.isSet("json"); }
    QString argument(int index) const { return parser.positionalArguments().value(index); }
};

// Opens the drawing and the takeoff stored for it, as the window would.
bool open(Backend &backend, Takeoff &takeoff, const QString &path) {
    if (path.isEmpty())
        return fail("Name a PDF drawing."), false;
    if (!backend.load(QUrl::fromUserInput(path, QDir::currentPath(), QUrl::AssumeLocalFile)))
        return fail(backend.error()), false;
    QString problem;
    QObject::connect(&takeoff, &Takeoff::failed, [&problem](const QString &message) { problem = message; });
    if (!takeoff.open(backend.filePath(), backend.sheetCount()))
        return fail("Could not read " + backend.filePath()), false;
    if (!problem.isEmpty())
        print(stderr, problem);
    return true;
}

double round2(double value) { return qRound64(value * 100) / 100.0; }

// The unit as it's written in JSON and CSV: plain ASCII.
QString plainUnit(const Takeoff &takeoff, const QString &kind) {
    if (kind == "count")
        return "ea";
    if (takeoff.units() == "metric")
        return kind == "length" ? "m" : "m2";
    return kind == "length" ? "ft" : "sq ft";
}

int sheets(const QStringList &arguments) {
    Command command("sheets", "List the sheets in a drawing, with their scales and measurement counts.");
    command.json();
    command.parser.process(arguments);
    Backend backend;
    Takeoff takeoff;
    if (!open(backend, takeoff, command.argument(1)))
        return 1;
    QJsonArray list;
    for (int sheet = 0; sheet < backend.sheetCount(); ++sheet) {
        const QSizeF size = backend.sizeOf(sheet);
        const QString scale = takeoff.scaleLabel(sheet);
        list.append(QJsonObject{{"number", sheet + 1},
                                {"label", backend.sheetLabel(sheet)},
                                {"width_in", round2(size.width() / 72)},
                                {"height_in", round2(size.height() / 72)},
                                {"scale", takeoff.hasScale(sheet) ? QJsonValue(scale) : QJsonValue()},
                                {"measurements", takeoff.countOnSheet(sheet)}});
    }
    if (command.wantsJson()) {
        print({{"file", backend.fileName()}, {"sheets", list}});
        return 0;
    }
    for (const auto &value : list) {
        const QJsonObject sheet = value.toObject();
        print(stdout, QString("%1\t%2\t%3 x %4 in\t%5\t%6 measurements")
                          .arg(sheet["number"].toInt())
                          .arg(sheet["label"].toString())
                          .arg(sheet["width_in"].toDouble())
                          .arg(sheet["height_in"].toDouble())
                          .arg(sheet["scale"].isNull() ? "no scale" : sheet["scale"].toString())
                          .arg(sheet["measurements"].toInt()));
    }
    return 0;
}

int quantities(const QStringList &arguments) {
    Command command("quantities", "Report each item's quantity, sheet by sheet and in total.");
    command.json();
    command.parser.process(arguments);
    Backend backend;
    Takeoff takeoff;
    if (!open(backend, takeoff, command.argument(1)))
        return 1;

    QJsonArray items;
    for (const TakeoffItem &item : takeoff.itemList()) {
        QJsonArray sheets;
        for (const QVariant &entry : takeoff.sheetBreakdown(item.id)) {
            const QVariantMap map = entry.toMap();
            const int sheet = map["sheet"].toInt();
            const bool scaled = map["scaled"].toBool();
            sheets.append(QJsonObject{{"number", sheet + 1},
                                      {"label", backend.sheetLabel(sheet)},
                                      {"measurements", map["count"].toInt()},
                                      // Lengths and areas need a scale to have a quantity.
                                      {"quantity", scaled ? QJsonValue(round2(takeoff.quantity(item.id, sheet))) : QJsonValue()}});
        }
        items.append(QJsonObject{{"name", item.name},
                                 {"kind", item.kind},
                                 {"unit", plainUnit(takeoff, item.kind)},
                                 {"color", item.color.name(QColor::HexRgb)},
                                 {"measurements", takeoff.measurementCount(item.id)},
                                 {"quantity", round2(takeoff.quantity(item.id))},
                                 {"unscaled", takeoff.unscaledCount(item.id)},
                                 {"sheets", sheets}});
    }

    if (command.wantsJson()) {
        print({{"file", backend.fileName()}, {"units", takeoff.units()}, {"items", items}});
        return 0;
    }
    if (items.isEmpty()) {
        print(stdout, "No items yet.");
        return 0;
    }
    for (const auto &value : items) {
        const QJsonObject item = value.toObject();
        const QString unit = item["unit"].toString();
        print(stdout, QString("%1\t%2 %3\t(%4 measurements)")
                          .arg(item["name"].toString())
                          .arg(item["quantity"].toDouble(), 0, 'f', item["kind"].toString() == "count" ? 0 : 2)
                          .arg(unit)
                          .arg(item["measurements"].toInt()));
        for (const auto &entry : item["sheets"].toArray()) {
            const QJsonObject sheet = entry.toObject();
            print(stdout, QString("  %1\t%2")
                              .arg(sheet["label"].toString())
                              .arg(sheet["quantity"].isNull()
                                       ? QString("no scale, not counted")
                                       : QString::number(sheet["quantity"].toDouble(), 'f',
                                                         item["kind"].toString() == "count" ? 0 : 2) + " " + unit));
        }
    }
    return 0;
}

int exportCsv(const QStringList &arguments) {
    Command command("export", "Write the quantities to a CSV file.");
    command.json();
    command.parser.addOption({{"o", "output"}, "CSV to write; - for standard output", "file"});
    command.parser.process(arguments);
    const QString output = command.parser.value("output");
    if (output.isEmpty())
        return fail("Name the CSV to write with -o, or -o - for standard output.");
    Backend backend;
    Takeoff takeoff;
    if (!open(backend, takeoff, command.argument(1)))
        return 1;
    if (output == "-") {
        fputs(takeoff.toCsv(backend.sheetLabels()).toUtf8().constData(), stdout);
        return 0;
    }
    if (QFileInfo(output).absoluteFilePath() == backend.filePath())
        return fail("Write the CSV to a new file; the drawing is never changed.");
    QString error;
    if (!takeoff.exportCsv(output, backend.sheetLabels(), &error))
        return fail(error);
    if (command.wantsJson())
        print({{"output", QFileInfo(output).absoluteFilePath()}, {"items", takeoff.itemCount()},
               {"measurements", takeoff.measurementTotal()}});
    else
        print(stdout, "Wrote " + output);
    return 0;
}

// As Hype does: one shared copy, linked into Claude Code, found directly by Codex.
int skill(const QStringList &arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription("Print the agent skill, or install it for your coding agents with: skill install.");
    parser.addHelpOption();
    parser.addPositionalArgument("skill", "", "skill");
    parser.addPositionalArgument("install", "Copy to ~/.agents/skills/plumb and link into ~/.claude/skills", "[install]");
    parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    const QString action = parser.positionalArguments().value(1);
    if (action.isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (action != "install")
        return fail("Use plumb skill to print the skill, or plumb skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/plumb");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/plumb");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/plumb", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments[0], topic, "--help"});
    print(stdout, "Usage: plumb <command> [options]\n\n" + cliSummary());
    return 0;
}

}

QString cliSummary() {
    return "Window:\n"
           "  open [drawing.pdf]                Open Plumb, with a drawing when given one\n\n"
           "Commands that need no display (plumb help <command> for options):\n"
           "  sheets <drawing>                  List the sheets, with scales and measurement counts\n"
           "  quantities <drawing>              Report each item's quantity, per sheet and in total\n"
           "  export <drawing> -o <csv>         Write the quantities to CSV\n"
           "  skill [install]                   Print the skill for coding agents, or install it";
}

bool isCliCommand(const QString &word) {
    return QStringList{"sheets", "quantities", "export", "skill", "help"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "sheets"       ? sheets(arguments)
           : command == "quantities" ? quantities(arguments)
           : command == "export"     ? exportCsv(arguments)
           : command == "skill"      ? skill(arguments)
                                     : help(arguments);
}
