#include<SoftwareSerial.h>
#include<LiquidCrystal.h>
#include<PS2Keyboard.h>
#include<EEPROM.h>

#define IRQ_PIN 2 //Keyboard IRQ pin
#define DATA_PIN 3 //Keyboard data pin
#define TX_PIN 4 //TX pin (Arduino to ESP)
#define RX_PIN 5 //RX pin (ESP to Arduino)
#define BIT_OFFSET 6 //Set where the address line starts
#define MAX_BITS 4 //Set the number of bits in address line
#define MAX_ADDRESS int(pow(2, MAX_BITS)) //Calculate the maximum address possible (2 ^ MAX_BITS)
#define MAX_NAME_LENGTH 16 //Sets the maximum length of a name for a device
#define state BIT_OFFSET + MAX_BITS //Set the location of switch state pin
#define enable state + 1 //Set the location of clock signal pin
#define RS 14 //Set Register Select pin of LCD
#define E 15 //Set Enable pin of LCD
#define D4 16 //Set 5th Data pin of LCD
#define D5 17 //Set 6th Data pin of LCD
#define D6 18 //Set 7th Data pin of LCD
#define D7 19 //Set 8th Data pin of LCD
#define LCD_WAIT 1000 //Set the wait time for LCD to display messages
#define LCD_LENGTH 16 //Sets the length of characters the LCD can display in one line
#define LCD_HEIGHT 2 //Sets the number of lines in LCD
#define DEFAULT_TIMEOUT 1000 //Set default timeout of ESP serial line

LiquidCrystal lcd(RS, E, D4, D5, D6, D7);
SoftwareSerial espSerial(RX_PIN, TX_PIN);
PS2Keyboard keyboard;

int address; //Stores address sent by client
boolean bitValue[MAX_BITS]; //Stores the address sent by client in bits

struct switchClass {
	boolean available; //Stores whether a device has been initialised to the switch
	boolean status; //Stores the status of each device
	char name[MAX_NAME_LENGTH]; //Stores the name of the device
}device[MAX_ADDRESS];

void softwareReset() { //Resets the uC via software
	asm volatile(" jmp 0");
}

void updateDevice(switchClass& device, String data, boolean value) { //Update the value of a device which are of type boolean
	int offset = sizeof(int); //Update the device status to Flash memory
	for(int i = 0; i < address; i++) {
		offset += sizeof(switchClass);
	}

	if(data.equals(F("available"))) {
		device.available = value;
		EEPROM.update(offset, device.available);
	} else if(data.equals(F("status"))) {
		offset += sizeof(boolean);
		device.status = value;
		EEPROM.update(offset, device.status);
	}
}

void updateDevice(switchClass& device, String data, char value[]) { //Update the value of a device which are of type char array
	int offset = sizeof(int); //Update the device status to Flash memory
	for(int i = 0; i < address; i++) {
		offset += sizeof(switchClass);
	}

	if(data.equals(F("name"))) {
		offset += sizeof(boolean) * 2;

		for(int i = 0; i < MAX_NAME_LENGTH; i++, offset += sizeof(char)) {
			device.name[i] = value[i];
			EEPROM.update(offset, device.name[i]);
		}
	}
}

void setAddressBits(int temp) { //Converts decimal to binary and stores in address bits
	for(int i = MAX_BITS - 1; i >= 0; i--) {
		if(temp % 2) {
			bitValue[i] = true;
		} else {
			bitValue[i] = false;
		}

		temp /= 2;
	}
}

void flushESPSerial() { //Flushes any previous responses in buffer
	while(espSerial.available()) {
		espSerial.read();
	}
}

boolean ATCommand(String cmd, String ack,int TIMEOUT = 0) { //Send command 'cmd' to esp8266 and wait for 'ack' string in reply for TIMEOUT milliseconds
	String reply;

	espSerial.print(cmd); //Send command to esp8266 appending \n\r to the end (CR+LF)
	espSerial.print(F("\r\n"));

	while(!espSerial.available()); //Wait for esp8266 to reply

	int currTime = millis(); //Wait for TIMEOUT milliseconds before reading reply
	while(millis() - currTime < TIMEOUT);

	while(espSerial.available()) { //Read reply from esp8266
		reply = espSerial.readString();
	}

	Serial.print(F("\n#Start##########\ncmd->")); //Print to Serial for debugging
	Serial.print(cmd);
	Serial.print(F("\nack->"));
	Serial.print(ack);
	Serial.print(F("\nreply->"));
	Serial.print(reply);
	Serial.println(F("\n############End#"));

	if(reply.indexOf(ack) != -1) { //If proper reply is recieved, return true
		return true;
	} else {
		return false;
	}
}

boolean ATCommand(String cmd, String ack, String& reply, int TIMEOUT = 0) { //Send command 'cmd' to esp8266 and wait for 'ack' string in reply for TIMEOUT milliseconds and send reply as reference
	espSerial.print(cmd); //Send command to esp8266 appending \n\r to the end (CR+LF)
	espSerial.print(F("\r\n"));

	while(!espSerial.available()); //Wait for esp8266 to reply

	int currTime = millis(); //Wait for TIMEOUT milliseconds before reading reply
	while(millis() - currTime < TIMEOUT);

	while(espSerial.available()) { //Read reply from esp8266
		reply = espSerial.readString();
	}

	Serial.print(F("\n#Start##########\ncmd->")); //Print to Serial for debugging
	Serial.print(cmd);
	Serial.print(F("\nack->"));
	Serial.print(ack);
	Serial.print(F("\nreply->"));
	Serial.print(reply);
	Serial.println(F("\n############End#"));

	if(reply.indexOf(ack) != -1) { //If proper reply is recieved, return true
		return true;
	} else {
		return false;
	}
}

void invertPin(int pin, int n) { //Inverts pin's state n times
	while(n--) {
		delay(50);
		digitalWrite(pin, !digitalRead(pin));
	}
}

void initialiseESP() { //Runs each command until proper reply is recieved from esp8266
	while(!ATCommand(F("AT+RST"), F("Ready"), 3000));
	while(!ATCommand(F("ATE0"), F("OK")));
	while(!ATCommand(F("AT+GMR"), F("OK")));
	while(!ATCommand(F("AT+CIPMUX=1"), F("OK")));
	while(!ATCommand(F("AT+CIPSERVER=1,80"), F("OK"))) {
		if(ATCommand(F("AT+CIPSERVER=1,80"), F("no change"))) {
			break;
		}
	}

	displayNetworkData();
}

void initialiseSwitches() { //Initialise all the devices using data stored in EEPROM
 	int offset = 0;
	boolean isRunBefore;
	EEPROM.get(offset, isRunBefore);
	offset += sizeof(int);

	if(!isRunBefore) {
		for(address = 0; address < MAX_ADDRESS; address++) {
			device[address].available = false;
			device[address].status = false;
			setAddressBits(address);
			processCommand();
		}

		isRunBefore = true; //Set the boolean variable to false for next run
		EEPROM.update(0, isRunBefore);
		invertPin(13, 1);
	} else {
		for(address=0; address<MAX_ADDRESS; address++) {
			EEPROM.get(offset, device[address]);
			offset += sizeof(switchClass);
			if(device[address].available) {
				setAddressBits(address);
				processCommand();
			}
		}
	}
}

boolean parseCommand(String input) { //Parse and validate the command sent by client
	Serial.print(F("****************\nInput-> ")); //Print to Serial for debugging
	Serial.println(input);
	Serial.println();

	if((input.indexOf(F("Link")) != -1) || (input.indexOf(F("Unlink")) != -1)) { //If client has opened or closed connection, do nothing
		return false;
	} else if(input.indexOf(F("Ready")) != -1) {
		Serial.println(F("!!!!!!!!ESP8266 Voltage Fluctuation Reset!!!!!!!!"));
		while(!ATCommand(F("AT+CIPMUX=1"), F("OK")));
		while(!ATCommand(F("AT+CIPSERVER=1,22"), F("OK")));
		return false;
	} else { //if this is not a client connection message, then parse the command
		String temp;
		temp.reserve(16);
		int commaLocation1 = input.indexOf(F("+IPD,")) + 4; //Client's connection number lies between the first 2 commas (ex: +IPD,0,...)
		int commaLocation2 = input.indexOf(F(","), commaLocation1);
		int replyTo = input.substring(commaLocation1 + 1, commaLocation2).toInt(); //Get connection number of client to send reply to
		int cmdLocation = input.indexOf(F("cmd=")); //Get location of GET variable 'cmd'

		if(cmdLocation != -1) {
			String cmd;
			cmd.reserve(16);
			cmd = input.substring(cmdLocation + 4); //Get the command sent by client

			if(cmd.startsWith(F("get_"))) { //If command was to get the name and current state of a switch (ex: get_128 is query status of device 128)
				address = cmd.substring(4).toInt(); //parsing out address value

				if((address > 0) && (address <= MAX_ADDRESS)) { //Checking if address is a valid value
					if(device[--address].available) {
						String tempName;
						tempName.reserve(MAX_NAME_LENGTH);
						int dataLength = MAX_NAME_LENGTH + 2;

						temp = F("AT+CIPSEND=");
						temp += String(replyTo);
						temp += F(",");
						temp += String(dataLength);
						ATCommand(temp, F(">"));

						for (int i = 0; i < MAX_NAME_LENGTH; i++) {
							tempName += device[address].name;
						}

						if(device[address].status) { //Replying to client about current switch status
							ATCommand(tempName + F(",1"), F("SEND OK"));
						} else {
							ATCommand(tempName + F(",0"), F("SEND OK"));
						}

						return false;
					} else {
						temp = F("AT+CIPSEND=");
						temp += String(replyTo);
						temp += F(",2");
						ATCommand(temp, F(">"));
						ATCommand(F("na"), F("SEND OK"));
						return false;
					}
				} else {
					temp = F("AT+CIPSEND=");
					temp += String(replyTo);
					temp += F(",5");
					ATCommand(temp, F(">"));
					ATCommand(F("error"), F("SEND OK"));
					return false;
				}
			} else if(cmd.startsWith(F("set_"))) { //If command was to set a switch's status (ex: set_128_1 is turn on device 128)
				address = cmd.substring(4, cmd.lastIndexOf(F("_"))).toInt(); //parsing out address value

				if((address > 0) && (address <= MAX_ADDRESS)) { //Checking if address is a valid value
					if(device[--address].available) {
						setAddressBits(address);

						if(cmd.substring(cmd.lastIndexOf(F("_")) + 1).toInt()) {
							updateDevice(device[address], F("status"), true);
						} else {
							updateDevice(device[address], F("status"), false);
						}

						temp = F("AT+CIPSEND=");
						temp += String(replyTo);
						temp += F(",4");
						ATCommand(temp, F(">")); //Replying to client that the switch will be turned on
						ATCommand(F("done"), F("SEND OK"));
						return true;
					} else {
						temp = F("AT+CIPSEND=");
						temp += String(replyTo);
						temp += F(",2");
						ATCommand(temp, F(">"));
						ATCommand(F("na"), F("SEND OK"));
						return false;
					}
				} else {
					temp = F("AT+CIPSEND=");
					temp += String(replyTo);
					temp += F(",5");
					ATCommand(temp, F(">"));
					ATCommand(F("error"), F("SEND OK"));
					return false;
				}
			} else if(cmd.startsWith(F("list"))) { //If command was to get a list of available switch IDs
				int dataLength;
				String data;
				data.reserve(32);

				for(int i = 0; i < MAX_ADDRESS; i++) {
					if(device[i].available) {
						int t = i + 1;
						while(t != 0) { //To count number of digits in i
							dataLength++;
							t /= 10;
						}
						data += String(i + 1);
						data += F(",");
						dataLength++; //To include length of comma character
					}
				}

				if(dataLength == 0) {
					data = "na";
					dataLength = 2;
				} else {
					data = data.substring(0, data.length() - 1); //To remove the extra comma at the end of the string added by above for loop
					dataLength--; //To remove the length of the extra comma
				}

				temp = F("AT+CIPSEND=");
				temp += String(replyTo);
				temp += F(",");
				temp += String(dataLength);
				ATCommand(temp, F(">"));
				ATCommand(data, F("SEND OK"));

				return false;
			} else { //If any of the conditions are not met, then inform client and change nothing
				temp = F("AT+CIPSEND=");
				temp += String(replyTo);
				temp += F(",5");
				ATCommand(temp, F(">"));
				ATCommand(F("error"), F("SEND OK"));
				return false;
			}
		} else { //Ignore HTTP request without required GET variable
			return false;
		}
	}
}

void processCommand() { //Process sent command and change values
	for(int i = 0; i < MAX_BITS; i++) {
		digitalWrite(BIT_OFFSET + i, bitValue[i]);
	}

	digitalWrite(state, device[address].status);

	invertPin(enable, 2); //Sends a pulse to clock of D flip-flop at specified address

	Serial.print(F("\n#Start##########\n\n\'")); //Print to Serial for debugging
	Serial.print(device[address].status);
	Serial.print(F("\' set to address \'"));
	Serial.print(address);
	Serial.println(F("\'\n\n############End#"));
}

String getKeyInput(int charLimit = 0) { //Gets input from keyboard and returns output as a String
	String input = "", displayText;
	input.reserve(32);
	displayText.reserve(32);

	while(true) {
		lcd.setCursor(0, 1);
		if(keyboard.available()) {
			char c = keyboard.read();

			// check for special keys
			if (c == PS2_ENTER) {
				if(input.length() == 0) { //If input doesn't contain anything, then return \n as input
					input = F("\n");
				}

				Serial.print(F("lcd -> "));
				Serial.println(input);
				return input;
			} else if (c == PS2_ESC) {
				return "";
			} else if (c == PS2_BACKSPACE) {
				if(input.length() > 0) { //Delete the last character if length of input is greater than 0
					input = input.substring(0, input.length() - 1);

					if(input.length() >= LCD_LENGTH) { //Scrolling display to the right
						displayText = input.substring(input.length() - LCD_LENGTH);
					} else {
						displayText = input;
						displayText += F(" ");
					}

					lcd.print(displayText);
				}
			} else {
				if(charLimit == 0 || input.length() < charLimit) {
					input += c;

					if(input.length() > LCD_LENGTH) { //Scrolling display to the left
						displayText = input.substring(input.length() - LCD_LENGTH);
					} else {
						displayText = input;
					}

					lcd.print(displayText);
				}
			}
		}
	}
}

void terminal() { //Terminal interface to manage devices
	String temp;
	temp.reserve(32);

	do {
		lcd.clear();
		lcd.print(F("Enter command:"));
		temp = getKeyInput(32);

		if(temp.indexOf(F("add ")) == 0) { //Command for adding devices to known devices list. ex: 'add 1' makes the device at pin 1 available
			address = temp.substring(4).toInt();

			if(address > 0 && address <= MAX_ADDRESS) {
				if(!device[--address].available) {
					lcd.clear();
					lcd.print(F("Enter name of "));
					lcd.print(address + 1);

					char tempArray[MAX_NAME_LENGTH];
					getKeyInput(MAX_NAME_LENGTH).toCharArray(tempArray, MAX_NAME_LENGTH);

					updateDevice(device[address], F("available"), true);
					updateDevice(device[address], F("status"), false);
					updateDevice(device[address], F("name"), tempArray);

					lcd.clear();
					lcd.print(F("Added"));
					lcd.setCursor(0, 1);
					lcd.print(F("device "));
					lcd.print(address + 1);
					delay(LCD_WAIT);
				} else {
					lcd.clear();
					lcd.print(F("Device "));
					lcd.print(address + 1);
					lcd.print(F(" used."));
					lcd.setCursor(0, 1);
					lcd.print(F("Use rename "));
					lcd.print(address + 1);
					delay(LCD_WAIT);
				}
			}

			else {
				lcd.clear();
				lcd.print(F("Invalid ID"));
				lcd.setCursor(0, 1);
				lcd.print(F("Range: 1 - "));
				lcd.print(MAX_ADDRESS);
				delay(LCD_WAIT);
			}
		} else if(temp.indexOf(F("rename ")) == 0) { //Command for renaming known devices. ex: 'rename 1' edits the name of device at pin 1
			address = temp.substring(7).toInt();

			if(address > 0 && address <= MAX_ADDRESS) {
				if(device[--address].available) {
					lcd.clear();
					lcd.print(F("Enter name of "));
					lcd.print(address + 1);

					char tempArray[MAX_NAME_LENGTH];
					getKeyInput(MAX_NAME_LENGTH).toCharArray(tempArray, MAX_NAME_LENGTH);

					updateDevice(device[address], F("name"), tempArray);

					lcd.clear();
					lcd.print(F("Done"));
					delay(LCD_WAIT);
				} else {
					lcd.clear();
					lcd.print(F("Device "));
					lcd.print(address + 1);
					lcd.setCursor(0, 1);
					lcd.print(F("not available"));
					delay(LCD_WAIT);
				}
			} else {
				lcd.clear();
				lcd.print(F("Invalid ID"));
				lcd.setCursor(0, 1);
				lcd.print(F("Range: 1 - "));
				lcd.print(MAX_ADDRESS);
				delay(LCD_WAIT);
			}
		} else if(temp.indexOf(F("remove ")) == 0) { //Command for removing devices from known devices list. ex: 'remove 1' makes the device at pin 1 unavailable
			address = temp.substring(7).toInt();

			if(address > 0 && address <= MAX_ADDRESS) {
				if(device[--address].available) {
					updateDevice(device[address], F("available"), false);

					lcd.clear();
					lcd.print(F("Removed"));
					lcd.setCursor(0, 1);
					lcd.print(F("device "));
					lcd.print(address + 1);
					delay(LCD_WAIT);
				} else {
					lcd.clear();
					lcd.print(F("Device "));
					lcd.print(address + 1);
					lcd.setCursor(0, 1);
					lcd.print(F("not available"));
					delay(LCD_WAIT);
				}
			} else {
				lcd.clear();
				lcd.print(F("Invalid ID"));
				lcd.setCursor(0, 1);
				lcd.print(F("Range: 1 - "));
				lcd.print(MAX_ADDRESS);
				delay(LCD_WAIT);
			}
		} else if(temp.indexOf(F("set ")) == 0) {
			temp = temp.substring(4);

			if(temp.indexOf(F("mode ")) == 0) { //Command for setting Wifi mode. ex: 'set mode 1' makes the system as WiFi client
				temp = temp.substring(5);

				if(temp.equals(F("1"))) {
					while(!ATCommand(F("AT+CWMODE=1"), F("OK"))) {
						if(ATCommand(F("AT+CWMODE=1"), F("no change"))) {
							break;
						}
					}

					getWifiCredentials();
				} else if(temp.equals(F("2"))) {
					while(!ATCommand(F("AT+CWMODE=2"), F("OK"))) {
						if(ATCommand(F("AT+CWMODE=2"), F("no change"))) {
							break;
						}
					}

					getWifiCredentials();
				} else {
					lcd.clear();
					lcd.print(F("Error"));
				}

				delay(LCD_WAIT);
			}

			if(temp.indexOf(F("device ")) == 0) { //Command for switching on/off connected devices. ex: 'set device 10 on' switches on device at pin 10
				temp = temp.substring(7);

				if(temp.indexOf(F(" ")) != -1) {
					address = temp.substring(0, temp.indexOf(F(" "))).toInt();

					if(address > 0 && address <= MAX_ADDRESS) {
						if(device[--address].available) {
							temp = temp.substring(temp.indexOf(F(" ")) + 1);
							setAddressBits(address);

							if(temp.equals(F("on"))) {
								updateDevice(device[address], F("status"), true);
								processCommand();
								lcd.clear();
								lcd.print(F("Device "));
								lcd.print(address + 1);
								lcd.setCursor(0, 1);
								lcd.print(F("switched on"));
								delay(LCD_WAIT);
							} else if(temp.equals(F("off"))) {
								updateDevice(device[address], F("status"), false);
								processCommand();
								lcd.clear();
								lcd.print(F("Device "));
								lcd.print(address + 1);
								lcd.setCursor(0, 1);
								lcd.print(F("switched off"));
								delay(LCD_WAIT);
							} else {
								lcd.clear();
								lcd.print(F("Invalid state"));
								lcd.setCursor(0, 1);
								lcd.print(F("Valid: on / off"));
								lcd.print(MAX_ADDRESS);
								delay(LCD_WAIT);
							}
						} else {
							lcd.clear();
							lcd.print(F("Device "));
							lcd.print(address + 1);
							lcd.setCursor(0, 1);
							lcd.print(F("not available"));
							delay(LCD_WAIT);
						}
					} else {
						lcd.clear();
						lcd.print(F("Invalid ID"));
						lcd.setCursor(0, 1);
						lcd.print(F("Range: 1 - "));
						lcd.print(MAX_ADDRESS);
						delay(LCD_WAIT);
					}
				} else {
					lcd.clear();
					lcd.print(F("Error"));
					delay(LCD_WAIT);
				}
			}
		} else if(temp.equals(F("wipe memory"))) { //Fully wipes the memory and makes all devices unknown, after restart
			lcd.clear();
			lcd.print(F("Wipe mem? [Y/N]:"));
			temp = getKeyInput(1);
			temp.toLowerCase();

			if(temp.equals(F("y"))) {
				char emptyArray[MAX_NAME_LENGTH] = {'\0'};

				for(address = 0; address < MAX_ADDRESS; address++) {
					lcd.clear();
					updateDevice(device[address], F("available"), false);
					updateDevice(device[address], F("status"), false);
					updateDevice(device[address], F("name"), emptyArray);
					lcd.print(address + 1);
					delay(50);
				}

				softwareReset();
			}
		} else if(temp.equals(F("reboot"))) { //Restart the system
			softwareReset();
		}
	}while(temp.length() > 0);

	displayNetworkData();
}

void displayNetworkData() { //Displays SSID and IP address of the system
	String temp;
	temp.reserve(16);
	lcd.clear();
	while(!ATCommand(F("AT+CWMODE?"), F("CWMODE:"), temp));
	if(temp.indexOf(F("CWMODE:1")) != -1) { //If CWMODE is 1 (Station mode)
		while(!ATCommand(F("AT+CWJAP?"), F("CWJAP:"), temp, 2000)) {
			if(ATCommand(F("AT+CWJAP?"), F("ERROR"))) { //If Wifi could not connect, request user for re-entering credentials
				getWifiCredentials();
				lcd.clear();
			}
		}

		temp = temp.substring(temp.indexOf(F("+CWJAP:\"")) + 8);
		temp = temp.substring(0, temp.lastIndexOf(F("\"")));
		lcd.print(temp); //Print the SSID of wifi network the ESP8266 is connected to
	} else if(temp.indexOf(F("CWMODE:2")) != -1) { //If CWMODE is 2 (AP mode)
		while(!ATCommand(F("AT+CWSAP?"), F("CWSAP:"), temp));
		temp = temp.substring(temp.indexOf(F("+CWSAP:\"")) + 8);
		temp = temp.substring(0, temp.indexOf(F("\"")));
		lcd.print(temp); //Print the SSID of wifi AP created by ESP8266
	}

	while(!ATCommand(F("AT+CIFSR"), F("OK"), temp));
	temp = temp.substring(0, temp.indexOf(F("\n")) - 1);
	lcd.setCursor(0, 1);
	lcd.print(temp); //Print the IP address of ESP8266 on the network
}

void getWifiCredentials() { //Gets the Wifi settings and applies it
	String cmd;
	int mode = 0;
	cmd.reserve(32);
	while(!ATCommand(F("AT+CWMODE?"), F("CWMODE:"), cmd));
	if(cmd.indexOf(F("CWMODE:1")) != -1) { //If CWMODE is 1 (Station mode), use AT+CWJAP
		mode = 1;
		cmd = F("AT+CWJAP=\"");
	} else if(cmd.indexOf(F("CWMODE:2")) != -1) { //If CWMODE is (AP mode), use AT+CWSAP
		mode = 2;
		cmd = F("AT+CWSAP=\"");
	}

	String input;
	input.reserve(32);

	do { //Get WiFi SSID
		lcd.clear();
		lcd.print(F("Enter SSID:"));
		input = getKeyInput();

		if(mode == 1) { //Validate WiFi SSID if mode 1
			if(!ATCommand(F("AT+CWLAP"), input)) {
				lcd.clear();
				lcd.print("Error. Try again");
				input = "";
				delay(500);
			}
		}
	}while(input.equals(""));

	cmd += input;
	cmd += F("\",\"");

	do { //Get WiFi password
		lcd.clear();
		lcd.print(F("Enter password:"));
		input = getKeyInput();
		cmd += input;
		cmd += F("\"");

		if(mode == 2) {
			cmd += F(",1,3");
		}

		if(!ATCommand(cmd, F("OK"))) { //Validate credentials
			lcd.clear();
			lcd.print("Error. Try again");
			cmd = cmd.substring(0, cmd.indexOf(input) - 1);
			input = "";
			delay(500);
		}
	}while(input.equals(""));

	lcd.clear();

	if(mode == 1) { //If CWMODE is 1 (Station mode)
		lcd.print(F("Wifi Client"));
	} else if(mode == 2) { //If CWMODE is 2 (AP mode)
		lcd.print(F("Wifi Hotspot"));
	}

	lcd.setCursor(0, 1);
	lcd.print(F("details updated"));
	delay(LCD_WAIT);
}

void setup() {
	Serial.begin(9600);
	espSerial.begin(9600);
	keyboard.begin(DATA_PIN, IRQ_PIN);
	lcd.begin(LCD_LENGTH, LCD_HEIGHT);
	lcd.print(F("Home"));
	lcd.setCursor(0, 1);
	lcd.print(F("Automation"));

	for(int i = 0; i < MAX_BITS; i++) {
		pinMode(BIT_OFFSET + i, OUTPUT);
	}
	pinMode(state, OUTPUT);
	pinMode(enable, OUTPUT);
	pinMode(RS, OUTPUT);
	pinMode(E, OUTPUT);
	pinMode(D4, OUTPUT);
	pinMode(D5, OUTPUT);
	pinMode(D6, OUTPUT);
	pinMode(D7, OUTPUT);

	initialiseESP();
	initialiseSwitches();
}

void loop() {
	if(keyboard.available()) {
		terminal();
	}

	if(espSerial.available()) {
		if(parseCommand(espSerial.readString())) {
			processCommand();
		}
	}
}
