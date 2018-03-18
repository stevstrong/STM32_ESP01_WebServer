/*-----------------------------------------------------------------------------

Web server using ESP8266 via AT commands over serial interface
Up to 5 communication sockets are supported.

Sources:
https://os.mbed.com/users/programmer5/code/STM32-ESP8266-WEBSERVER/docs/tip/main_8cpp_source.html
https://startingelectronics.org/articles/arduino/switch-and-web-page-button-LED-control/
https://github.com/MaJerle/ESP8266_AT_Commands_parser/blob/master/00-ESP8266_LIBRARY/esp8266.c
http://www.diygoodies.org.ua/?p=827

-----------------------------------------------------------------------------*/

#include "esp_at.h"


// size of buffer used to capture HTTP requests
#define RX_BUF_SIZE   1024
#define TX_BUF_SIZE   1024
#define TX_SEND_SIZE  2048 // data block size to send

#define MAX_SOCKETS 5

//-----------------------------------------------------------------------------
#define OK 1

//-----------------------------------------------------------------------------
customParseFunctPtr customParse;
voidFuncPtr customProcess;
//-----------------------------------------------------------------------------
typedef enum
{
	ESP_OFF=0,
	ESP_CONNECTED,
	ESP_FLUSH
}
esp_sock_status_t;
esp_sock_status_t sockStatus[MAX_SOCKETS];

typedef enum
{
	ESP_NO_REPLY=0,
	ESP_REPLY_OK,
	ESP_REPLY_READY,
	ESP_REPLY_TX_START,
	ESP_REPLY_TX_END,
	ESP_REPLY_GOT_IP,
	ESP_REPLY_ERROR,
	ESP_REPLY_FAIL,
	ESP_REPLY_NOK,
}
esp_tx_reply_t;
esp_tx_reply_t txReply; // reply to be expected for the last Tx command

const char * replies[] = { "", "OK", "ready", ">", "SEND OK", "WIFI GOT IP", "ERROR", "FAIL" };

uint16_t retVal; // succes or fail to receive txReply

typedef enum
{
	SEND_NOTHING=0,
	SEND_NOT_FOUND,
	SEND_FAVICON,
	SEND_INDEX_PAGE,
	SEND_CUSTOM,

} esp_tx_mode_t;
esp_tx_mode_t txMode[MAX_SOCKETS]; // Tx mode

uint16_t txSocket; // current Tx socket
uint16_t rxSocket; // last/currently active Rx socket

char rxBuf[RX_BUF_SIZE]; // buffer for received data from ESP 
uint16_t rxIndex; // index of rxBuf[]

char txBuf[TX_BUF_SIZE]; // command buffer using sprintf

static char dbg[256]; // for debug sprintf
#if DEBUG>0
#define PRINTNL { Serial.write('\n'); }
#define PRINTF(...) { Serial.print(millis()); Serial.write('>'); Serial.write(' '); sprintf(dbg, __VA_ARGS__); Serial.print(dbg); }
#else
#define PRINTNL {}
#define PRINTF(...) {}
#endif
bool debugWriteChar;
uint8_t rxMode;
//-----------------------------------------------------------------------------
const char header_NOK[]="HTTP/1.1 404 Not Found\r\n"
						"Content-type: text/plain\r\n"
						"Connection: close\r\n\r\n"
						"page not found.\r\n";

const char header_icon[]="HTTP/1.1 200 OK\r\n"
						"Content-type: image/x-icon\r\n\r\n";

const char header_html[]="HTTP/1.1 200 OK\r\n"
						"Content-type: text/html\r\n"
						"Connection: close\r\n\r\n";

const char fav_icon[]={
#include "favicon.h"
};

const char index_html[]={
#include "index_htm.h"
};
//-----------------------------------------------------------------------------
// Parse incomming HTTP GET request
//-----------------------------------------------------------------------------
void HTTP_ParseRequest(uint16_t sock, char * p)
{
	esp_tx_mode_t tx_mode = SEND_NOTHING;

	if ( p[0]=='G' && p[1]=='E' && p[2]=='T' ) // "GET / HTTP/1.1"
	{
		//debug << millis() << "> Rx: GET\n";
		// get request data
		if ( (p = strtok(p+4, " "))==NULL ) {
			PRINTF("Wrong HTTP request format!\n"); return; }
		// now we have the request
		p++;
		if ( *p==0 ) // '/' only
		{
			PRINTF("Rx: index page\n");
			tx_mode = SEND_INDEX_PAGE;
		}
		else if ( strstr(p, "favicon.ico") ) {
			PRINTF("Rx: favicon\n");
			tx_mode = SEND_FAVICON;
		}
		else if ( customParse!=NULL ) // process custom string
		{
			customParse(p);
			tx_mode = SEND_CUSTOM;
		}
		else
		{
			PRINTF("Rx: Invalid HTTP data: %s\n", p);
			tx_mode = SEND_NOT_FOUND;
		}
	}

	txMode[sock] = tx_mode;
}
//-----------------------------------------------------------------------------
void ESP_CheckTx(void)
{
	for (txSocket = 0; txSocket<MAX_SOCKETS; txSocket++)
	{
		if ( sockStatus[txSocket]>ESP_OFF )
		{
			esp_tx_mode_t tx_mode = txMode[txSocket];
			txMode[txSocket] = SEND_NOTHING; // allow new connection during sending data

			if ( tx_mode==SEND_NOTHING ) continue;
			if ( tx_mode>SEND_FAVICON )
			{
				PRINTF("Tx: OK header\n");
				ESP_SendData(header_html, sizeof(header_html)-1, 0);
				if ( retVal==0 ) continue; //goto end;
			}

			switch ( tx_mode )
			{
				case SEND_FAVICON: // send favicon
				{ // send favorit.ico in raw format included via "favicon.h"
					PRINTF("Tx: favicon header\n");
					ESP_SendData(header_icon, sizeof(header_icon)-1, 0);
					if ( retVal==0 ) break;
					PRINTF("Tx: favicon data\n");
					ESP_SendData(fav_icon, sizeof(fav_icon), 1);
					break;
				}
				case SEND_INDEX_PAGE:
				{ // send the index HTML page in raw format included via "index_htm.h"
					PRINTF("Tx: index page\n");
					ESP_SendData(index_html, sizeof(index_html), 1);
					break;
				}
				case SEND_CUSTOM: // send the LED status
				{
					if ( customProcess!=NULL ) customProcess();
					break;
				}
				case SEND_NOT_FOUND: // send error page
				default:
				{
					PRINTF("Tx: NOK header\n");
					ESP_SendData(header_NOK, sizeof(header_NOK)-1, 1);
					break;
				}
			}
		}
		else
			txMode[txSocket] = SEND_NOTHING;
	}
}
//-----------------------------------------------------------------------------
void ESP_CheckRx(void);
//-----------------------------------------------------------------------------
void ESP_GetReply(esp_tx_reply_t rep)
{
	uint16_t tmp = 0;
	rxIndex = 0;
	uint32_t time = millis();
	while ( (millis()-time)<5000 ) // give 5 second time to get reply
	{
		ESP_CheckRx();
		if ( tmp!=rxIndex )
		{
			tmp = rxIndex;
			time = millis();
			//debug << "> last Rx chars: " << (rxBuf+rxIndex-3) << endl;
			if ( strstr(rxBuf, replies[rep]) )
				break;
		}
	}
	if ( rxIndex==0 )
		PRINTF("ESP_GetReply timeout !!!\n");
}
//-----------------------------------------------------------------------------
void ESP_WaitReply(esp_tx_reply_t rep)
{
	txReply = rep;
	retVal = 0;
	uint32_t time = millis();
	while ( retVal==0 )
	{
		if ( (millis()-time)>5000 )
		{
			PRINTF("ESP_WaitReply timeout !!!\n");
			break;
		}
		ESP_CheckRx();
		// break if the socket gets closed meanwhile by the host
		if ( txSocket<MAX_SOCKETS && sockStatus[txSocket]==ESP_OFF ) break;
	}
	txReply = ESP_NO_REPLY;
}
//-----------------------------------------------------------------------------
void ESP_SendCommand(const char * cmd, esp_tx_reply_t rep)
{
	if ( rxMode )
		PRINTF("CMD: %s\n", cmd);
	esp.print(cmd);
	if ( rxMode )
		ESP_WaitReply(rep); // reply to be expected
	else
		ESP_GetReply(rep); // leave time for ESP reply
}
//-----------------------------------------------------------------------------
#define cmdBuf (&dbg[128]) // buffer for command
//-----------------------------------------------------------------------------
void ESP_SendData(const char * rpl, uint16_t len, uint8_t close)
{
	while ( len>0 ) {
		uint16_t size = len;
		if ( size>TX_SEND_SIZE ) size = TX_SEND_SIZE;
		sprintf(cmdBuf, "AT+CIPSEND=%d,%d\r\n", txSocket, size);
		ESP_SendCommand(cmdBuf, ESP_REPLY_TX_START); // reply to be expected
		if ( retVal==0 ) goto return2;
		esp.write(rpl, size);
		ESP_WaitReply(ESP_REPLY_TX_END); // reply to be expected
		if ( retVal==0 ) goto return2;
		rpl += size;
		len -= size;
	}
	if ( close ) {
return2:
		// close socket
		sprintf(cmdBuf, "AT+CIPCLOSE=%d\r\n", txSocket);
		ESP_SendCommand(cmdBuf, ESP_REPLY_OK); // reply to be expected
	}
}
//-----------------------------------------------------------------------------
const char delim[] = ",:";
//-----------------------------------------------------------------------------
uint8_t ESP_ParseLine()
{
	rxIndex = 0;
	//debug << "> txReply: " << txReply << ", rxSocket: " << rxSocket
	//		<< ", status: " << ((rxSocket<MAX_SOCKETS)?sockStatus[rxSocket]:-1) << endl;

	if ( rxBuf[0]==0 ) // empty line
	{
		if ( rxSocket<MAX_SOCKETS ) //&& sockStatus[rxSocket]==ESP_FLUSH )
		{
			//debug << millis() << ("> flush end socket ") << rxSocket << endl;
			debugWriteChar = true;
		}
		goto end;
	}
	// check socket connect or close
	if ( rxBuf[1]==',' )
	{
		if ( strcmp(rxBuf+2, "CONNECT")==0 ) // socket open
		{
			uint8_t sock = atoi(rxBuf);
			//debug.print("> open sock: "); debug.println(sock);
			if ( sock<MAX_SOCKETS )
			{
				rxSocket = sock;
				sockStatus[sock] = ESP_CONNECTED;
				PRINTF("connect socket %u\n", sock);
			}
			else // should not happen
			{
				rxSocket = MAX_SOCKETS;
				PRINTF("Invalid socket: %u\n", sock);
			}
		}
		else
		if ( strcmp(rxBuf+2, "CLOSED")==0 ) // socket close
		{
			uint8_t sock = atoi(rxBuf);
			if ( sock==rxSocket) rxSocket = MAX_SOCKETS;
			if (sock<MAX_SOCKETS )
			{
				sockStatus[sock] = ESP_OFF;
				PRINTF("close socket %u\n", sock);
			}
			else // should not happen
				PRINTF("closing INVALID socket %u\n", sock);
		}
		goto end;
	}
	// check for any reply
	if ( txReply>ESP_NO_REPLY && retVal==0 )
	{
		if ( txReply==ESP_REPLY_OK )
		{
			if ( rxBuf[0]=='O' && rxBuf[1]=='K' ) { // OK
				PRINTF("reply: OK\n");
				retVal = OK;
			}
		}
		else
		if ( txReply==ESP_REPLY_TX_START)
		{
			if ( rxBuf[0]=='>' ) { // ">"
				PRINTNL; PRINTF("reply: >\n");
				retVal = OK;
			}
		}
		else
		if ( strcmp(rxBuf, replies[txReply])==0 )
		{
			PRINTF("reply: %s\n", replies[txReply]);
			retVal = OK;
		}
		if ( retVal==OK )
			goto end; // don't process any incomming data
	}
	// check here new incoming data
	//debug.print("> rxBuf: "); debug.println(rxBuf);
	if ( rxBuf[0]=='+' ) {
		char * p;
		if ( (p = strtok(rxBuf+1, delim))==NULL ) {
			PRINTF("ERROR: Wrong response tag!\n"); goto fail; }

		if ( p[0]=='I' && p[1]=='P' && p[2]=='D') // +IPD:x,y
		{
			// data received from client
			if ( (p = strtok(NULL, delim))==NULL ) {
				PRINTF("ERROR: Wrong client channel!\n"); goto fail; }

			uint16_t sock = atoi(p);
			//debug.print("> Client channel: "); debug.println(sock);
			rxSocket = sock;
			if ( sockStatus[sock]!=ESP_CONNECTED ) {
				PRINTF("ERROR: Socket not connected!\n"); goto fail; }

			if ( (p = strtok(NULL, delim))==NULL ) {
				PRINTF("ERROR: Wrong data length!\n"); goto fail; }
			//uint16_t dataLen = atoi(p);
			//debug.print("> Data length: "); debug.println(dataLen);

			if ( (p = strtok(NULL, delim))==NULL ) {
				PRINTF("EROR: Wrong payload!\n"); goto fail; }
			// parse payload
			HTTP_ParseRequest(sock, p);
			//sockStatus[rxSocket] = ESP_FLUSH; // flush the rest of header
			debugWriteChar = false;
		}
		else
		{
			// add here other possible tokens starting with '+'
		}
	}

fail:
end:
	if ( !debugWriteChar )
		return 1;
	else
		return 0;
}
//-----------------------------------------------------------------------------
void ESP_ParseData(void)
{
	char c = 0;
	uint32_t t = millis();
	while ( (millis()-t)<2 ) // timed reception: max 2 millis gap between characters
	{
		if ( esp.available() )
		{
			t = millis();
			c = esp.read();
#if DEBUG>0
			if ( debugWriteChar )
			//if (rxMode==0)
				debug.write( c );
#endif
			if ( c=='\n' && rxMode )
				c = 0;
			else if ( c<' ' )
				continue; // disregard any other control character

			if ( rxIndex==1 && c==' ' && rxBuf[0]=='>' ) // is "TX start" response?
				c = 0;

			if ( rxIndex>(RX_BUF_SIZE-1) ) 
			{
				rxIndex = (RX_BUF_SIZE-1);
				c = 0;
			}
			rxBuf[rxIndex++] = c;

			if ( c==0 && rxMode )
				if ( ESP_ParseLine() ) break; // disregard (flush) the rest
			
		}
	}
}
//-----------------------------------------------------------------------------
void ESP_CheckRx(void)
{
	if ( esp.available() ) ESP_ParseData();
}
//-----------------------------------------------------------------------------
void DEBUG_CheckRx(void)
{
	if ( !debug.available() ) return;
	uint32_t t = millis();
	while ( (millis()-t)<2 ) // timed reception: max 2 millis gap between characters
	{
		while ( debug.available() )
		{
			t = millis();
			char c = debug.read();
			//debug.write(c); // echo
			esp.write(c);
		}
	}
}
//-----------------------------------------------------------------------------
char DEBUG_GetChar(void)
{
	//while ( !debug.available() ) while ( esp.available() ) debug.write(esp.read());
	char c = 0;
	uint32_t t = millis();
	while ( (millis()-t)<5000 ) // max 5 secs to enter character
	{
		if ( debug.available() )
		{
			c = debug.read();
			break;
		}
		else while ( esp.available() ) debug.write(esp.read());
	}
	while ( debug.available() ) debug.read(); // flush
	return c;
}
//-----------------------------------------------------------------------------
uint16_t DEBUG_GetLine(char * inStr)
{
	uint16_t cnt = 0;
	uint32_t t = millis();
	while ( (millis()-t)<30000 ) // max 30 secs to enter character
	{
		if ( debug.available() )
		{
			t = millis();
			char c = debug.read();
			//debug.write(c);
			if (c=='\r' || c=='\n') c = 0;
			*inStr++ = c;
			if ( c==0 ) break;
			cnt ++;
		}
		else while ( esp.available() ) debug.write(esp.read());
	}
	while ( debug.available() ) debug.read(); // flush
	return cnt;
}
//-----------------------------------------------------------------------------
void ESP_setup(customParseFunctPtr custom_Parse, voidFuncPtr custom_Process)
{
	customParse = custom_Parse;
	customProcess = custom_Process;
	rxIndex = 0;
	rxBuf[0] = 0; // invalid data
	retVal = 0;
	debugWriteChar = true;
	txSocket = rxSocket = MAX_SOCKETS;
	for (uint8_t i = 0; i<MAX_SOCKETS; i++) sockStatus[i] = ESP_OFF;

	Serial1.begin(115200);       // for communicating with ESP-01
	debug.begin(115200); // Serial
	//while (!debug); delay(100);

	PRINTNL; PRINTF("System powered up!\n");

	rxMode = 0; // 0=no Rx parsing, 1=line-by-line parsing mode

	ESP_SendCommand("AT+RST\r\n", ESP_REPLY_READY); // reset
	
	ESP_SendCommand("ATE0\r\n", ESP_REPLY_OK); // disable ECHO

	ESP_SendCommand("AT+CWMODE=3\r\n", ESP_REPLY_OK); // enable Soft AP mode

/*
	ESP_SendCommand("AT+CWMODE_CUR?\r\n", ESP_REPLY_OK); // get default mode
	ESP_SendCommand("AT+CWAUTOCONN?\r\n", ESP_REPLY_OK); // get autoconn status
	ESP_SendCommand("AT+CWDHCP_CUR?\r\n", ESP_REPLY_OK); // get DHCP status
	ESP_SendCommand("AT+CWMODE=3\r\n", ESP_REPLY_OK); // enable station mode
	ESP_SendCommand("AT+CWSAP=\"ESP8266\",\"1234567890\",1,4\r\n", ESP_REPLY_OK); // enable station mode
	ESP_SendCommand("AT+CIPAP=\"192.168.6.100\",\"192.168.6.1\",\"255.255.255.0\"\r\n", ESP_REPLY_OK); // enable station mode
*/

	PRINTNL; PRINTF("Current AP configuration:\n");
	ESP_SendCommand("AT+CWJAP_CUR?\r\n", ESP_REPLY_OK); // get current AP config

	ESP_SendCommand("AT+CIPAP_CUR?\r\n", ESP_REPLY_OK); // get current AP config
/*
	ESP_SendCommand("AT+CIPAPMAC_CUR?\r\n", ESP_REPLY_OK); // get current AP MAC
	ESP_SendCommand("AT+CWLIF\r\n", ESP_REPLY_OK); // get connected IP address
*/
	rxMode = 1; // switch back to line-by-line parsing mode

	PRINTNL;
	PRINTF("Now starting WEB server...\n\n");
	ESP_SendCommand("AT+CIPMUX=1\r\n", ESP_REPLY_OK); // enable multiple connections

	ESP_SendCommand("AT+CIPSERVER=1,80\r\n", ESP_REPLY_OK); // web server at port 80

	ESP_SendCommand("AT+CIPSTO=5\r\n", ESP_REPLY_OK); // server timeout 5 seconds
	
// AT+CIPSNTPCFG?  // Sets the Configuration of SNTP: AT+CIPSNTPCFG=1,8,"cn.ntp.org.cn","ntp.sjtu.edu.cn","us.pool.ntp.org"
// AT+CIPSNTPTIME? // Checks the SNTP Time
// AT+CIFSR        // Gets the Local IP Address

}
//-----------------------------------------------------------------------------
void ESP_loop()
{
	DEBUG_CheckRx();
	ESP_CheckRx();
	ESP_CheckTx();
}
