# STM32_ESP01_WebServer

Simple web server example using STM32F03C8 (blue pill) and ESP8266 via AT commands.
The index page is stored in flash.

Usage:
- in setup:

	ESP_setup(ParseHeader, ProcessData);
	
- in loop:

	ESP_loop(); // call it as often as possible

- send data:

	ESP_SendData(msgLedOn, sizeof(msgLedOn)-1, 1); // third parameter indicates that the reply is complete


