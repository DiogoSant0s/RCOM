#include "../include/alarm.h"

#include "stdio.h"

void alarmHandler(int signal) {
    alarmEnabled = FALSE;
}