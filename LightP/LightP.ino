#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>


/************ WIFI and MQTT Information (CHANGE THESE FOR YOUR SETUP) ******************/
const char* ssid = "YourWifi"; //type your WIFI information inside the quotes
const char* password = "YourWifiPassword";
const char* mqtt_server = "MQTTIP";
const char* mqtt_username = "MQTTName";
const char* mqtt_password = "MQTTPassword";
const int mqtt_port = 1883;

/**************************** FOR OTA **************************************************/
#define SENSORNAME "OTAName" //change this to whatever you want to call your device                                                                                              
#define OTApassword "OTAPassword" //the password you will need to enter to upload remotely via the ArduinoIDE
int OTAport = 8266;

/************* MQTT TOPICS (change these topics as you wish)  **************************/
const char* light_state_topic = "Home/TableLamp";
const char* light_set_topic = "Home/TableLamp/set";

const char* on_cmd = "ON";
const char* off_cmd = "OFF";


/****************************************FOR JSON***************************************/
const int BUFFER_SIZE = JSON_OBJECT_SIZE(10);
#define MQTT_MAX_PACKET_SIZE 512

const uint8_t SwitchPin = 5;
const uint8_t LightUpPin = 4;
const uint8_t LightDownPin = 14;

int pre_time = 0;
int hold_time = 200;

bool ProcessEnable = false;
bool stateOn = false;
bool Onbeforestate = false;
int StateSel = -1;
byte brightness = 5;
byte Brightness = brightness;
byte subBrightness = 0;
bool stateChange = false;

WiFiClient espClient;
PubSubClient client(espClient);

void setup() 
{
  // put your setup code here, to run once:
  Serial.begin(115200);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  //OTA SETUP
  ArduinoOTA.setPort(OTAport);
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(SENSORNAME);

  // No authentication by default
  ArduinoOTA.setPassword((const char *)OTApassword);

  ArduinoOTA.onStart([]() {
	String type;
	if (ArduinoOTA.getCommand() == U_FLASH)
		type = "sketch";
	else // U_SPIFFS
		type = "filesystem";
    Serial.println("Starting" + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  pinMode(SwitchPin, OUTPUT);
  pinMode(LightUpPin, OUTPUT);
  pinMode(LightDownPin, OUTPUT);

  digitalWrite(SwitchPin, LOW);
  digitalWrite(LightUpPin, LOW);
  digitalWrite(LightDownPin, LOW);

  Serial.println("Ready");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

}

void loop()
{
	// put your main code here, to run repeatedly:
	if (!client.connected()) {
		reconnect();
	}

	if (WiFi.status() != WL_CONNECTED) {
		delay(1);
		Serial.print("WIFI Disconnected. Attempting reconnection.");
		setup_wifi();
		return;
	}

	client.loop();

	ArduinoOTA.handle();

	if (ProcessEnable)
	{
		switch (StateSel)
		{
			case 1:
				EnableSign(SwitchPin);
				break;
			case 2:
				EnableSign(LightUpPin);
				break;
			case 3:
				EnableSign(LightDownPin);
				break;
			default:
				break;
		}
		
	}

}

/********************************** START SETUP WIFI*****************************************/
void setup_wifi() 
{

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) 
{
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");

	char message[length + 1];
	for (int i = 0; i < length; i++) 
	{
		message[i] = (char)payload[i];
	}
	message[length] = '\0';
	Serial.println(message);

	if (!processJson(message)) 
	{
		Serial.println("JsonError");
		return;
	}

	sendState();
}

/********************************** START SEND STATE*****************************************/
void sendState() 
{
	StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

	JsonObject& root = jsonBuffer.createObject();

	root["state"] = (stateOn) ? on_cmd : off_cmd;
	JsonObject& color = root.createNestedObject("color");

	root["brightness"] = Brightness;

	char buffer[root.measureLength() + 1];
	root.printTo(buffer, sizeof(buffer));

	client.publish(light_state_topic, buffer, true);
}

/********************************** START PROCESS JSON*****************************************/
bool processJson(char* message)
{
	StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

	JsonObject& root = jsonBuffer.parseObject(message);

	if (!root.success())
	{
		Serial.println("parseObject() failed");
		return false;
	}

	if (root.containsKey("state"))
	{
		if (strcmp(root["state"], on_cmd) == 0) 
		{
			stateOn = true;
			if (stateOn != Onbeforestate)
			{
				Onbeforestate = true;
				StateSel = 1;
				pre_time = millis();
				ProcessEnable = true;
			}
		}
		else if (strcmp(root["state"], off_cmd) == 0) 
		{
			stateOn = false;
			if (stateOn != Onbeforestate)
			{
				Onbeforestate = false;
				StateSel = 1;
				pre_time = millis();
				ProcessEnable = true;
			}
		}
	}

	if (root.containsKey("brightness")) 
	{
		brightness = root["brightness"];
		if (Brightness > brightness && !stateChange)
		{
			//subBrightness = Brightness - brightness;
			if ((Brightness - 1)<=1)
			{
				Brightness = 1;				
			}
			else
			{
				Brightness = Brightness - 1;
			}
			
			StateSel = 3;
			pre_time = millis();
			ProcessEnable = true;
		}
		else if (Brightness < brightness && !stateChange)
		{
			if ((Brightness + 1) >= 5)
			{
				Brightness = 5;
			}
			else
			{
				Brightness = Brightness + 1;
			}
			StateSel = 2;
			pre_time = millis();
			ProcessEnable = true;
		}
	}
	else 
	{
		Brightness = brightness;
	}

	return true;
}

/********************************** START RECONNECT*****************************************/
void reconnect() 
{
	// Loop until we're reconnected
	while (!client.connected()) 
	{
		Serial.print("Attempting MQTT connection...");
		// Attempt to connect
		if (client.connect(SENSORNAME, mqtt_username, mqtt_password)) {
			Serial.println("connected");
			client.subscribe(light_set_topic);
			//setColor(0, 0, 0);
			sendState();
		}
		else {
			Serial.print("failed, rc=");
			Serial.print(client.state());
			Serial.println(" try again in 5 seconds");
			// Wait 5 seconds before retrying
			delay(5000);
		}
	}
}

bool EnableSign(int pin)
{
	int cur_time = millis(); //
	stateChange = true;
	if (cur_time - pre_time > hold_time)//
	{
		//*led_state = !(*led_state); //
		//pre_time = cur_time;//		
		ProcessEnable = false;
		stateOn = -1;
		digitalWrite(pin, LOW);
		stateChange = false;
		return 1;				
	}
	digitalWrite(pin, HIGH);
  return 0;
}

