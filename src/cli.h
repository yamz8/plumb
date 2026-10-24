#pragma once

#include <QString>
#include <QStringList>

// Commands that need no display, for scripts and coding agents, as Hype
// does: each takes --json, exits 0 or 1, and writes errors to stderr.
QString cliSummary();
bool isCliCommand(const QString &word);
int runCli(const QStringList &arguments);
