#include<SoftwareSerial.h>

#define BIT_OFFSET 4 //Set where the address line starts
#define MAX_BITS 4 //Set the number of bits in address line
#define MAX_ADDRESS int(pow(2, MAX_BITS)) //Calculate the maximum address possible (2 ^ MAX_BITS)
#define state BIT_OFFSET + MAX_BITS //Set the location of clock generator pin
#define enable BIT_OFFSET + MAX_BITS + 1 //Set the location of switch toggle pin
#define DEFAULT_TIMEOUT 1000 //Set the default timeout to wait for ESP to reply

SoftwareSerial espSerial(3, 2); //RX, TX

int address; //Stores address sent by client
int addressBit[MAX_BITS]; //Stores the location of each bit of address line
boolean bitValue[MAX_BITS]; //Stores the address sent by client in bits
boolean switchState[MAX_ADDRESS]; //Stores the state of each address

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

void processCommand() { //Process sent command and change values
  for(int i = 0; i < MAX_BITS; i++) {
    digitalWrite(addressBit[i], bitValue[i]);
  }

  digitalWrite(state, switchState[address]);
  invertLED(enable, 2);  //Sends a pulse to clock of D flip-flop at specified address

  Serial.println("\n#Start##########\n\n\'" + String(switchState[address]) + "\' set to address \'" + String(address) + "\'\n\n############End#"); //Print to Serial for debugging
}

boolean ATCommand(String cmd, String ack, int TIMEOUT) { //Send command 'cmd' to esp8266 and wait for 'ack' string in response for 'TIMEOUT' milliseconds
  String response;

  flushESPSerial(); //Flush any previous responses in buffer
  espSerial.setTimeout(TIMEOUT);  //Set timeout to specified value
  espSerial.print(cmd + "\r\n");  //Send command to esp8266 appending \n\r to the end (CR+LF)

  while(espSerial.available()) {  //Read response from esp8266
    response = espSerial.readString();
  }

  espSerial.setTimeout(DEFAULT_TIMEOUT);  //Change value of timeout to default value again
  Serial.println("\n#Start##########\ncmd->" + cmd + "\nack->" + ack + "\nresponse->" + response + "\n############End#"); //Print to Serial for debugging

  if(response.indexOf(ack) != -1) { //If proper response is recieved, return true
    return true;
  }

  else {
    return false;
  }
}

void invertLED(int pin, int n) { //Inverts pin's state n times
  while(n--) {
    delay(50);
    digitalWrite(pin, !digitalRead(pin));
  }
}

void flushESPSerial() { //Flushes any previous responses from esp8266 in buffer
  while(espSerial.available()) {
    espSerial.read();
  }
}

void initialiseESP() {  //Runs each command until proper response is recieved from esp8266
  while(!ATCommand("AT+RST", "Ready", 2000)) {
    Serial.println("Power Supply not sufficient for esp8266");
    while(true);
  }
  while(!ATCommand("AT+GMR", "OK", 100));
  while(!ATCommand("AT+CWMODE?", "CWMODE:2", 100)) {  //If CWMODE is not 2 (AP mode), set it to 2
    while(!ATCommand("AT+CWMODE=2", "OK", 100));
  }
  while(!ATCommand("AT+CIPMUX=1", "OK", 100));
  while(!ATCommand("AT+CIPSERVER=1,22", "OK", 100));
}

void initialiseSwitches() {
  for(address=0; address<MAX_ADDRESS; address++) {
    switchState[address] = false;
    setAddressBits(address);
    processCommand();
  }
}

boolean parseCommand(String input) {
  Serial.println("****************\nInput-> " + input + "\n"); //Print to Serial for debugging

  if((input.indexOf("Link") != -1) || (input.indexOf("Unlink") != -1)) { //If client has opened or closed connection, do nothing
    return false;
  }

  else if(input.indexOf("Ready") != -1) {
    Serial.println("!!!!!!!!ESP8266 Voltage Fluctuation Reset!!!!!!!!");
    while(!ATCommand("AT+CIPMUX=1", "OK", 100));
    while(!ATCommand("AT+CIPSERVER=1,22", "OK", 100));
    return false;
  }

  else {  //if this is not a client connection message, then parse the command
    String cmd;
    int commaLocation1 = input.indexOf("+IPD,") + 4;  //Client's connection number lies between the first 2 commas (ex: +IPD,0,...)
    int commaLocation2 = input.indexOf(",", commaLocation1);
    int replyTo = input.substring(commaLocation1 + 1, commaLocation2).toInt(); //Get connection number of client to send reply to

    cmd = input.substring(input.indexOf(":") + 1);  //Get the command sent by client

    if(cmd.indexOf("?") != -1) { //If command was to get the current state of a switch
      int queryLocation = cmd.indexOf("?"); //Get location of end of query (ex: 128? is query device 128)
      address = cmd.substring(0, queryLocation).toInt();  //parsing out address value

      if((address > 0) && (address <= MAX_ADDRESS)) {  //Checking if address is a valid value
        if(switchState[--address]) {  //Replying to client about current switch status
          while(!ATCommand("AT+CIPSEND=" + String(replyTo) + ",2", ">", 100));
          while(!ATCommand("on", "SEND OK", 600));
        }

        else {
          while(!ATCommand("AT+CIPSEND=" + String(replyTo) + ",3", ">", 100));
          while(!ATCommand("off", "SEND OK", 900));
        }

        return false;
      }
    }

    else if(cmd.indexOf("=") != -1) {  //If command was to set a switch's status
      int setLocation = cmd.indexOf("=");  //Get location of address and signal state value separator (ex: 128=1 is turn on device 128)
      address = cmd.substring(0, setLocation).toInt();  //parsing out address value

      if((address > 0) && (address <= MAX_ADDRESS)) {  //Checking if address is a valid value
        setAddressBits(--address);

        if(cmd.substring(setLocation + 1, setLocation + 2).toInt()) {
          switchState[address] = true;
        }

        else {
          switchState[address] = false;
        }

        while(!ATCommand("AT+CIPSEND=" + String(replyTo) + ",4", ">", 100)); //Replying to client that the switch will be turned on
        while(!ATCommand("done", "SEND OK", 1200));
        return true;
      }
    }

    else if(cmd.indexOf("exit") != -1) {  //If client was using telnet, then it prints exit before leaving
      return false;
    }

    else { //If any of the conditions are not met, then inform client and change nothing
      while(!ATCommand("AT+CIPSEND=" + String(replyTo) + ",5", ">", 100));
      while(!ATCommand("error", "SEND OK", 1500));
      return false;
    }
  }
}

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);
  espSerial.setTimeout(DEFAULT_TIMEOUT);

  for(int i = 0; i < MAX_BITS; i++) {
    addressBit[i] = BIT_OFFSET + i;
    pinMode(addressBit[i], OUTPUT);
  }
  pinMode(state, OUTPUT);
  pinMode(enable, OUTPUT);

  initialiseESP();
  initialiseSwitches();
}

void loop() {
  while(espSerial.available()) {
    if(parseCommand(espSerial.readString())) {
      processCommand();
    }
  }
}
