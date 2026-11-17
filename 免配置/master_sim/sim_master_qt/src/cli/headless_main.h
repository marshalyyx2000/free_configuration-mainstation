#ifndef HEADLESS_MAIN_H
#define HEADLESS_MAIN_H

#include <QString>

bool isHeadlessSubcommand(const QString& arg);
int runHeadlessCli(int argc, char* argv[]);

#endif
