// Arduino - BANG-detection.ino
//
// Firework BANG detector
// Using the ESP-12E module / ESP node mcu (v3.0)
// Sound detection via MAX4466 module, maximum volume ouput
// GPS-module, for correct current time with millisecond precision
// PSU MAX, GPS and Pulse via 3V3 of ESP
//
// My personal MAX/ESP setup has a silent level of 545
//
// (cl) by Arno Jacobs  - 2020-12-05  version 0.3   Web server with BANG detection list
//                                                  Based on (standard) ESP web server
//                                                  Removed most of the Serial I/O
//                                                  Only my-IP over serial
//                                                  Web data only        
//                        2020-12-09  version 0.4   Changes in listen routine.
//                                                  Only record the 'louder' noises, not the zero-crossings.
//                                                  How to switch off the ESP-F50A5D access point?
//                                                  OR how to close it with security (password)
//                        2020-12-11  version 0.5   After adding 'getSilentLevel' some changes there
//                                                  (Used for testing 3 new ESP-modules and 5 mic-modules)
//                                                  (All ESP-modules 0K.)
//                        2020-12-17  version 0.6   Again, rescaling audio detection levels - redone scaling
//                                                  (Older versions did not register (loud) bangs outside the house.)
//                        2021-02-22  version 0.7   Adding NTP - how does it work?
//                        2021-03-06  version 0.8   Adding GPS module
//                                                  (Removed NTP - not accurate enough)    
//                                                  The one second sync from GPS to ESP at D1 or GPIO5   
//                                                  Serial input on ESP at D7 or GPIO13 (RXD2)
//                                                  Check LED at D2 or GPIO4
//                        2021-03-07  version 0.9   "Fixing" time sync pulse measurement by ESP32
//                        2021-03-08  version 0.10  Added test pulse input at D0 or GPIO16. 
//                                                  This for testing multiple ESP modules reacting on and timing of one pulse.
//                                                  Now saved under: "BANG-detection-test-pulse-sync" 
//                        2021-03-08  version 0.11  Listen for BANG and get correct current time via GPS module 
//                                                  Now using the GPS module to get the correct BANG time 
//
//
                                                   


// Load Wi-Fi library
#include <ESP8266WiFi.h>          // ESP node code
#include <SoftwareSerial.h>       // Serial input from GPS module

// software serial #2: RX = GPIO 13, TX = GPIO 15
SoftwareSerial gps_port(13,15);

int time_sync_pin = 4;      // GPIO4  -> D2
int test_LED      = 5;      // GPIO5  -> D1
// The test led will be ON at start.
// As soon as a BANG is detected, the time is retrieved from the GPS module. 
// The time reading is indicated by LED-ON at start and LED-OFF at the end of that function.
// So the LED will be OFF after time registration after a BANG.
//


IPAddress local_IP(192,168,178,35);   // 32, 33, 34 or 35 for Arno's local
// Module marked 'V' at 192.168.178.32
// Module marked '+' at 192.168.178.33
// Module marked ':' at 192.168.178.34
// Module marked '&' at 192.168.178.35 -- this is the oldest one, sometimes with buggy issues

IPAddress gateway(192,168,178,1);
IPAddress subnet(255,255,255,0); 

// Replace with your network credentials
const char* ssid        = "MySSID";
const char* password    = "password";      // Is "fixed" in one of my modules...
/// const int   schannel    = 7;


// That number has to be in my code ;-)
// just the number...
const int theUltimateNumber = 42;
const int theReversedOne    = 24;


// Silent (or middle) signal in my MAX4466/ESP-12E setup @ 3V3
// Tested with ESP-module with green V coding and mic with green _ coding
int testedSilent = 540;
int salevel = 0;
// This can be changed after silent test at setup
// If NO silent testing is done than testedSilent will not change and
// there is NO report of the unscaled silent noise on the web page
//

// Some measuremnts with the one mic with green _ coding
// ESP-module with green V coding: 544
// ESP-module with green & coding: 541
// ESP-module with green : coding: 534
// ESP-module with green + coding: 548


// The pin input for the audio signal of the MAX4466 module
const int Audio_pin = A0;

// Set web server port number to 80
WiFiServer server(80);

// Array with data of last 25 bangs (1+1) extra for memory safety...
const int minAVGsnd = 999;
const int maxBangs = theUltimateNumber;
int bang[maxBangs+1];
unsigned long timeRecord[maxBangs+1];
int nBang;



  
void getSilentLevel() {
  //Check zero level
  delay(2000);
  int samples = 100;
  for (int t=0; t<samples; t++) {
      salevel += analogRead( Audio_pin); 
  }
  salevel /=  samples;
  testedSilent = salevel;
}



/*
 * The one second pulse from the GPS module has a HIGH for 100 milliseconds and a low for 900 milliseconds.
 * We will pick te moment the HIGH starts.
 */
unsigned long get_TSP_time_sync_millis() {
    // The return value
    unsigned long ts_ms = 0;
    int tsp = digitalRead( time_sync_pin);

    // Wait for START of sync signal
    while (tsp == HIGH) { 
        tsp = digitalRead( time_sync_pin); 
    }    
    delay (1);
    // The pulse we need has to go from LOW to HIGH

    while (tsp == LOW) {
        tsp = digitalRead( time_sync_pin);  
    }
      
    ts_ms = millis();             
    return (ts_ms);
}         
 


unsigned long get_GPS_time (unsigned long ref_time) {
    digitalWrite(test_LED,HIGH);

    // Get millis() at first HIGH of one second pulse GPS module
    unsigned long tsp_time_ms = get_TSP_time_sync_millis();

    // And now is the time to read the serial port for the time
    // like "$GPRMC,100227,00"

    // **** *** **** *** **** *** **** *** **** *** **** *** **** *** **** *** **** *** **
    // At this stage - NO PARSING - Just get the first 16 characters and strip time info
    
    // First Clear buffer - serial port GPS module
    while (gps_port.available()) { gps_port.read(); }

    String gps_data = "";
    int cc = 16;                              // Read the first 16 characters...
    while (cc > 0) {
        while (gps_port.available() > 0) {    // Only read a byte if at least one is in it's buffer.
            char cb = gps_port.read();
            gps_data += cb;                   // read byte by byte - if available...
            cc--;
        }
    }
    
    int htime1 = gps_data.substring(7,13).toInt();
    int ht_sec = htime1 % 100;
    int htime2 = htime1 / 100;
    int ht_min = htime2 % 100;
    int ht_hrs = htime2 / 100; 

    // The end of NO PARSING ;-)
    // **** *** **** *** **** *** **** *** **** *** **** *** **** *** **** *** **** *** **

    // UTC time in milliseconds. 
    // The first moment of recording of the BANG
    unsigned long gps_time_ms = (1000 * (ht_sec + (60 * ht_min)  + (3600 * ht_hrs))) - (tsp_time_ms - ref_time); 

    digitalWrite(test_LED,LOW);    
    return (gps_time_ms);
}


// Scaling of the analog input data
// The silent level will brought down to zero level
// All signals will convert to absolute values
int scaleAnalog(int aval) {
    int rv = (aval - testedSilent);
    return ((rv*rv)/10);
}

// Read the analog port 
// Listen for BANG
// If one loud pulse is detected a series of 100 more audiosamples will be recorded
// Only if those samples are loud enough the noise will be stored 
//
void listen_mic() {

    int AvalS = scaleAnalog( analogRead( Audio_pin));   // 'Constant' read analog port

    // At first - only test for any LOUD noise
    // Scaled to levels > 400 (about the level of normal speaking into the mic at about half a meter)
    if (AvalS > theUltimateNumber * 10)
    {
        unsigned long bang_time_ms = millis();
        
        // Record new sound samples and simply sum them together
        // Try to record "high level" signals only
        int asum = 0;
        int sc = 0;
        for (int t=0; t < theUltimateNumber * 2; t++) { 
            AvalS = scaleAnalog( analogRead( Audio_pin));
            if (AvalS > theUltimateNumber * 6) {
                asum += AvalS;
                sc++;
             }
        }
        
        int avgSnd = theUltimateNumber;
        if (sc > 0) {
            // mean value of the recorded 'louder' noises.
            avgSnd = asum / sc;    
        }

        if (avgSnd > theUltimateNumber * theReversedOne) {             // Skip the short spikes...
            // Store the samples in a list with maximum length
            // At maximum list length shift the stack
            // Drop the first element of the list, shift the list
            // and add the new sample at the end of the list

            // ********* *****  ********* *****  ********* *****  ********* *****  *** ********* *****  ********* ***** *****
            unsigned long bang_time = get_GPS_time( bang_time_ms);        // Only call this routine after a bang is recorded
            // The current time in String-format is stored GLOBAL in 'gps_String_time'

            if (nBang >= maxBangs) {
                bang[nBang]       = avgSnd;                       // Store at end+1
                timeRecord[nBang] = bang_time;            
                for (int i=0; i<maxBangs; i++) { 
                    bang[i]       = bang[i+1];                    // Shift data            
                    timeRecord[i] = timeRecord[i+1];
                }
            } else {
                bang[nBang]       = avgSnd;
                timeRecord[nBang] = bang_time;
                nBang++;  
            }
        }
        // Wait at least a second for next possible BANG detection
        delay ( theUltimateNumber * theReversedOne );
    } 
    
    // easy going -- with loop of listen to bang and listen to client-request
    delay (theUltimateNumber); 
}


String show_time (unsigned long ulTime) {
    char rt[16];

    int ms = ulTime % 1000;
    int s  = (ulTime / 1000) % 60;
    int m  = (ulTime / 60000) % 60;
    int h  = (ulTime / 3600000);

    sprintf(rt,"%i:%02i:%02i,%03i",h,m,s,ms);
    return(rt);
}


// *** **** **  *** **** **  *** **** **  *** **** **  *** **** **  *** **** **  *** **** **  *** **** **  *** **** ** 
// The main setup
//
void setup() {

    // Serial setup for GPS module
    gps_port.begin(9600);         // Alas a bit slow...
    gps_port.listen();            // Does it corrupt interrups?

    pinMode(time_sync_pin, INPUT);
    pinMode(test_LED,OUTPUT);

    // 
    WiFi.begin(); // WiFi config was done befor with other app and WiFi-manager
    // Connect to Wi-Fi network with SSID and password
    // 
    WiFi.begin( ssid, password );
             
    // WiFi.mode(WIFI_AP);   //To Turn on WiFi in Specific Mode like WIFI_STA or WIFI_AP
    WiFi.config(local_IP, gateway, subnet);
    
    // This will shut down the AP-mode
    WiFi.mode(WIFI_STA);
    
    // Wait for the ESP module is connected with WiFi
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
    
    // Start the web-server
    server.begin ();

    for (int i=0; i<maxBangs; i++) { bang[i] = 0; } // Be sure to start clean
    // Then number of registerd Bangs, maximum 25 -> maxBangs
    nBang = 0;    

    // If needed, un-comment next line to measure the silent level
    getSilentLevel();

    // The server has started and all is set to go.
    // LED ON -- now wait for the BANGs
    digitalWrite(test_LED,HIGH);
}


// *** **** **  *** **** **  *** **** **  *** **** **  *** **** **  *** **** **  *** **** **  *** **** **  *** **** ** 
// The main loop
//
void loop() {
    // Variable to store the HTTP request
    String header;
    unsigned long currentTime   = millis();
    unsigned long previousTime  = 0; 
    // Define timeout time in milliseconds (example: 2000ms = 2s)
    const long timeoutTime      = 2000;

    // Listen for a BANG
    listen_mic ();
    
    WiFiClient client = server.available();         // Listen for incoming clients
    if (client) {                                   // If a new client connects,
        String currentLine  = "";                   // make a String to hold incoming data from the client
        currentTime         = millis();
        previousTime        = currentTime;
        while (client.connected() && currentTime - previousTime <= timeoutTime) { // loop while the client's connected
            currentTime     = millis();         
            if (client.available()) {               // if there's bytes to read from the client,
                char c = client.read();             // read a byte, then
                header += c;
                if (c == '\n') {                    // if the byte is a newline character
                // if the current line is blank, you got two newline characters in a row.
                // that's the end of the client HTTP request, so send a response:
                //
                if (currentLine.length() == 0) {
                    // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
                    // and a content-type so the client knows what's coming, then a blank line:
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-type:text/html");
                    client.println("Connection: close");
                    client.println();
                    
                    // Display the HTML web page
                    client.println("<!DOCTYPE html><html>");
                    client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
                    client.println("<link rel=\"icon\" href=\"data:,\">");
                    
                    // CSS to style web-page
                    client.println("<style>html ");
                    client.println("{ font-family: 'Trebuchet MS'; display: inline-block; margin: 0px auto; text-align: center;}");
                    client.println("table, th, td { border: 2px solid blue;}");
                    client.println("</style></head>");
                    // table with lines
                    
                    // Web Page Heading
                    client.println("<body><h1>Bang Detector</h1>");
                    client.println("<h3>2021-03-08  version 0.11</h3><br>");

                    if (salevel > 1) {
                      
                        // Show silent level noise data -- unscaled value
                        client.println( "<p>The (unscaled) silent noise level A0 pin value: " );
                        client.println( String(testedSilent) + "</p>"); 
                    }
                    
                    // Show noise data
                    client.println("<p>The last recorded loud noises</p>");

                    // <table style="width:100%">
                    client.println("<p> <table style=\"width:100%\"> ");
                    client.println("<tr> <th>bang #</th> <th>loudness</th> <th>time (in ms.)</th> <th>time (UTC)</th> </tr> ");
                    
                    for (int i=0; i<nBang; i++)
                    {
                        client.println("<tr> <td>" + String( i + 1)              +      "</td>");
                        client.println("     <td>" + String( bang[i] )           +      "</td>");
                        client.println("     <td>" + String( timeRecord[i])      + " ms. </td>");
                        client.println("     <td>" + show_time( timeRecord[i])   +      "</td> </tr>");
                    }
                    client.println("</table> </p> <br>");      

                    // End of HTML-body
                    client.println("</body></html>");

                    // Break out of the while loop -- UGLY! (I hate breaks...)                   
                    break;
                } else { // if you got a newline, then clear currentLine
                    currentLine = "";
                }
            } else if (c != '\r') {  // if you got anything else but a carriage return character,
                    currentLine += c;      // add it to the end of the currentLine
                }
            }
        }
        // Clear the header variable            -- But WHY ???
        header = "";
        // Close the connection
        client.stop();

        // Wait at least 0.1 second for next reload web-page
        delay (100);
    }
}

// End of code.

/*
 *  5 20968 36669123 ms.  10:11:09,123 (32)
 *  3 11412 36669130 ms.  10:11:09,130 (35)
 *
 * 11 21794 36828430 ms.  10:13:48,430 (32)
 *  9 19500 36828441 ms.  10:13:48,441 (35)
 * 
 *  Switched mic-modules
 *  
 *  1  2319  37051902 ms.  10:17:31,902 (32)
 *  1  15921 37051921 ms.  10:17:31,921 (35)
 * 
 * There is always at least a 10ms difference.
 * With Vs ~ 335 m/s this 10ms results in a 3,35 meter 'error'.
 * The actual error will be much larger. 
 * 
 * A mic closer to a sound source will register the bang sooner than a mic further away.
 * Not regarding it's distance but the threshold of the soundlevel.
 * 
 * So a double delay. 
 *    1.) distance to the source
 *    2.) threshold of the soundlevel
 *    
 *    
 */
 
