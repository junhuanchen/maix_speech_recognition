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

int sr_print_model(uint8_t keyword_num, uint8_t model_num)
{
  Serial.printf("frm_num=%d\n", ftr_save[keyword_num * 4 + model_num].frm_num);
  for (int i = 0; i < (vv_frm_max * mfcc_num); i++)
  {
    if (((i + 1) % 35) == 0)
      Serial.printf("%d,\n", ftr_save[keyword_num * 4 + model_num].mfcc_dat[i]);
    else
      Serial.printf("%d, ", ftr_save[keyword_num * 4 + model_num].mfcc_dat[i]);
  }
  Serial.printf("\nprint model ok!\n");
  return 0;
}

void setup()
{
  Serial.begin(9600);

  sr_begin();
  Serial.println("Initializing model...");

  const int RECORD_MODE = 0; // Set this 1 to start collecting your Voice Model

  if (RECORD_MODE == 0)
  {
    sr_addVoiceModel(0, 0, hey_friday_0, fram_num_hey_friday_0);
    sr_addVoiceModel(0, 1, hey_friday_1, fram_num_hey_friday_1);
    sr_addVoiceModel(0, 2, hey_friday_2, fram_num_hey_friday_2);
    sr_addVoiceModel(0, 3, hey_friday_3, fram_num_hey_friday_3);
    sr_addVoiceModel(1, 0, hey_jarvis_0, fram_num_hey_jarvis_0);
    sr_addVoiceModel(1, 1, hey_jarvis_1, fram_num_hey_jarvis_1);
    sr_addVoiceModel(1, 2, hey_jarvis_2, fram_num_hey_jarvis_2);
    sr_addVoiceModel(1, 3, hey_jarvis_3, fram_num_hey_jarvis_3);
    Serial.println("Model init OK!");
  }
  else
  {
    for (int i = sr_record(0, 0); i != 6; i = sr_record(0, 0))
    {
      if (i == 5)
        Serial.println("Say AB");
    }
    for (int i = sr_record(0, 1); i != 6; i = sr_record(0, 1))
    {
      if (i == 5)
        Serial.println("Say AB");
    }
    // sr_print_model(0, 0);
    for (int i = sr_record(1, 0); i != 6; i = sr_record(1, 0))
    {
      if (i == 5)
        Serial.println("Say CD");
    }
    for (int i = sr_record(1, 1); i != 6; i = sr_record(1, 1))
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
  int res;
  res = sr_recognize();
  Serial.printf("recognize \r\n");
}
