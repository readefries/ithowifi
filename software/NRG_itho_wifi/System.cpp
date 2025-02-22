#include <Arduino.h>
#include "System.h"

const char* System::uptime()
{
  char buffer[65];

  long days = 0;
  long hours = 0;
  long mins = 0;
  long secs = 0;

  secs = millis() / 1000; //convect milliseconds to seconds
  mins = secs / 60; //convert seconds to minutes
  hours = mins / 60; //convert minutes to hours
  days = hours / 24; //convert hours to days
  secs = secs - (mins * 60); //subtract the coverted seconds to minutes in order to display 59 secs max
  mins = mins - (hours * 60); //subtract the coverted minutes to hours in order to display 59 minutes max
  hours = hours - (days * 24); //subtract the coverted hours to days in order to display 23 hours max

  if (days < 10) {
    strcpy(retval, "0");
  }
  else {
    strcpy(retval, "");
  }
  ltoa(days, buffer, 10);
  strcat(retval, buffer);


  if (hours < 10) {
    strcat(retval, ":0");
  }
  else {
    strcat(retval, ":");
  }
  ltoa(hours, buffer, 10);
  strcat(retval, buffer);

  if (mins < 10) {
    strcat(retval, ":0");
  }
  else {
    strcat(retval, ":");
  }
  ltoa(mins, buffer, 10);
  strcat(retval, buffer);

  if (secs < 10) {
    strcat(retval, ":0");
  }
  else {
    strcat(retval, ":");
  }
  ltoa(secs, buffer, 10);
  strcat(retval, buffer);

  return retval;
}

int System::ramFree() {
  int a = ESP.getFreeHeap();
  return a;
}

#if defined (ESP8266)
int System::ramSize() {
  return 0;
}
#elif defined (ESP32)
int System::ramSize() {
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
  return info.total_free_bytes;
}
#endif

bool System::updateFreeMem() {
  bool result = false;
#if defined (ESP8266)
  int newMem = ramFree();
#elif defined (ESP32)
  int newMem = ramSize();
#endif
  if (newMem != memHigh) {
    memHigh = newMem;
    result = true;
  }
#if defined (ESP32)
  newMem = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
#endif  
  if (newMem < memLow) {
    memLow = newMem;
    result = true;
  }
  return result;
}

int System::getMemHigh() {
  return memHigh;
}

int System::getMemLow() {
  return memLow;
}

uint32_t System::getMaxFreeBlockSize() {

#if defined (ESP8266)
  memMaxBlock = ESP.getMaxFreeBlockSize();
#elif defined (ESP32)
  memMaxBlock = ESP.getMaxAllocHeap();
#endif  
  return memMaxBlock;
}
