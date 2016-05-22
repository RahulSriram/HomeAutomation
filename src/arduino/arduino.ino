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
#define state BIT_OFFSET + MAX_BITS //Set the location of switch state pin
#define enable state + 1 //Set the location of clock signal pin
#define RS 14 //Set Register Select pin of LCD
#define E 15 //Set Enable pin of LCD
#define D4 16 //Set 5th Data pin of LCD
#define D5 17 //Set 6th Data pin of LCD
#define D6 18 //Set 7th Data pin of LCD
#define D7 19 //Set 8th Data pin of LCD
#define LCD_WAIT 2000 //Set the wait time for LCD to display messages
#define DEFAULT_TIMEOUT 1000 //Set default timeout of ESP serial line

LiquidCrystal lcd(RS, E, D4, D5, D6, D7);
SoftwareSerial espSerial(RX_PIN, TX_PIN);
PS2Keyboard keyboard;

int address; //Stores address sent by client
boolean bitValue[MAX_BITS]; //Stores the address sent by client in bits

struct switchClass {
  boolean available; //Stores whether a device has been initialised to the switch
  boolean status; //Stores the status of each device
  char name[16]; //Stores the name of the device
}device[MAX_ADDRESS];

void updateDevice(switchClass device, String data, boolean value) { //Update the value of a device which are of type boolean
  int offset = sizeof(int); //Update the device status to Flash memory
  for(int i = 0; i < address; i++) {
    offset += sizeof(switchClass);
  }

  if(data.equals(F("available"))) {
    device.available = value;
    EEPROM.update(offset, device.available);
  }

  else if(data.equals(F("status"))) {
    offset += sizeof(boolean);
    device.status = value;
    EEPROM.update(offset, device.status);
  }
}

void updateDevice(switchClass device, String data, char value[]) { //Update the value of a device which are of type char array
  int offset = sizeof(int); //Update the device status to Flash memory
  for(int i = 0; i < address; i++) {
    offset += sizeof(switchClass);
  }

  if(data.equals(F("name"))) {
    offset += sizeof(boolean) * 2;

    for(int i = 0; i < 16; i++, offset += sizeof(char)) {
      device.name[i] = value[i];
      EEPROM.update(offset, device.name[i]);
    }
  }
}

void setAddressBits(int temp) { //Converts decimal to binary and stores in address bits
  for(int i = MAX_BITS - 1; i >= 0; i--) {
    if(temp % 2) {
      bitValue[i] = true;
    }

    else {
      bitValue[i] = false;
    }

    temp /= 2;
  }
}

boolean ATCommand(String cmd, String ack, int TIMEOUT) { //Send command 'cmd' to esp8266 and wait for 'ack' string in reply for TIMEOUT milliseconds
  String reply;

  flushESPSerial(); //Flush any previous replies in buffer
  espSerial.setTimeout(TIMEOUT);
  espSerial.print(cmd); //Send command to esp8266 appending \n\r to the end (CR+LF)
  espSerial.print(F("\r\n"));

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

  espSerial.setTimeout(DEFAULT_TIMEOUT);

  if(reply.indexOf(ack) != -1) { //If proper reply is recieved, return true
    return true;
  }

  else {
    return false;
  }
}

boolean ATCommand(String cmd, String ack, int TIMEOUT, String& reply) { //Send command 'cmd' to esp8266 and wait for 'ack' string in reply for TIMEOUT milliseconds and send reply as reference
  flushESPSerial(); //Flush any previous replies in buffer
  espSerial.setTimeout(TIMEOUT);
  espSerial.print(cmd);  //Send command to esp8266 appending \n\r to the end (CR+LF)
  espSerial.print(F("\r\n"));

  while(espSerial.available()) {  //Read reply from esp8266
    reply = espSerial.readString();
  }

  Serial.print(F("\n#Start##########\ncmd->")); //Print to Serial for debugging
  Serial.print(cmd);
  Serial.print(F("\nack->"));
  Serial.print(ack);
  Serial.print(F("\nreply->"));
  Serial.print(reply);
  Serial.println(F("\n############End#"));

  espSerial.setTimeout(DEFAULT_TIMEOUT);

  if(reply.indexOf(ack) != -1) { //If proper reply is recieved, return true
    return true;
  }

  else {
    return false;
  }
}

void invertPin(int pin, int n) { //Inverts pin's state n times
  while(n--) {
    delay(50);
    digitalWrite(pin, !digitalRead(pin));
  }
}

void flushESPSerial() { //Flushes any previous replies from esp8266 in buffer
  while(espSerial.available()) {
    espSerial.read();
  }
}

void initialiseESP() { //Runs each command until proper reply is recieved from esp8266
  while(!ATCommand(F("AT+RST"), F("Ready"), 3000)) {
    Serial.println(F("Power Supply not sufficient for esp8266"));
    while(true);
  }
  while(!ATCommand(F("ATE0"), F("OK"), 100));
  while(!ATCommand(F("AT+GMR"), F("OK"), 100));
  while(!ATCommand(F("AT+CIPMUX=1"), F("OK"), 100));
  while(!ATCommand(F("AT+CIPSERVER=1,22"), F("OK"), 100)) {
    if(ATCommand(F("AT+CIPSERVER=1,22"), F("no change"), 100)) {
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
    for(address=0; address<MAX_ADDRESS; address++) {
      device[address].available = false;
      device[address].status = false;
      setAddressBits(address);
      processCommand();
    }

    isRunBefore = true; //Set the boolean variable to false for next run
    EEPROM.update(0, isRunBefore);
    invertPin(13, 1);
  }

  else {
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
  }

  else if(input.indexOf(F("Ready")) != -1) {
    Serial.println(F("!!!!!!!!ESP8266 Voltage Fluctuation Reset!!!!!!!!"));
    while(!ATCommand(F("AT+CIPMUX=1"), F("OK"), 100));
    while(!ATCommand(F("AT+CIPSERVER=1,22"), F("OK"), 100));
    return false;
  }

  else {  //if this is not a client connection message, then parse the command
    String cmd;
    String temp;
    temp.reserve(16);
    int commaLocation1 = input.indexOf(F("+IPD,")) + 4;  //Client's connection number lies between the first 2 commas (ex: +IPD,0,...)
    int commaLocation2 = input.indexOf(F(","), commaLocation1);
    int replyTo = input.substring(commaLocation1 + 1, commaLocation2).toInt(); //Get connection number of client to send reply to

    cmd = input.substring(input.indexOf(F(":")) + 1);  //Get the command sent by client

    if(cmd.indexOf(F("?")) != -1) { //If command was to get the current state of a switch
      int queryLocation = cmd.indexOf(F("?")); //Get location of end of query (ex: 128? is query device 128)
      address = cmd.substring(0, queryLocation).toInt();  //parsing out address value

      if((address > 0) && (address <= MAX_ADDRESS)) {  //Checking if address is a valid value
        if(device[--address].available) {
          temp = F("AT+CIPSEND=");
          temp += String(replyTo);
          if(device[address].status) {  //Replying to client about current switch status
            temp += F(",2");
            ATCommand(temp, F(">"), 100);
            ATCommand(F("on"), F("SEND OK"), 600);
          }

          else {
            temp += F(",3");
            ATCommand(temp, F(">"), 100);
            ATCommand(F("off"), F("SEND OK"), 900);
          }

          return false;
        }

        else {
          temp = F("AT+CIPSEND=");
          temp += String(replyTo);
          temp += F(",2");
          ATCommand(temp, F(">"), 100);
          ATCommand(F("na"), F("SEND OK"), 600);
          return false;
        }
      }

      else {
        temp = F("AT+CIPSEND=");
        temp += String(replyTo);
        temp += F(",5");
        ATCommand(temp, F(">"), 100);
        ATCommand(F("error"), F("SEND OK"), 1500);
        return false;
      }
    }

    else if(cmd.indexOf(F("=")) != -1) {  //If command was to set a switch's status
      int setLocation = cmd.indexOf(F("="));  //Get location of address and signal state value separator (ex: 128=1 is turn on device 128)
      address = cmd.substring(0, setLocation).toInt();  //parsing out address value

      if((address > 0) && (address <= MAX_ADDRESS)) {  //Checking if address is a valid value
        if(device[--address].available) {
          setAddressBits(address);

          if(cmd.substring(setLocation + 1, setLocation + 2).toInt()) {
            updateDevice(device[address], F("status"), true);
          }

          else {
            updateDevice(device[address], F("status"), false);
          }

          temp = F("AT+CIPSEND=");
          temp += String(replyTo);
          temp += F(",4");
          ATCommand(temp, F(">"), 100); //Replying to client that the switch will be turned on
          ATCommand(F("done"), F("SEND OK"), 1200);
          return true;
        }

        else {
          temp = F("AT+CIPSEND=");
          temp += String(replyTo);
          temp += F(",2");
          ATCommand(temp, F(">"), 100);
          ATCommand(F("na"), F("SEND OK"), 600);
          return false;
        }
      }

      else {
        temp = F("AT+CIPSEND=");
        temp += String(replyTo);
        temp += F(",5");
        ATCommand(temp, F(">"), 100);
        ATCommand(F("error"), F("SEND OK"), 1500);
        return false;
      }
    }

    else if(cmd.indexOf(F("exit")) != -1) {  //If client was using telnet, then it prints exit before leaving
      return false;
    }

    else { //If any of the conditions are not met, then inform client and change nothing
      temp = F("AT+CIPSEND=");
      temp += String(replyTo);
      temp += F(",5");
      ATCommand(temp, F(">"), 100);
      ATCommand(F("error"), F("SEND OK"), 1500);
      return false;
    }
  }
}

void processCommand() { //Process sent command and change values
  for(int i = 0; i < MAX_BITS; i++) {
    digitalWrite(BIT_OFFSET + i, bitValue[i]);
  }

  digitalWrite(state, device[address].status);

  invertPin(enable, 2);  //Sends a pulse to clock of D flip-flop at specified address

  Serial.print(F("\n#Start##########\n\n\'")); //Print to Serial for debugging
  Serial.print(device[address].status);
  Serial.print(F("\' set to address \'"));
  Serial.print(address);
  Serial.println(F("\'\n\n############End#"));
}

void displayNetworkData() { //Displays SSID and IP address of the system
  String temp;
  temp.reserve(16);
  lcd.clear();
  while(!ATCommand(F("AT+CWMODE?"), F("CWMODE:"), 100, temp)); 
  if(temp.indexOf(F("CWMODE:1")) != -1) {  //If CWMODE is 1 (Station mode)
    while(!ATCommand(F("AT+CWJAP?"), F("CWJAP:"), 100, temp));
    temp = temp.substring(temp.indexOf(F("+CWJAP:\"")) + 8);
    temp = temp.substring(0, temp.lastIndexOf(F("\"")));
    lcd.print(temp); //Print the SSID of wifi network the ESP8266 is connected to
  }
 
  else if(temp.indexOf(F("CWMODE:2")) != -1) {  //If CWMODE is 2 (AP mode)
    while(!ATCommand(F("AT+CWSAP?"), F("CWSAP:"), 100, temp));
    temp = temp.substring(temp.indexOf(F("+CWSAP:\"")) + 8);
    temp = temp.substring(0, temp.indexOf(F("\"")));
    lcd.print(temp); //Print the SSID of wifi AP created by ESP8266
  }

  while(!ATCommand(F("AT+CIFSR"), F("OK"), 100, temp));
  temp = temp.substring(0, temp.indexOf(F("\n")) - 1);
  lcd.setCursor(0, 1);
  lcd.print(temp); //Print the IP address of ESP8266 on the network
}

String getKeyInput(int charLimit = 0) { //Gets input from keyboard and returns output as a String
  String input = "";
  input.reserve(32);

  while(true) {
    lcd.setCursor(0, 1);
    if(keyboard.available()) {
      char c = keyboard.read();

      // check for special keys
      if (c == PS2_ENTER) {
        Serial.print(F("lcd -> "));
        Serial.println(input);
        return input;
      }

      else if (c == PS2_ESC) {
        return "";
      }

      else if (c == PS2_BACKSPACE) {
        input = input.substring(0, input.length() - 1);
        lcd.print(input);
        lcd.print(F(" "));
      }

      else {
        if(charLimit == 0 || input.length() < charLimit) {
          input += c;
          lcd.print(input);
        }
      }
    }
  }
}

void terminal() { //Terminal interface to manage devices
  String temp;
  temp.reserve(64);

  while(temp.length() > 0) {
    lcd.clear();
    lcd.print(F(">"));
    temp = getKeyInput();

    if(temp.indexOf(F("add ")) == 0) {
      address = temp.substring(4).toInt();

      if(address > 0 && address <= MAX_ADDRESS) {
        if(!device[--address].available) {
          lcd.clear();
          lcd.print(F("Enter name of "));
          lcd.print(address + 1);

          char tempArray[16];
          getKeyInput(16).toCharArray(tempArray, 16);

          updateDevice(device[address], F("available"), true);
          updateDevice(device[address], F("status"), false);
          updateDevice(device[address], F("name"), tempArray);

          lcd.clear();
          lcd.print(F("Added"));
          lcd.setCursor(0, 1);
          lcd.print(F("device "));
          lcd.print(address + 1);
          delay(LCD_WAIT);
        }

        else {
          lcd.clear();
          lcd.print(F("Device "));
          lcd.print(address + 1);
          lcd.print(F(" used."));
          lcd.setCursor(0, 1);
          lcd.print(F("Use edit instead"));
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
    }

    else if(temp.indexOf(F("edit ")) == 0) {
      address = temp.substring(5).toInt();

      if(address > 0 && address <= MAX_ADDRESS) {
        lcd.clear();
        lcd.print(F("Enter name of "));
        lcd.print(address--);

        char tempArray[16];
        getKeyInput(16).toCharArray(tempArray, 16);

        updateDevice(device[address], F("name"), tempArray);

        lcd.clear();
        lcd.print(F("Done"));
        delay(LCD_WAIT);
      }

      else {
        lcd.clear();
        lcd.print(F("Invalid ID"));
        lcd.setCursor(0, 1);
        lcd.print(F("Range: 1 - "));
        lcd.print(MAX_ADDRESS);
        delay(LCD_WAIT);
      }
    }

    else if(temp.indexOf(F("remove ")) == 0) {
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
        }

        else {
          lcd.clear();
          lcd.print(F("Device"));
          lcd.setCursor(0, 1);
          lcd.print(F("not found"));
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
    }

    else if(temp.indexOf(F("set ")) == 0) {
      temp = temp.substring(4);

      if(temp.indexOf(F("mode ")) == 0) {
        temp = temp.substring(5);

        if(temp.equals(F("1"))) {
          while(!ATCommand(F("AT+CWMODE=1"), F("OK"), 100));
          lcd.clear();
          lcd.print(F("Wifi Client"));
          lcd.setCursor(0, 1);
          lcd.print(F("mode enabled"));
        }

        else if(temp.equals(F("2"))) {
          while(!ATCommand(F("AT+CWMODE=2"), F("OK"), 100));
          lcd.clear();
          lcd.print(F("Wifi Hotspot"));
          lcd.setCursor(0, 1);
          lcd.print(F("mode enabled"));
        }

        else {
          lcd.clear();
          lcd.print(F("Error"));
        }

        delay(LCD_WAIT);
      }

      else if(temp.equals("wifi")) {
        int mode = 0;
        while(!ATCommand(F("AT+CWMODE?"), F("CWMODE:"), 100, temp));
        if(temp.indexOf(F("CWMODE:1")) != -1) {  //If CWMODE is 1 (Station mode)
          mode = 1;
          temp = F("AT+CWJAP=\"");
        }

        else if(temp.indexOf(F("CWMODE:2")) != -1) {  //If CWMODE is  (AP mode)
          mode = 2;
          temp = F("AT+CWSAP=\"");
        }

        lcd.clear();
        lcd.print(F("Enter SSID:"));
        String s1, s2 = "";
        s1.reserve(16);

        do { //Validate WiFi SSID presence
          s1 = getKeyInput();

          if(mode == 1) {
            ATCommand(F("AT+CWLAP"), F("OK"), 10000, s2);
          }
        }while(s1.equals("") || s2.indexOf(s1) == -1);

        temp += s1;
        temp += F("\",\"");
        lcd.clear();
        lcd.print(F("Enter password:"));

        do { //Validate WiFi password
          s1 = getKeyInput();

          if(mode == 1) {
            if(!ATCommand(temp + s1 + F("\""), F("OK"), 8000)) {
              s1 = "";
            }
          }

          else if(mode == 2) {
            if(!ATCommand(temp + s1 + F("\",1,3"), F("OK"), 8000)) {
              s1 = "";
            }
          }
        }while(s1.equals(""));

        if(mode == 1) {  //If CWMODE is 1 (Station mode)
          lcd.clear();
          lcd.print(F("Wifi Client"));
          lcd.setCursor(0, 1);
          lcd.print(F("details updated"));
        }

        else if(mode == 2) {  //If CWMODE is 2 (AP mode)
          lcd.clear();
          lcd.print(F("Wifi Hotspot"));
          lcd.setCursor(0, 1);
          lcd.print(F("details updated"));
        }

        delay(LCD_WAIT);
      }
    }

    else if(temp.equals(F("reset"))) {
      lcd.clear();
      lcd.print(F("Reset? [Y/N]:"));
      temp = getKeyInput(1);
      temp.toLowerCase();

      if(temp.equals(F("y"))) {
        char emptyArray[16] = {'\0'};

        for(address = 0; address < MAX_ADDRESS; address++) {
          updateDevice(device[address], F("available"), false);
          updateDevice(device[address], F("status"), false);
          updateDevice(device[address], F("name"), emptyArray);
        }
      }
    }
  }

  displayNetworkData();
}

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);
  keyboard.begin(DATA_PIN, IRQ_PIN);
  lcd.begin(16, 2);
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

  while(espSerial.available()) {
    if(parseCommand(espSerial.readString())) {
      processCommand();
    }
  }
}
