
/* ENP32_SNMP_Traffic_Counter.ino
This example uses SNMP to poll the number of packets flowing in and out of the ports on a single Aruba 2540 network Switch.  (with a capital S because "switch" is a a function or something)
We are using an ESP32 here, but it could be updated for an ESP8266 easily enough, but the number of ports we can poll will be smaller - I think.

You will need to add your wifi SSID and password, as well as the IP address of your router/Switch, and the number of ports you want to poll, from 1 to whatever.
Note that I could only seem to poll up to 30 ports at a time without crashing the esp32 device I am using. Your mileage may vary.
*/

#include "Arduino.h"
#include "WiFi.h" // ESP32 Core Wifi Library
#include <WiFiUdp.h> // enables UDP protocol
#include <Arduino_SNMP_Manager.h> 

// Your WiFi info 
const char *ssid = "YourSSID";
const char *password = "YourWifiPassword";

// Insert your SNMP Device Info 
IPAddress Switch(192, 168, 0, 1);  // must capitalize the letter S for our variable
const char *community = "public"; // if different from the default of "public"
const int snmpVersion = 1; // SNMP Version 1 = 0, SNMP Version 2 = 1

// How many ports?
const int numberOfPorts = 30; // Set this to the desired number of ports, e.g., 30 in this case
// CAUTION: We seem to hit a maximum of how many ports can be polled at one time. if you experience device reboots, you are probably asking for too many OIDs
// How often should you poll the device? 
int pollInterval = 10017; // polling interval (delay) in milliseconds - 1000 = 1 second  ( I set it to a non round number so I could see the uptime change easier)

// The above items SHOULD be all you would need to change.   - provided your network switch uses the same OID's for packet counters.
// ---------------------------------------------------

// Now we set up our OID (Object Identifiers) variables for the items we want to query on our Switch
// If you don't know what SNMP, MIBs and OIDs are, you can learn more about them here https://www.paessler.com/info/snmp_mibs_and_oids_an_overview
// We'll use arrays for some to store the multiple values of our lastInOctets, responseInOctets and oids
const char *oidSysName = ".1.3.6.1.2.1.1.5.0";       // This is the OID string we query to get the system name (SysName) of the Switch. 
const char *oidUptime = ".1.3.6.1.2.1.1.3.0";        // This OID gets us the uptime of the Switch (hundredths of seconds)
unsigned int responseInOctets[numberOfPorts] = {0};  // This will create a resizable array as big as the numberOfPorts we want to poll established above.
unsigned int responseOutOctets[numberOfPorts] = {0}; // We need arrays for in and out. 
unsigned int lastOutOctets[numberOfPorts] = {0};     // The 'response' arrays will store the data we get from our query, and the 'last' arrays store the value
unsigned int lastInOctets[numberOfPorts] = {0};      // from the last time it was polled so we can compare against.
unsigned int in[numberOfPorts] = {0};                // These two items store the difference. 
unsigned int out[numberOfPorts] = {0}; 
const char* oidInOctets[numberOfPorts];  // We will need to populate this array with the OID strings for the ifInOctets (and out) for each of our ports
const char* oidOutOctets[numberOfPorts]; // and we have to do that in setup 
char sysName[50]; // empty string thats big enough for 50 characters I guess
char *sysNameResponse = sysName; // will be replaced once we get a response
unsigned int uptime = 0; 
unsigned int lastUptime = 0; 
unsigned long pollStart = 0;
unsigned long intervalBetweenPolls = 0;

// SNMP Objects
WiFiUDP udp;                                           // UDP object used to send and receive packets
SNMPManager snmp = SNMPManager(community);             // Starts an SNMPManager to listen to replies to get-requests
SNMPGet snmpRequest = SNMPGet(community, snmpVersion); // Starts an SNMPGet instance to send requests
// Blank callback pointer for each OID
ValueCallback *callbackInOctets[numberOfPorts] = {0};  // These are the callback handlers for the various items we are gathering
ValueCallback *callbackOutOctets[numberOfPorts] = {0};
ValueCallback *callbackSysName;
ValueCallback *callbackUptime;

// Declare some empty functions? not sure why we do this... 

// void createArrays();
void getSNMP();
void doSNMPCalculations();
void printVariableValues();

void setup()
{
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("");
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print("."); //print a row of dots to indicate connection progress......
  }
  Serial.println("");
  Serial.print("Connected to SSID: ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  snmp.setUDP(&udp); // give snmp a pointer to the UDP object
  snmp.begin();      // start the SNMP Manager

  // Get callbacks from creating a handler for each of the OID
  for (int i = 0; i < numberOfPorts; ++i) {
    std::string oidInStr = ".1.3.6.1.2.1.2.2.1.16." + std::to_string(i + 1); // create the list of inOids 
    std::string oidOutStr = ".1.3.6.1.2.1.2.2.1.10." + std::to_string(i + 1); // create the list of outOids
    oidInOctets[i] = oidInStr.c_str();
    oidOutOctets[i] = oidOutStr.c_str();
    callbackInOctets[i]= snmp.addCounter32Handler(Switch, oidInOctets[i], &responseInOctets[i]); // create callbacks array for the OID
    callbackOutOctets[i]= snmp.addCounter32Handler(Switch, oidOutOctets[i], &responseOutOctets[i]); // create callbacks array for the OID
  }
  callbackSysName = snmp.addStringHandler(Switch, oidSysName, &sysNameResponse);
  callbackUptime = snmp.addTimestampHandler(Switch, oidUptime, &uptime);
}

void loop()
{
    snmp.loop();
    intervalBetweenPolls = millis() - pollStart;
    if (intervalBetweenPolls >= pollInterval)
    {
      pollStart += pollInterval; // this prevents drift in the delays
      getSNMP();
      printVariableValues(); // Print the values to the serial console
      doSNMPCalculations(); // Do something with the data collected
    }
}

void getSNMP()
{
  // Build a SNMP get-request add each OID to the request
  for (int i = 0; i < numberOfPorts; ++i) {
  snmpRequest.addOIDPointer(callbackInOctets[i]);
  snmpRequest.addOIDPointer(callbackOutOctets[i]);
  }
  snmpRequest.addOIDPointer(callbackSysName);
  snmpRequest.addOIDPointer(callbackUptime);
  snmpRequest.setIP(WiFi.localIP()); // IP of the listening MCU
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(rand() % 5555);
  snmpRequest.sendTo(Switch);
  snmpRequest.clearOIDList();
}


void printVariableValues()
{ // just the header really.
    Serial.print("My IP: ");
    Serial.println(WiFi.localIP());
    Serial.printf("Polling Device: %s\n", sysNameResponse);
    Serial.printf("Uptime: %d\n", uptime);
    Serial.println("----------------------");
}

void doSNMPCalculations()  // this prints out the polled values after processing
{
  if (uptime == lastUptime)
  {
    Serial.println("Data not updated between polls");
    return;
  }
  else if (uptime < lastUptime)
  { // Check if device has rebooted which will reset counters
    Serial.println("Uptime < lastUptime. Device probably restarted");
  }
  else
  {
    // We will be receiving a number of snmp responses from our Switch reporting how many octets of data were received and sent for each of the polled ports. 
    // We will query anywhere from one to 48 ports, and assign each response to a variable such as "responseInOctets[0]" for  port 1, and so on.
    // for each of our responses, as responseInOctets[#] subtract lastInOctets[#] from it and assign it to a variable in[#]
    // then print the variable in#, and do the same to out while we are at it.
    for (int i = 0; i < numberOfPorts; ++i) {
      in[i] = responseInOctets[i]-lastInOctets[i];
      out[i] = responseOutOctets[i]-lastOutOctets[i];
      Serial.print("Port "); 
      Serial.print(i+1); 
      Serial.print(" In: ");
      Serial.print(in[i]);
      if (in[i] > 999){  // if we want the values to appear in tody little columns in the serial monitor, we use two tabs for values under 1000, or one tab for above.
        Serial.print(" \tOut: ");
      } else {
        Serial.print(" \t\tOut: ");// \t is a tab. :)
      }
      Serial.println(out[i]);
      lastInOctets[i] = responseInOctets[i];
      lastOutOctets[i] = responseOutOctets[i];
    }
    Serial.print("----- elapsed: ");
    Serial.print(uptime - lastUptime);
    Serial.println(" -----");
  }
  // Update last samples
  lastUptime = uptime;
}

