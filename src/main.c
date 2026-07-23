//#pragma src

#include "main.h"
#include "InitDevice.h"
#include "i2c.h"
#include "player.h"
#include "adpcm_decoder.h"
#include "mulaw_decoder.h"
//#include "flash.h"

void SiLabs_Startup(void) {
  // $[SiLabs Startup]
  // [SiLabs Startup]$

}

//uint8_t xdata testBuffer[70];

int main(void) {

  enter_DefaultMode_from_RESET();

  i2c_init();


//  waitNms(10000);
//  test_write_flash_data();

//  retval = test_verify_flash_data();



  while(1){
      player_Start();
      ADPCM_Start(11264, 8192);
      while(player_IsBusy()){
          ADPCM_Task_interpolated();
      }

      waitNms(500);

      player_Start();
      MULAW_Start(0, G_KVASHIMUSLUGAM_MULAW_ULAW_LENGTH);
      while(player_IsBusy()){
          MULAW_Task_interpolated();
      }

      waitNms(2000);
  }
}
