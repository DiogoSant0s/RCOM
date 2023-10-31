#ifndef ALARM_H
#define ALARM_H

#include "macros.h"

extern int alarmEnabled;

void alarmHandler(int signal);

#endif // ALARM_H