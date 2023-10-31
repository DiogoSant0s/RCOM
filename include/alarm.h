#ifndef ALARM_H
#define ALARM_H

#include "macros.h"

extern int alarmEnabled;
extern int alarmCount;

void alarmHandler(int signal);

#endif // ALARM_H