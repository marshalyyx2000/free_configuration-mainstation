#ifndef STDOUT_QUEUE_H
#define STDOUT_QUEUE_H

#include <stdio.h>

/* Lines produced on the IEC 61850 receive thread must not write stdout.
 * A full pipe blocks that thread, the association lock stays held, and the
 * next general-interrogation write never reaches the wire. */

void stdout_queue_init(void);
/* Copies the line. Returns 0 on success. */
int stdout_queue_push(const char* line);
char* stdout_queue_pop(void);
int stdout_queue_count(void);
void stdout_queue_flush(FILE* out);

#endif
