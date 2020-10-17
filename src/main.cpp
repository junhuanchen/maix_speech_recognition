/**
 * @file main.cpp
 * @brief Demo of speech recognition using Maix board - K210 MCU
 * @copyright 2019 - Andri Yadi, DycodeX
 */

#include <Arduino.h>

extern "C"
{
#include "./maix_speech.h"
#include "./voice_model.h"
}

void setup()
{
  Serial.begin(9600);

  sr_begin();
  Serial.println("Initializing model...");

  const int RECORD_MODE = 0; // Set this 1 to start collecting your Voice Model

  if (RECORD_MODE == 0)
  {
    sr_set_model(0, hey_friday_0, fram_num_hey_friday_0);
    sr_set_model(1, hey_friday_1, fram_num_hey_friday_1);
    sr_set_model(2, hey_friday_2, fram_num_hey_friday_2);
    sr_set_model(3, hey_friday_3, fram_num_hey_friday_3);
    sr_set_model(4, hey_jarvis_0, fram_num_hey_jarvis_0);
    sr_set_model(5, hey_jarvis_1, fram_num_hey_jarvis_1);
    sr_set_model(6, hey_jarvis_2, fram_num_hey_jarvis_2);
    sr_set_model(7, hey_jarvis_3, fram_num_hey_jarvis_3);
    Serial.println("Model init OK!");
  }
  else
  {
    for (int i = sr_record(0); i != 6; i = sr_record(0))
    {
      if (i == 5)
        Serial.println("Say AB");
    }
    for (int i = sr_record(1); i != 6; i = sr_record(1))
    {
      if (i == 5)
        Serial.println("Say AB");
    }
    // sr_print_model(0, 0);
    for (int i = sr_record(2); i != 6; i = sr_record(2))
    {
      if (i == 5)
        Serial.println("Say CD");
    }
    for (int i = sr_record(3); i != 6; i = sr_record(3))
    {
      if (i == 5)
        Serial.println("Say CD");
    }
    // sr_print_model(1, 0);
    Serial.println("Record init OK!");
  }
}

void loop()
{
  int res = sr_recognize();
  // Serial.printf("recognize res %d\r\n", res);
}
