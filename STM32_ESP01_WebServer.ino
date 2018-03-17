/*-----------------------------------------------------------------------------

Web server example using STM32F03C8 (blue pill) and ESP8266 via AT commands.
The index page is stored in flash.

Usage:
- in setup:
	ESP_setup(ParseHeader, ProcessData);
	
- in loop:
	ESP_loop(); // call it as often as possible

- send data:
	ESP_SendData(msgLedOn, sizeof(msgLedOn)-1, 1);
	- third parameter indicates whether reply is complete

-----------------------------------------------------------------------------*/

#include "esp_at.h"

//-----------------------------------------------------------------------------
// optional
static char dbg[256]; // for debug sprintf
#define PRINTNL { Serial.write('\n'); }
#define PRINTF(...) { sprintf(dbg, __VA_ARGS__); Serial.print(dbg); }
//-----------------------------------------------------------------------------
// global variables
uint32_t t;
uint8_t req; // set if reply has to be sent
//-----------------------------------------------------------------------------
// Parses the GET header for icoming commands/parameters
// Parameter 'p' points to the char position after first "/" following "GET"
//-----------------------------------------------------------------------------
void ParseHeader(char * p)
{
	req = false; // reset valid command detected
	if ( strncmp(p, "LED=", 4)==0 ) // expected parameter format: 'LED=x', x=0,1,?
	{
		p += 4; // set pointer to value
		if ( (*p)=='0' || (*p)=='1' ) // set LED state
		{
			digitalWrite(LED_BUILTIN, (*p)-'1');
			t = millis(); // reset blink timing
			req = true; // send LED status
		}
		else
		if ( *p=='?' ) // request LED status
		{ // do nothing, just send LED status
			req = true;
		}
	}
}
//-----------------------------------------------------------------------------
const char msgLedOn[] = "LED=1\r\n";
const char msgLedOff[] = "LED=0\r\n";
const char nack[] = "NOK\r\n";
//-----------------------------------------------------------------------------
// Prepares and sends the reply to the client
//-----------------------------------------------------------------------------
void ProcessData()
{
	Serial.print("-> sending ");
	if ( req )
	{
		// read and send here the LED status
		if ( digitalRead(LED_BUILTIN) ) {
			Serial.print(msgLedOff); // optional
			ESP_SendData(msgLedOff, sizeof(msgLedOff)-1, 1);
		} else {
			Serial.print(msgLedOn); // optional
			ESP_SendData(msgLedOn, sizeof(msgLedOn)-1, 1);
		}
		req = false;
	}
	else
	{
		Serial.print(nack); // optional
		// reply string, nr bytes=(size-1) to omit ending zero char, message end
		ESP_SendData(nack, sizeof(nack)-1, 1);
	}
}
//-----------------------------------------------------------------------------
void setup()
{
	pinMode(LED_BUILTIN, OUTPUT);
	req = false;

	Serial.begin(115200); // for debug output
	//while (!debug); delay(10);

	ESP_setup(ParseHeader, ProcessData);
}
//-----------------------------------------------------------------------------
void loop()
{
	ESP_loop();

	if ( (millis()-t)>3000 )
	{
		t = millis();
		digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // toggle
		//Serial << "len1 = " << sizeof(test1) << ", len2 = " << sizeof(test2) << endl;
	}
}
