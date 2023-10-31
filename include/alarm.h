#ifndef ALARM_H
#define ALARM_H

#include "macros.h"

extern int alarmEnabled;

// Alarm handler for the alarm() function.
void alarmHandler(int signal);

#endif // ALARM_H