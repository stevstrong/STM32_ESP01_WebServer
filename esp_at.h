/*-----------------------------------------------------------------------------

Web server using ESP8266 via AT commands over serial interface
Up to 5 communication sockets are supported.

Sources:
https://os.mbed.com/users/programmer5/code/STM32-ESP8266-WEBSERVER/docs/tip/main_8cpp_source.html
https://startingelectronics.org/articles/arduino/switch-and-web-page-button-LED-control/
https://github.com/MaJerle/ESP8266_AT_Commands_parser/blob/master/00-ESP8266_LIBRARY/esp8266.c
http://www.diygoodies.org.ua/?p=827

-----------------------------------------------------------------------------*/
#ifndef _ESP_H_
#define _ESP_H_


#include <Arduino.h>
#include <Streaming.h>

#define DEBUG 0


//-----------------------------------------------------------------------------
#define debug Serial
#define esp Serial1

//-----------------------------------------------------------------------------
typedef void (*customParseFunctPtr)(char *);
//-----------------------------------------------------------------------------
extern void ESP_setup(customParseFunctPtr custom_Parse, voidFuncPtr custom_Process);

extern void ESP_loop();

extern void ESP_SendData(const char * reply, uint16_t len, uint8_t close);


#endif // _ESP_H_