#ifndef CLI_CONSOLE_H
#define CLI_CONSOLE_H

#include <QString>

namespace CliConsole {

/** Attach or allocate a Windows console so WIN32 GUI exe can print to the parent terminal. */
void attach();
void writeLine(const char* text);
void writeLine(const QString& text);

} // namespace CliConsole

#endif
