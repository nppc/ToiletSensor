//#pragma src

#include "main.h"
#include "InitDevice.h"
#include "i2c.h"
#include "player.h"
#include "adpcm_decoder.h"
//#include "flash.h"
//#include "mulaw_decoder.h"

void SiLabs_Startup(void) {
  // $[SiLabs Startup]
  // [SiLabs Startup]$

}

//uint8_t xdata testBuffer[70];

int main(void) {

  enter_DefaultMode_from_RESET();

  i2c_init();

  waitNms(4000);

//  waitNms(10000);

//  eeprom_write_test(PLAY_BAK, g_adpcmFlashData_bak);
//  eeprom_write_test(PLAY_ZAPOLNENO, g_adpcmFlashData_zapolneno);
//  eeprom_write_test(PLAY_30, g_adpcmFlashData_30);
//  eeprom_write_test(PLAY_50, g_adpcmFlashData_50);
//  eeprom_write_test(PLAY_75, g_adpcmFlashData_75);
//  eeprom_write_test(PLAY_PROCENTOV, g_adpcmFlashData_procentov);
//  eeprom_write_test(PLAY_POCHTI, g_adpcmFlashData_pochti);
//  eeprom_write_test(PLAY_POLNY, g_adpcmFlashData_polny);
//  eeprom_write_test(PLAY_PUSTOJ, g_adpcmFlashData_pustoj);
//  eeprom_write_test(PLAY_OBLEGCHENIJE, g_adpcmFlashData_oblegchenije);
//  eeprom_write_test(PLAY_KVASHIMUSLUGAM, g_adpcmFlashData_kvashimuslugam);
//51968

  while(1){

      player_play_sample(PLAY_KVASHIMUSLUGAM);

      waitNms(1000);

      player_play_sample(PLAY_BAK);
      player_play_sample(PLAY_PUSTOJ);

      waitNms(1000);

      player_play_sample(PLAY_ZAPOLNENO);
      player_play_sample(PLAY_30);
      player_play_sample(PLAY_PROCENTOV);

      waitNms(1000);

      player_play_sample(PLAY_ZAPOLNENO);
      player_play_sample(PLAY_50);
      player_play_sample(PLAY_PROCENTOV);

      waitNms(1000);

      player_play_sample(PLAY_ZAPOLNENO);
      player_play_sample(PLAY_75);
      player_play_sample(PLAY_PROCENTOV);

      waitNms(1000);

      player_play_sample(PLAY_BAK);
      player_play_sample(PLAY_POCHTI);
      player_play_sample(PLAY_POLNY);

      waitNms(1000);

      player_play_sample(PLAY_BAK);
      player_play_sample(PLAY_POLNY);

      waitNms(1000);

      player_play_sample(PLAY_OBLEGCHENIJE);

      waitNms(4000);
  }
}
