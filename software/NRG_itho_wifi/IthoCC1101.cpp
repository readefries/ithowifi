/*
   Author: Klusjesman, supersjimmie, modified and reworked by arjenhiemstra
*/
#define DEBUG 0

//printf("Leading text "BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(byte));
#define BYTE_TO_BINARY_PATTERN "%c,%c,%c,%c,%c,%c,%c,%c,"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0')

#include "IthoCC1101.h"
#include "IthoPacket.h"
#include <string>
#include <Arduino.h>
#include <SPI.h>

//#define CRC_FILTER

////original sync byte pattern
//#define STARTBYTE 6 //relevant data starts 6 bytes after the sync pattern bytes 170/171
//#define SYNC1 170
//#define SYNC0 171
//#define MDMCFG2 0x02 //16bit sync word / 16bit specific

//alternative sync byte pattern (filter much more non-itho messages out. Maybe too strict? Testing needed.
#define STARTBYTE 0 //relevant data starts 0 bytes after the sync pattern bytes 179/42/171/42
#define SYNC1 187 //byte11 = 179, byte13 = 171 with SYNC1 = 163, 179 and 171 differ only by 1 bit
#define SYNC0 42
#define MDMCFG2 0x03 //32bit sync word / 30bit specific

//alternative sync byte pattern
//#define STARTBYTE 2 //relevant data starts 2 bytes after the sync pattern bytes 179/42
//#define SYNC1 179
//#define SYNC0 42
//#define MDMCFG2 0x02 //16bit sync word / 16bit specific

//calibration
#define STEP0 0x10
#define CAL_TIMEOUT ( 1UL * 15 * 1000 )

enum cc_cal_state {
  CAL_IDLE,
  CAL_START,
  CAL_BEGIN,
  CAL_WAIT,
  CAL_CHOP,
  CAL_ABORT,
  CAL_STOP
};

cc_cal_state calState = CAL_IDLE;


// default constructor
IthoCC1101::IthoCC1101(uint8_t counter, uint8_t sendTries) : CC1101()
{
  this->outIthoPacket.counter = counter;
  this->sendTries = sendTries;

  this->outIthoPacket.deviceId[0] = 33;
  this->outIthoPacket.deviceId[1] = 66;
  this->outIthoPacket.deviceId[2] = 99;

  this->outIthoPacket.deviceType = 22;

  calEnabled = 0;
  calFinised = 0;
  timeoutCCcal = CAL_TIMEOUT;
  cc_freq[0] = 0x6A;
  cc_freq[1] = 0x65;
  cc_freq[2] = 0x21;
  f0 = 0;
  lastValid = 0;
  lastF = 0;

  bindAllowed = false;
  allowAll = true;



} //IthoCC1101

// default destructor
IthoCC1101::~IthoCC1101()
{
} //~IthoCC1101

void IthoCC1101::initSendMessage(uint8_t len)
{
  //finishTransfer();
  writeCommand(CC1101_SIDLE);
  delayMicroseconds(1);
  writeRegister(CC1101_IOCFG0 , 0x2E);
  delayMicroseconds(1);
  writeRegister(CC1101_IOCFG1 , 0x2E);
  delayMicroseconds(1);
  writeCommand(CC1101_SIDLE);
  writeCommand(CC1101_SPWD);
  delayMicroseconds(2);

  /*
    Configuration reverse engineered from remote print. The commands below are used by IthoDaalderop.
    Base frequency    868.299866MHz
    Channel       0
    Channel spacing   199.951172kHz
    Carrier frequency 868.299866MHz
    Xtal frequency    26.000000MHz
    Data rate     38.3835kBaud
    Manchester      disabled
    Modulation      2-FSK
    Deviation     50.781250kHz
    TX power      ?
    PA ramping      enabled
    Whitening     disabled
  */
  writeCommand(CC1101_SRES);
  delayMicroseconds(1);
  writeRegister(CC1101_IOCFG0 , 0x2E);    //High impedance (3-state)
  writeRegister(CC1101_FREQ2 , cc_freq[2]);   //00100001  878MHz-927.8MHz
  writeRegister(CC1101_FREQ1 , cc_freq[1]);   //01100101
  writeRegister(CC1101_FREQ0 , cc_freq[0]);   //01101010
  writeRegister(CC1101_MDMCFG4 , 0x5A); //difference compared to message1
  writeRegister(CC1101_MDMCFG3 , 0x83); //difference compared to message1
  writeRegister(CC1101_MDMCFG2 , 0x00); //00000000  2-FSK, no manchester encoding/decoding, no preamble/sync
  writeRegister(CC1101_MDMCFG1 , 0x22); //00100010
  writeRegister(CC1101_MDMCFG0 , 0xF8); //11111000
  writeRegister(CC1101_CHANNR , 0x00);    //00000000
  writeRegister(CC1101_DEVIATN , 0x50); //difference compared to message1
  writeRegister(CC1101_FREND0 , 0x17);    //00010111  use index 7 in PA table
  writeRegister(CC1101_MCSM0 , 0x18);   //00011000  PO timeout Approx. 146microseconds - 171microseconds, Auto calibrate When going from IDLE to RX or TX (or FSTXON)
  writeRegister(CC1101_FSCAL3 , 0xA9);    //10101001
  writeRegister(CC1101_FSCAL2 , 0x2A);    //00101010
  writeRegister(CC1101_FSCAL1 , 0x00);    //00000000
  writeRegister(CC1101_FSCAL0 , 0x11);    //00010001
  writeRegister(CC1101_FSTEST , 0x59);    //01011001  For test only. Do not write to this register.
  writeRegister(CC1101_TEST2 , 0x81);   //10000001  For test only. Do not write to this register.
  writeRegister(CC1101_TEST1 , 0x35);   //00110101  For test only. Do not write to this register.
  writeRegister(CC1101_TEST0 , 0x0B);   //00001011  For test only. Do not write to this register.
  writeRegister(CC1101_PKTCTRL0 , 0x12);  //00010010  Enable infinite length packets, CRC disabled, Turn data whitening off, Serial Synchronous mode
  writeRegister(CC1101_ADDR , 0x00);    //00000000
  writeRegister(CC1101_PKTLEN , 0xFF);    //11111111  //Not used, no hardware packet handling

  //0x6F,0x26,0x2E,0x8C,0x87,0xCD,0xC7,0xC0
  writeBurstRegister(CC1101_PATABLE | CC1101_WRITE_BURST, (uint8_t*)ithoPaTableSend, 8);

  //difference, message1 sends a STX here
  writeCommand(CC1101_SIDLE);
  writeCommand(CC1101_SIDLE);

  writeRegister(CC1101_MDMCFG4 , 0x5A); //difference compared to message1
  writeRegister(CC1101_MDMCFG3 , 0x83); //difference compared to message1
  writeRegister(CC1101_DEVIATN , 0x50); //difference compared to message1
  writeRegister(CC1101_IOCFG0 , 0x2D);    //GDO0_Z_EN_N. When this output is 0, GDO0 is configured as input (for serial TX data).
  writeRegister(CC1101_IOCFG1 , 0x0B);    //Serial Clock. Synchronous to the data in synchronous serial mode.

  writeCommand(CC1101_STX);
  writeCommand(CC1101_SIDLE);

  writeRegister(CC1101_MDMCFG4 , 0x5A); //difference compared to message1
  writeRegister(CC1101_MDMCFG3 , 0x83); //difference compared to message1
  writeRegister(CC1101_DEVIATN , 0x50); //difference compared to message1
  //writeRegister(CC1101_IOCFG0 ,0x2D);   //GDO0_Z_EN_N. When this output is 0, GDO0 is configured as input (for serial TX data).
  //writeRegister(CC1101_IOCFG1 ,0x0B);   //Serial Clock. Synchronous to the data in synchronous serial mode.

  //Itho is using serial mode for transmit. We want to use the TX FIFO with fixed packet length for simplicity.
  writeRegister(CC1101_IOCFG0 , 0x2E);
  writeRegister(CC1101_IOCFG1 , 0x2E);
  writeRegister(CC1101_PKTCTRL0 , 0x00);
  writeRegister(CC1101_PKTCTRL1 , 0x00);

  writeRegister(CC1101_PKTLEN , len);

}

void IthoCC1101::finishTransfer()
{
  writeCommand(CC1101_SIDLE);
  delayMicroseconds(1);

  writeRegister(CC1101_IOCFG0 , 0x2E);
  writeRegister(CC1101_IOCFG1 , 0x2E);

  writeCommand(CC1101_SIDLE);
  writeCommand(CC1101_SPWD);
}

void IthoCC1101::initReceive()
{
  /*
    Configuration reverse engineered from RFT print.

    Base frequency    868.299866MHz
    Channel       0
    Channel spacing   199.951172kHz
    Carrier frequency 868.299866MHz
    Xtal frequency    26.000000MHz
    Data rate     38.3835kBaud
    RX filter BW    325.000000kHz
    Manchester      disabled
    Modulation      2-FSK
    Deviation     50.781250kHz
    TX power      0x6F,0x26,0x2E,0x7F,0x8A,0x84,0xCA,0xC4
    PA ramping      enabled
    Whitening     disabled
  */
  writeCommand(CC1101_SRES);

  writeRegister(CC1101_TEST0 , 0x09);
  writeRegister(CC1101_FSCAL2 , 0x00);

  //0x6F,0x26,0x2E,0x7F,0x8A,0x84,0xCA,0xC4
  writeBurstRegister(CC1101_PATABLE | CC1101_WRITE_BURST, (uint8_t*)ithoPaTableReceive, 8);

  writeCommand(CC1101_SCAL);

  //wait for calibration to finish
  while ((readRegisterWithSyncProblem(CC1101_MARCSTATE, CC1101_STATUS_REGISTER)) != CC1101_MARCSTATE_IDLE) yield();

  writeRegister(CC1101_FSCAL2 , 0x00);
  writeRegister(CC1101_MCSM0 , 0x18);     //no auto calibrate
  writeRegister(CC1101_FREQ2 , cc_freq[2]);
  writeRegister(CC1101_FREQ1 , cc_freq[1]);
  writeRegister(CC1101_FREQ0 , cc_freq[0]);
  writeRegister(CC1101_IOCFG0 , 0x2E);      //High impedance (3-state)
  writeRegister(CC1101_IOCFG2 , 0x06);      //0x06 Assert when sync word has been sent / received, and de-asserts at the end of the packet.
  writeRegister(CC1101_FSCTRL1 , 0x0F); //change 06
  writeRegister(CC1101_FSCTRL0 , 0x00);
  writeRegister(CC1101_MDMCFG4 , 0x6A);
  writeRegister(CC1101_MDMCFG3 , 0x83);
  writeRegister(CC1101_MDMCFG2 , 0x10);   //Enable digital DC blocking filter before demodulator, 2-FSK, Disable Manchester encoding/decoding, No preamble/sync
  writeRegister(CC1101_MDMCFG1 , 0x22);   //Disable FEC
  writeRegister(CC1101_MDMCFG0 , 0xF8);
  writeRegister(CC1101_CHANNR , 0x00);
  writeRegister(CC1101_DEVIATN , 0x50);
  writeRegister(CC1101_FREND1 , 0x56);
  writeRegister(CC1101_FREND0 , 0x10);
  writeRegister(CC1101_MCSM0 , 0x18);     //no auto calibrate
  writeRegister(CC1101_FOCCFG , 0x16);
  writeRegister(CC1101_BSCFG , 0x6C);
  writeRegister(CC1101_AGCCTRL2 , 0x43);
  writeRegister(CC1101_AGCCTRL1 , 0x40);
  writeRegister(CC1101_AGCCTRL0 , 0x91);
  writeRegister(CC1101_FSCAL3 , 0xE9);
  writeRegister(CC1101_FSCAL2 , 0x21);
  writeRegister(CC1101_FSCAL1 , 0x00);
  writeRegister(CC1101_FSCAL0 , 0x1F);
  writeRegister(CC1101_FSTEST , 0x59);
  writeRegister(CC1101_TEST2 , 0x81);
  writeRegister(CC1101_TEST1 , 0x35);
  writeRegister(CC1101_TEST0 , 0x09);
  writeRegister(CC1101_PKTCTRL1 , 0x04);    //No address check, Append two bytes with status RSSI/LQI/CRC OK,
  writeRegister(CC1101_PKTCTRL0 , 0x32);    //Infinite packet length mode, CRC disabled for TX and RX, No data whitening, Asynchronous serial mode, Data in on GDO0 and data out on either of the GDOx pins
  writeRegister(CC1101_ADDR , 0x00);
  writeRegister(CC1101_PKTLEN , 0xFF);


  writeCommand(CC1101_SCAL);

  //wait for calibration to finish
  while ((readRegisterWithSyncProblem(CC1101_MARCSTATE, CC1101_STATUS_REGISTER)) != CC1101_MARCSTATE_IDLE) yield();

  writeRegister(CC1101_MCSM0 , 0x18);     //no auto calibrate

  writeCommand(CC1101_SIDLE);
  writeCommand(CC1101_SIDLE);

  writeRegister(CC1101_MDMCFG2 , 0x00);   //Enable digital DC blocking filter before demodulator, 2-FSK, Disable Manchester encoding/decoding, No preamble/sync
  writeRegister(CC1101_IOCFG0 , 0x0D);      //Serial Data Output. Used for asynchronous serial mode.

  writeCommand(CC1101_SRX);

  while ((readRegisterWithSyncProblem(CC1101_MARCSTATE, CC1101_STATUS_REGISTER)) != CC1101_MARCSTATE_RX) yield();

  initReceiveMessage();
}

void  IthoCC1101::initReceiveMessage()
{
  uint8_t marcState;

  writeCommand(CC1101_SIDLE); //idle

  //set datarate
  writeRegister(CC1101_MDMCFG4 , 0x6A); // set kBaud
  writeRegister(CC1101_MDMCFG3 , 0x83); // set kBaud
  writeRegister(CC1101_DEVIATN , 0x50);

  //set fifo mode with fixed packet length and sync bytes
  //writeRegister(CC1101_PKTLEN , 63);      //63 bytes message (sync at beginning of message is removed by CC1101)

  //set fifo mode with fixed packet length and sync bytes
  writeRegister(CC1101_PKTCTRL0 , 0x02);
  writeRegister(CC1101_SYNC1 , SYNC1);
  writeRegister(CC1101_SYNC0 , SYNC0);
  writeRegister(CC1101_MDMCFG2 , MDMCFG2);
  writeRegister(CC1101_PKTCTRL1 , 0x00);

  writeCommand(CC1101_SRX); //switch to RX state

  // Check that the RX state has been entered
  while (((marcState = readRegisterWithSyncProblem(CC1101_MARCSTATE, CC1101_STATUS_REGISTER)) & CC1101_BITS_MARCSTATE) != CC1101_MARCSTATE_RX)
  {
    if (marcState == CC1101_MARCSTATE_RXFIFO_OVERFLOW) // RX_OVERFLOW
      writeCommand(CC1101_SFRX); //flush RX buffer
  }
}

uint8_t IthoCC1101::receivePacket() {
  return readData(&inMessage, MAX_RAW);
}

bool IthoCC1101::checkForNewPacket() {
  bool result = false;
  if (parseMessageCommand()) {
    if (calEnabled && inIthoPacket.error == 0) {
      cc_cal_update( inIthoPacket.error, false );
    }
    initReceiveMessage();
    result = true;
  }
  if (calEnabled && inIthoPacket.error > 1) {
    //printf("packet error: %d\n", inIthoPacket.error);
  }

  return result;
}

bool IthoCC1101::parseMessageCommand() {

  messageDecode(&inMessage, &inIthoPacket);

  uint8_t dataPos = 0;
  inIthoPacket.error = 0;
  inIthoPacket.command = IthoUnknown;

  //first byte is the header of the message, this determines the structure of the rest of the message
  //The bits are used as follows <00TTAAPP>
  // 00 - Unused
  // TT - Message type
  // AA - Present DeviceID fields
  // PP - Present Params
  inIthoPacket.header  = inIthoPacket.dataDecoded[0];
  dataPos++;

  //packet type: RQ-Request, W-Write, I-Inform, RP-Response
  if ((inIthoPacket.dataDecoded[0] >> 4) > 3) {
    inIthoPacket.error = 1;
    return false;
  }
  inIthoPacket.type = inIthoPacket.dataDecoded[0] >> 4;

  inIthoPacket.deviceId0 = 0;
  inIthoPacket.deviceId1 = 0;
  inIthoPacket.deviceId2 = 0;

  //get DeviceID fields
  uint8_t idfield = (inIthoPacket.dataDecoded[0] >> 2) & 0x03;

  if (idfield == 0x00 || idfield == 0x02 || idfield == 0x03) {
    inIthoPacket.deviceId0 = inIthoPacket.dataDecoded[dataPos] << 16 | inIthoPacket.dataDecoded[dataPos + 1] << 8 | inIthoPacket.dataDecoded[dataPos + 2];
    dataPos += 3;
    if (idfield == 0x00 || idfield == 0x03) {
      inIthoPacket.deviceId1 = inIthoPacket.dataDecoded[dataPos] << 16 | inIthoPacket.dataDecoded[dataPos + 1] << 8 | inIthoPacket.dataDecoded[dataPos + 2];
      dataPos += 3;
    }
    if (idfield == 0x00 || idfield == 0x02) {
      inIthoPacket.deviceId2 = inIthoPacket.dataDecoded[dataPos] << 16 | inIthoPacket.dataDecoded[dataPos + 1] << 8 | inIthoPacket.dataDecoded[dataPos + 2];
      dataPos += 3;
    }
  }
  else {
    inIthoPacket.deviceId2 = inIthoPacket.dataDecoded[dataPos] << 16 | inIthoPacket.dataDecoded[dataPos + 1] << 8 | inIthoPacket.dataDecoded[dataPos + 2];
    dataPos += 3;
  }

  //determine param0 present
  if (inIthoPacket.dataDecoded[0] & 0x02) {
    inIthoPacket.param0 = inIthoPacket.dataDecoded[dataPos];

    dataPos++;
  }
  else {
    inIthoPacket.param0 = 0;
  }
  //determine param1 present
  if (inIthoPacket.dataDecoded[0] & 0x01) {
    inIthoPacket.param1 = inIthoPacket.dataDecoded[dataPos];
    dataPos++;
  }
  else {
    inIthoPacket.param1 = 0;
  }

  //Get the two bytes of the opcode
  inIthoPacket.opcode = inIthoPacket.dataDecoded[dataPos] << 8 | inIthoPacket.dataDecoded[dataPos + 1];
  dataPos += 2;

  //Payload length
  inIthoPacket.len = inIthoPacket.dataDecoded[dataPos];
  if (inIthoPacket.len > MAX_PAYLOAD) {
    inIthoPacket.error = 1;
    return false;
  }

  dataPos++;
  inIthoPacket.payloadPos = dataPos;


  //Now we have parsed all the variable fields and know the total lenth of the message
  //with that we can determine if the message CRC is correct
  uint8_t mLen = inIthoPacket.payloadPos + inIthoPacket.len;

  if (getCounter2(&inIthoPacket, mLen) != inIthoPacket.dataDecoded[mLen]) {
    inIthoPacket.error = 2;
#if defined (CRC_FILTER)
    inIthoPacket.command = IthoUnknown;
    return false;
#endif
  }

  //
  // old message parse code below
  //

  //deviceType of message type?
  inIthoPacket.deviceType  = inIthoPacket.dataDecoded[0];

  //deviceID
  inIthoPacket.deviceId[0] = inIthoPacket.dataDecoded[1];
  inIthoPacket.deviceId[1] = inIthoPacket.dataDecoded[2];
  inIthoPacket.deviceId[2] = inIthoPacket.dataDecoded[3];

  //counter1
  inIthoPacket.counter = inIthoPacket.dataDecoded[4];

  //handle command
  switch (inIthoPacket.opcode) {
    case IthoPacket::Type::BIND :
      handleBind();
      break;
    case IthoPacket::Type::LEVEL :
      handleLevel();
      break;
    case IthoPacket::Type::SETPOINT :
      break;
    case IthoPacket::Type::TIMER :
      handleTimer();
      break;
    case IthoPacket::Type::STATUS :
      handleStatus();
      break;
    case IthoPacket::Type::REMOTESTATUS :
      handleRemotestatus();
      break;
    case IthoPacket::Type::TEMPHUM :
      handleTemphum();
      break;
    case IthoPacket::Type::CO2 :
      handleCo2();
      break;
    case IthoPacket::Type::BATTERY :
      handleBattery();
      break;
  }


  return true;
}

bool IthoCC1101::checkIthoCommand(IthoPacket *itho, const uint8_t commandBytes[]) {
  uint8_t offset = 0;
  if (itho->deviceType == 28 || itho->deviceType == 24) offset = 2;
  for (int i = 4; i < 6; i++)
  {
    //if (i == 2 || i == 3) continue; //skip byte3 and byte4, rft-rv and co2-auto remote device seem to sometimes have a different number there
    if ( (itho->dataDecoded[i + 5 + offset] != commandBytes[i]) && (itho->dataDecodedChk[i + 5 + offset] != commandBytes[i]) ) {
      return false;
    }
  }
  return true;
}

void IthoCC1101::sendCommand(IthoCommand command)
{
  CC1101Packet outMessage;
  uint8_t maxTries = sendTries;
  uint8_t delaytime = 40;

  //update itho packet data
  outIthoPacket.command = command;
  outIthoPacket.counter += 1;

  //get message2 bytes
  switch (command)
  {
    case IthoJoin:
      createMessageJoin(&outIthoPacket, &outMessage);
      break;

    case IthoLeave:
      createMessageLeave(&outIthoPacket, &outMessage);
      //the leave command needs to be transmitted for 1 second according the manual
      maxTries = 30;
      delaytime = 4;
      break;

    default:
      createMessageCommand(&outIthoPacket, &outMessage);
      break;
  }

  //send messages
  for (int i = 0; i < maxTries; i++)
  {

    //message2
    initSendMessage(outMessage.length);
    sendData(&outMessage);

    finishTransfer();
    delay(delaytime);
  }
  initReceive();
}


void IthoCC1101::createMessageStart(IthoPacket *itho, CC1101Packet *packet)
{

  //fixed, set start structure in data buffer manually
  for (uint8_t i = 0; i < 7; i++) {
    packet->data[i] = 170;
  }
  packet->data[7] = 171;
  packet->data[8] = 254;
  packet->data[9] = 0;
  packet->data[10] = 179;
  packet->data[11] = 42;
  packet->data[12] = 171;
  packet->data[13] = 42;

  //[start of command specific data]

}

void IthoCC1101::createMessageCommand(IthoPacket *itho, CC1101Packet *packet)
{

  //set start message structure
  createMessageStart(itho, packet);

  //set deviceType? (or messageType?), not sure what this is
  itho->dataDecoded[0] = itho->deviceType;

  //set deviceID
  itho->dataDecoded[1] = itho->deviceId[0];
  itho->dataDecoded[2] = itho->deviceId[1];
  itho->dataDecoded[3] = itho->deviceId[2];

  //set counter1
  itho->dataDecoded[4] = itho->counter;

  //set command bytes on dataDecoded[5 - 10]
  uint8_t *commandBytes = getMessageCommandBytes(itho->command);
  for (uint8_t i = 0; i < 6; i++) {
    itho->dataDecoded[i + 5] = commandBytes[i];
  }

  //set counter2
  itho->dataDecoded[11] = getCounter2(itho, 11);

  itho->length = 12;

  packet->length = messageEncode(itho, packet);
  packet->length += 1;

  //set end byte
  packet->data[packet->length] = 172;
  packet->length += 1;

  //set end 'noise'
  for (uint8_t i = packet->length; i < packet->length + 7; i++) {
    packet->data[i] = 170;
  }
  packet->length += 7;

}

void IthoCC1101::createMessageJoin(IthoPacket *itho, CC1101Packet *packet)
{

  //set start message structure
  createMessageStart(itho, packet);

  //set deviceType? (or messageType?)
  itho->dataDecoded[0] = itho->deviceType;

  //set deviceID
  itho->dataDecoded[1] = itho->deviceId[0];
  itho->dataDecoded[2] = itho->deviceId[1];
  itho->dataDecoded[3] = itho->deviceId[2];

  //set counter1
  itho->dataDecoded[4] = itho->counter;

  //set command bytes on dataDecoded[5 - ?]
  uint8_t *commandBytes = getMessageCommandBytes(itho->command);
  for (uint8_t i = 0; i < 6; i++) {
    itho->dataDecoded[i + 5] = commandBytes[i];
  }

  //set deviceID
  itho->dataDecoded[11] = itho->deviceId[0];
  itho->dataDecoded[12] = itho->deviceId[1];
  itho->dataDecoded[13] = itho->deviceId[2];

  itho->dataDecoded[14] = 1;
  itho->dataDecoded[15] = 16;
  itho->dataDecoded[16] = 224;

  //set deviceID
  itho->dataDecoded[17] = itho->deviceId[0];
  itho->dataDecoded[18] = itho->deviceId[1];
  itho->dataDecoded[19] = itho->deviceId[2];

  //set counter2
  itho->dataDecoded[20] = getCounter2(itho, 20);

  itho->length = 21;

  packet->length = messageEncode(itho, packet);
  packet->length += 1;

  //set end byte
  packet->data[packet->length] = 202;
  packet->length += 1;

  //set end 'noise'
  for (uint8_t i = packet->length; i < packet->length + 7; i++) {
    packet->data[i] = 170;
  }
  packet->length += 7;

}

void IthoCC1101::createMessageLeave(IthoPacket *itho, CC1101Packet *packet)
{

  //set start message structure
  createMessageStart(itho, packet);

  //set deviceType? (or messageType?)
  itho->dataDecoded[0] = itho->deviceType;

  //set deviceID
  itho->dataDecoded[1] = itho->deviceId[0];
  itho->dataDecoded[2] = itho->deviceId[1];
  itho->dataDecoded[3] = itho->deviceId[2];

  //set counter1
  itho->dataDecoded[4] = itho->counter;

  //set command bytes on dataDecoded[5 - 10]
  uint8_t *commandBytes = getMessageCommandBytes(itho->command);
  for (uint8_t i = 0; i < 6; i++) {
    itho->dataDecoded[i + 5] = commandBytes[i];
  }

  //set deviceID
  itho->dataDecoded[11] = itho->deviceId[0];
  itho->dataDecoded[12] = itho->deviceId[1];
  itho->dataDecoded[13] = itho->deviceId[2];

  //set counter2
  itho->dataDecoded[14] = getCounter2(itho, 14);

  itho->length = 15;

  packet->length = messageEncode(itho, packet);
  packet->length += 1;

  //set end byte
  packet->data[packet->length] = 202;
  packet->length += 1;

  //set end 'noise'
  for (uint8_t i = packet->length; i < packet->length + 7; i++) {
    packet->data[i] = 170;
  }
  packet->length += 7;

}

uint8_t* IthoCC1101::getMessageCommandBytes(IthoCommand command)
{
  switch (command)
  {
    case IthoStandby:
      return (uint8_t*)&ithoMessageStandByCommandBytes[0];
    case IthoHigh:
      return (uint8_t*)&ithoMessageHighCommandBytes[0];
    case IthoFull:
      return (uint8_t*)&ithoMessageFullCommandBytes[0];
    case IthoMedium:
      return (uint8_t*)&ithoMessageMediumCommandBytes[0];
    case IthoLow:
      return (uint8_t*)&ithoMessageLowCommandBytes[0];
    case IthoTimer1:
      return (uint8_t*)&ithoMessageTimer1CommandBytes[0];
    case IthoTimer2:
      return (uint8_t*)&ithoMessageTimer2CommandBytes[0];
    case IthoTimer3:
      return (uint8_t*)&ithoMessageTimer3CommandBytes[0];
    case IthoJoin:
      return (uint8_t*)&ithoMessageJoinCommandBytes[0];
    case IthoLeave:
      return (uint8_t*)&ithoMessageLeaveCommandBytes[0];
    default:
      return (uint8_t*)&ithoMessageLowCommandBytes[0];
  }
}

/*
   Counter2 is the decimal sum of all bytes in decoded form from
   deviceType up to the last byte before counter2 subtracted
   from zero.
*/
uint8_t IthoCC1101::getCounter2(IthoPacket *itho, uint8_t len) {

  uint8_t val = 0;

  for (uint8_t i = 0; i < len; i++) {
    val += itho->dataDecoded[i];
  }

  return 0 - val;
}

uint8_t IthoCC1101::messageEncode(IthoPacket *itho, CC1101Packet *packet) {

  uint8_t out_bytecounter = 14;   //index of Outbuf, start at offset 14, first part of the message is set manually
  uint8_t out_bitcounter = 0;     //bit position of current outbuf byte
  uint8_t out_patterncounter = 0; //bit counter to add 1 0 bit pattern after every 8 bits
  uint8_t bitSelect = 4;          //bit position of the inData byte (4 - 7, 0 - 3)
  uint8_t out_shift = 7;          //bit shift inData bit in position of outbuf byte

  //we need to zero the out buffer first cause we are using bitshifts
  for (int i = out_bytecounter; i < sizeof(packet->data) / sizeof(packet->data[0]); i++) {
    packet->data[i] = 0;
  }

  for (uint8_t dataByte = 0; dataByte < itho->length; dataByte++) {
    for (uint8_t dataBit = 0; dataBit < 8; dataBit++) {                                     //process a full dataByte at a time resulting in 20 output bits (2.5 bytes) with the pattern 7x6x5x4x 10 3x2x1x0x 10 7x6x5x4x 10 3x2x1x0x 10 etc
      if (out_bitcounter == 8) {                                                            //check if new byte is needed
        out_bytecounter++;
        out_bitcounter = 0;
      }

      if (out_patterncounter == 8) {                                                        //check if we have to start with a 1 0 pattern
        out_patterncounter = 0;
        packet->data[out_bytecounter] = packet->data[out_bytecounter] | 1 << out_shift;
        out_shift--;
        out_bitcounter++;
        packet->data[out_bytecounter] = packet->data[out_bytecounter] | 0 << out_shift;
        if (out_shift == 0) out_shift = 8;
        out_shift--;
        out_bitcounter++;
      }

      if (out_bitcounter == 8) {                                                            //check if new byte is needed
        out_bytecounter++;
        out_bitcounter = 0;
      }

      //set the even bit
      uint8_t bit = (itho->dataDecoded[dataByte] & (1 << bitSelect)) >> bitSelect;          //select bit and shift to bit pos 0
      bitSelect++;
      if (bitSelect == 8) bitSelect = 0;

      packet->data[out_bytecounter] = packet->data[out_bytecounter] | bit << out_shift;     //shift bit in corect pos of current outbuf byte
      out_shift--;
      out_bitcounter++;
      out_patterncounter++;

      //set the odd bit (inverse of even bit)
      bit = ~bit & 0b00000001;
      packet->data[out_bytecounter] = packet->data[out_bytecounter] | bit << out_shift;
      if (out_shift == 0) out_shift = 8;
      out_shift--;
      out_bitcounter++;
      out_patterncounter++;

    }

  }
  if (out_bitcounter < 8) {                                                                   //add closing 1 0 pattern to fill last packet->data byte and ensure DC balance in the message
    for (uint8_t i = out_bitcounter; i < 8; i += 2) {
      packet->data[out_bytecounter] = packet->data[out_bytecounter] | 1 << out_shift;
      out_shift--;
      packet->data[out_bytecounter] = packet->data[out_bytecounter] | 0 << out_shift;
      if (out_shift == 0) out_shift = 8;
      out_shift--;
    }
  }

  return out_bytecounter;
}


void IthoCC1101::messageDecode(CC1101Packet *packet, IthoPacket *itho) {

  itho->length = 0;
  int lenInbuf = packet->length;

  lenInbuf -= STARTBYTE; //correct for sync byte pos

  while (lenInbuf >= 5) {
    lenInbuf -= 5;
    itho->length += 2;
  }
  if (lenInbuf >= 3) {
    itho->length++;
  }

  for (int i = 0; i < sizeof(itho->dataDecoded) / sizeof(itho->dataDecoded[0]); i++) {
    itho->dataDecoded[i] = 0;
  }
  for (int i = 0; i < sizeof(itho->dataDecodedChk) / sizeof(itho->dataDecodedChk[0]); i++) {
    itho->dataDecodedChk[i] = 0;
  }

  uint8_t out_i = 0;                                  //byte index
  uint8_t out_j = 4;                                  //bit index
  uint8_t out_i_chk = 0;                              //byte index
  uint8_t out_j_chk = 4;                              //bit index
  uint8_t in_bitcounter = 0;                          //process per 10 input bits

  for (int i = STARTBYTE; i < packet->length; i++) {

    for (int j = 7; j > -1; j--) {
      if (in_bitcounter == 0 || in_bitcounter == 2 || in_bitcounter == 4 || in_bitcounter == 6) { //select input bits for output
        uint8_t x = packet->data[i];   //select input byte
        x = x >> j;             //select input bit
        x = x & 0b00000001;
        x = x << out_j;         //set value for output bit
        itho->dataDecoded[out_i] = itho->dataDecoded[out_i] | x;
        out_j += 1;             //next output bit
        if (out_j > 7) out_j = 0;
        if (out_j == 4) out_i += 1;
      }
      if (in_bitcounter == 1 || in_bitcounter == 3 || in_bitcounter == 5 || in_bitcounter == 7) { //select input bits for check output
        uint8_t x = packet->data[i];   //select input byte
        x = x >> j;             //select input bit
        x = x & 0b00000001;
        x = x << out_j_chk;         //set value for output bit
        itho->dataDecodedChk[out_i_chk] = itho->dataDecodedChk[out_i_chk] | x;
        out_j_chk += 1;             //next output bit
        if (out_j_chk > 7) out_j_chk = 0;
        if (out_j_chk == 4) {
          itho->dataDecodedChk[out_i_chk] = ~itho->dataDecodedChk[out_i_chk]; //inverse bits
          out_i_chk += 1;
        }
      }
      in_bitcounter += 1;     //continue cyling in groups of 10 bits
      if (in_bitcounter > 9) in_bitcounter = 0;
    }
  }
}

uint8_t IthoCC1101::ReadRSSI()
{
  uint8_t rssi = 0;
  uint8_t value = 0;

  rssi = (readRegister(CC1101_RSSI, CC1101_STATUS_REGISTER));

  if (rssi >= 128)
  {
    value = 255 - rssi;
    value /= 2;
    value += 74;
  }
  else
  {
    value = rssi / 2;
    value += 74;
  }
  return (value);
}

bool IthoCC1101::checkID(const uint8_t *id)
{
  for (uint8_t i = 0; i < 3; i++)
    if (id[i] != inIthoPacket.deviceId[i])
      return false;
  return true;
}

String IthoCC1101::getLastIDstr(bool ashex) {
  String str;
  for (uint8_t i = 0; i < 3; i++) {
    if (ashex) str += String(inIthoPacket.deviceId[i], HEX);
    else str += String(inIthoPacket.deviceId[i]);
    if (i < 2) str += ",";
  }
  return str;
}

int * IthoCC1101::getLastID() {
  static int id[3];
  for (uint8_t i = 0; i < 3; i++) {
    id[i] = inIthoPacket.deviceId[i];
  }
  return id;
}

String IthoCC1101::getLastMessagestr(bool ashex) {
  String str = "Length=" + String(inMessage.length) + ".";
  for (uint8_t i = 0; i < inMessage.length; i++) {
    if (ashex) str += String(inMessage.data[i], HEX);
    else str += String(inMessage.data[i]);
    if (i < inMessage.length - 1) str += ":";
  }
  return str;
}

String IthoCC1101::LastMessageDecoded() {

  String str;


  if (inIthoPacket.length > 11) {

    str += String(MsgType[inIthoPacket.type]);

    if (inIthoPacket.param0 == 0)  {
      str += " ---";
    }
    else {
      str += " ";
      str += String(inIthoPacket.param0);
    }

    if (inIthoPacket.deviceId0 == 0)  {
      str += " --,--,--";
    }
    else {
      str += " ";
      char buf[10];
      snprintf(buf, sizeof(buf), "%02X,%02X,%02X", inIthoPacket.deviceId0 >> 16 & 0xFF, inIthoPacket.deviceId0 >> 8 & 0xFF, inIthoPacket.deviceId0 & 0xFF);
      str += String(buf);
    }
    if (inIthoPacket.deviceId1 == 0)  {
      str += " --,--,--";
    }
    else {
      str += " ";
      char buf[10];
      sprintf(buf, "%02X,%02X,%02X", inIthoPacket.deviceId1 >> 16 & 0xFF, inIthoPacket.deviceId1 >> 8 & 0xFF, inIthoPacket.deviceId1 & 0xFF);
      str += String(buf);
    }
    if (inIthoPacket.deviceId2 == 0)  {
      str += " --,--,--";
    }
    else {
      str += " ";
      char buf[10];
      sprintf(buf, "%02X,%02X,%02X", inIthoPacket.deviceId2 >> 16 & 0xFF, inIthoPacket.deviceId2 >> 8 & 0xFF, inIthoPacket.deviceId2 & 0xFF);
      str += String(buf);
    }


    str += " ";

    char buf[10];
    sprintf(buf, "%04X", inIthoPacket.opcode);
    str += String(buf);

    str += " ";
    str += String(inIthoPacket.len);
    str += ":";

    for (int i = inIthoPacket.payloadPos; i < inIthoPacket.payloadPos + inIthoPacket.len; i++) {
      sprintf(buf, "%02X", inIthoPacket.dataDecoded[i]);
      str += String(buf);
      if (i < inIthoPacket.payloadPos + inIthoPacket.len - 1) {
        str += ",";
      }
    }
    str += "\n";
    //    for (int i = 0; i < inIthoPacket.length; i++) {
    //      sprintf(buf, "%02X", inIthoPacket.dataDecoded[i]);
    //      str += String(buf);
    //      if (i < inIthoPacket.length - 1) str += ",";
    //    }

  }
  else {
    for (uint8_t i = 0; i < inIthoPacket.length; i++) {
      char buf[10];
      sprintf(buf, "%02X", inIthoPacket.dataDecoded[i]);
      str += String(buf);
      if (i < inIthoPacket.length - 1) str += ",";
    }

  }
  str += "\n";
  return str;

}

void IthoCC1101::setCCcalEnable( uint8_t enable ) {
  if ( calEnabled != enable ) {
    //printf("calEnabled (%d) != enable (%d)\n", calEnabled, enable);
    if ( enable ) { // Start calibration process
      // Grab the current frequency in case we abort
      uint8_t startFreq[1 + 3] = { CC1101_FREQ2, 0, 0, 0 };
      readBurstRegister( startFreq + 1, startFreq[0], sizeof(startFreq) - 1);

      f0 = ( (uint32_t)startFreq[1] << 16 )
           | ( (uint32_t)startFreq[2] <<  8 )
           | ( (uint32_t)startFreq[3] <<  0 );

      calEnabled = 1;
      calState = CAL_START;
      calibrationTask.attach(1, +[](IthoCC1101 * IthoCC1101Instance) {
        IthoCC1101Instance->cc_cal_task();
      }, this);
    }
    else {
      // Stop calibration process
      calState = CAL_STOP;

    }
  }
  else {

  }

}

void IthoCC1101::abortCCcal() {
  setCCcalEnable(0);
  setCCcal(f0);
  calibrationTask.detach();
}

void IthoCC1101::cc_cal_task() {
  //check cal timeout
  unsigned long now = millis();

  unsigned long interval = now - lastValid;
  //printf("cal_task timeout: %lu\n", timeoutCCcal - interval);

  if ( interval > timeoutCCcal ) {
    cc_cal_update( 0xFF, true );

  }

}

uint32_t IthoCC1101::cc_cal( uint8_t validMsg, bool timeout ) {
  // Store the search control values in 16 bits
  // to avoid excessive 32 bit arithmetic
  static int16_t x, y;
  static int16_t step;
  static int16_t low, high;

  static uint32_t f;

  switch ( calState ) {
    case CAL_IDLE:
      //Serial.println("CAL_IDLE");
      break;

    case CAL_STOP:
    case CAL_ABORT:
      //Serial.println("CAL_STOP");
      calState = CAL_IDLE;
      calEnabled = 0;
      calibrationTask.detach();
      break;

    case CAL_START:
      //Serial.println("CAL_START");
      step = -STEP0;  // Initial search is for low limit
      f = f0;
      calState = CAL_BEGIN;
      break;

    case CAL_BEGIN:  // Begin initial search for extreme limit in direction of step
      //Serial.println("CAL_BEGIN");
      y = 0;
      x = step;
      f = f0 + x;
      calState = CAL_WAIT;
      break;

    case CAL_WAIT:
      //Serial.println("CAL_WAIT");
      if ( validMsg == 0 ) {
        // Move the initial search window out a step
        x += step;
        y += step;
        f += step;
      } else if ( timeout ) {
        f -= step / 2;
        calState = CAL_CHOP;
      }
      break;

    case CAL_CHOP:
      //Serial.println("CAL_CHOP");
      // Update appropriate boundary
      if ( validMsg == 0 ) {
        y  = ( x + y ) / 2;
        f += ( x - y ) / 2;
      } else if ( timeout ) {
        x  = ( x + y ) / 2;
        f -= ( x - y ) / 2;
      }

      if ( abs(x - y) <= 1  ) {
        if ( step < 0 ) {
          // Low limit found
          low = y;
          f = f0;
          step = -step; // Search in opposite direction
          calState = CAL_BEGIN;
        } else {
          // High limit found
          high = y;
          f = f0 + ( low + high ) / 2;
          calState = CAL_STOP;
        }
      }
  }

  return f;
}


void IthoCC1101::cc_cal_update( uint8_t msgError, bool timeout ) {

  if (calEnabled == 0) {
    return;
  }

  unsigned long now = millis();

  uint8_t isValid = msgError;


  if ( isValid || timeout )
    lastValid = now;

  uint32_t F = cc_cal( isValid, timeout );

  if ( lastF != F ) {
    if (calState == CAL_STOP) {

      calFinised = 1;
    }

    setCCcal(F);
    lastF = F;

    lastValid = now;
  }

}

void IthoCC1101::setCCcal(uint32_t F) {
  //double freq = (F * 26) / (double)65536;

  uint8_t param[3];
  param[0] = (uint8_t)( ( F >> 16 ) & 0xFF ); //FREQ2
  param[1] = (uint8_t)( ( F >> 8 ) & 0xFF );  //FREQ1
  param[2] = (uint8_t)( ( F >> 0 ) & 0xFF );  //FREQ0


  cc_freq[0] = param[2];
  cc_freq[1] = param[1];
  cc_freq[2] = param[0];

  writeBurstRegister(CC1101_FREQ2, param, 3);


}
void IthoCC1101::resetCCcal() {
  //reset to default values
  setCCcal(2188650); //828.299Mhz
}



bool IthoCC1101::addRFDevice(uint8_t byte0, uint8_t byte1, uint8_t byte2) {
  uint32_t tempID = byte0 << 16 | byte1 << 8 | byte2;
  return addRFDevice(tempID);
}
bool IthoCC1101::addRFDevice(uint32_t ID) {
  if (!bindAllowed) return false;
  if (checkRFDevice(ID)) return false; //device already registered

  for (auto& item : ithoRF.device) {
    if (item.deviceId == 0) { //pick the first available slot
      item.deviceId = ID;
      ithoRF.count++;
      return true;
    }
  }
  return false;
}

bool IthoCC1101::removeRFDevice(uint8_t byte0, uint8_t byte1, uint8_t byte2) {
  uint32_t tempID = byte0 << 16 | byte1 << 8 | byte2;
  return removeRFDevice(tempID);
}

bool IthoCC1101::removeRFDevice(uint32_t ID) {
  if (!bindAllowed) return false;
  if (!checkRFDevice(ID)) return false; //device not registered

  for (auto& item : ithoRF.device) {
    if (item.deviceId == ID) {
      item.deviceId = 0;
      //      strcpy(item.name, "");
      item.lastCommand = IthoUnknown;
      item.co2 = 0xEFFF;
      item.temp = 0xEFFF;
      item.hum = 0xEFFF;
      item.dewpoint = 0xEFFF;
      item.battery = 0xEFFF;
      ithoRF.count--;
      return true;
    }
  }

  return false; //not found
}

bool IthoCC1101::checkRFDevice(uint8_t byte0, uint8_t byte1, uint8_t byte2) {
  uint32_t tempID = byte0 << 16 | byte1 << 8 | byte2;
  return checkRFDevice(tempID);
}

bool IthoCC1101::checkRFDevice(uint32_t ID) {
  for (auto& item : ithoRF.device) {
    if (item.deviceId == ID) {
      return true;
    }
  }
  return false;
}

void IthoCC1101::handleBind() {
  uint32_t tempID = 0;
  if (inIthoPacket.deviceId0 != 0) tempID = inIthoPacket.deviceId0;
  else if (inIthoPacket.deviceId2 != 0) tempID = inIthoPacket.deviceId2;
  else return;

  if ( checkIthoCommand(&inIthoPacket, ithoMessageLeaveCommandBytes) ) {
    inIthoPacket.command = IthoLeave;
    if (bindAllowed) {
      removeRFDevice(tempID);
    }
  }
  else if ( checkIthoCommand(&inIthoPacket, ithoMessageJoinCommandBytes) || checkIthoCommand(&inIthoPacket, ithoMessageJoin2CommandBytes) ) {
    inIthoPacket.command = IthoJoin;
    if (bindAllowed) {
      addRFDevice(tempID);
    }
  }
  else if ( checkIthoCommand(&inIthoPacket, ithoMessageRVJoinCommandBytes) ) {
    inIthoPacket.command = IthoJoin;
    if (bindAllowed) {
      addRFDevice(tempID);
    }
    //TODO: handle join handshake
  }

  for (auto& item : ithoRF.device) {
    if (item.deviceId == tempID) {
      item.lastCommand = inIthoPacket.command;
    }
  }
  
}

void IthoCC1101::handleLevel() {
  uint32_t tempID = 0;
  if (inIthoPacket.deviceId0 != 0) tempID = inIthoPacket.deviceId0;
  else if (inIthoPacket.deviceId2 != 0) tempID = inIthoPacket.deviceId2;
  else return;

  if (!allowAll) {
    if (!checkRFDevice( tempID )) return;
  }
  if ( checkIthoCommand(&inIthoPacket, ithoMessageHighCommandBytes) ) {
    inIthoPacket.command = IthoHigh;
  }
  else if ( checkIthoCommand(&inIthoPacket, ithoMessageMediumCommandBytes) ) {
    inIthoPacket.command = IthoMedium;
  }
  else if ( checkIthoCommand(&inIthoPacket, ithoMessageLowCommandBytes) ) {
    inIthoPacket.command = IthoLow;
  }
  else if ( checkIthoCommand(&inIthoPacket, ithoMessageStandByCommandBytes) ) {
    inIthoPacket.command = IthoStandby;
  }

  for (auto& item : ithoRF.device) {
    if (item.deviceId == tempID) {
      item.lastCommand = inIthoPacket.command;
    }
  }
  
}

void IthoCC1101::handleTimer() {
  uint32_t tempID = 0;
  if (inIthoPacket.deviceId0 != 0) tempID = inIthoPacket.deviceId0;
  else if (inIthoPacket.deviceId2 != 0) tempID = inIthoPacket.deviceId2;
  else return;

  if (!allowAll) {
    if (!checkRFDevice( tempID )) return;
  }
  if ( checkIthoCommand(&inIthoPacket, ithoMessageTimer1CommandBytes) ) {
    inIthoPacket.command = IthoTimer1;
  }
  else if ( checkIthoCommand(&inIthoPacket, ithoMessageTimer2CommandBytes) ) {
    inIthoPacket.command = IthoTimer2;
  }
  else if ( checkIthoCommand(&inIthoPacket, ithoMessageTimer3CommandBytes) ) {
    inIthoPacket.command = IthoTimer3;
  }

  for (auto& item : ithoRF.device) {
    if (item.deviceId == tempID) {
      item.lastCommand = inIthoPacket.command;
    }
  }

}
void IthoCC1101::handleStatus() {
  uint32_t tempID = 0;
  if (inIthoPacket.deviceId0 != 0) tempID = inIthoPacket.deviceId0;
  else if (inIthoPacket.deviceId2 != 0) tempID = inIthoPacket.deviceId2;
  else return;

  if (!checkRFDevice( tempID )) return;

  for (auto& item : ithoRF.device) {
    if (item.deviceId == tempID) {
      //store last command
    }
  }
}
void IthoCC1101::handleRemotestatus() {
  uint32_t tempID = 0;
  if (inIthoPacket.deviceId0 != 0) tempID = inIthoPacket.deviceId0;
  else if (inIthoPacket.deviceId2 != 0) tempID = inIthoPacket.deviceId2;
  else return;

  if (!checkRFDevice( tempID )) return;

  for (auto& item : ithoRF.device) {
    if (item.deviceId == tempID) {
      //store last command
    }
  }

}
void IthoCC1101::handleTemphum() {
  /*
     message length: 6
     message opcode: 0x12A0
     byte[0]    : unknown
     byte[1]    : humidity
     bytes[2:3] : temperature
     bytes[4:5] : dewpoint temperature

  */
  if(inIthoPacket.error > 0) return;
  uint32_t tempID = 0;
  if (inIthoPacket.deviceId0 != 0) tempID = inIthoPacket.deviceId0;
  else if (inIthoPacket.deviceId2 != 0) tempID = inIthoPacket.deviceId2;
  else return;

  if (!checkRFDevice( tempID )) return;
  int32_t tempHum  = inIthoPacket.dataDecoded[inIthoPacket.payloadPos + 1];
  int32_t tempTemp = inIthoPacket.dataDecoded[inIthoPacket.payloadPos + 2] << 8 | inIthoPacket.dataDecoded[inIthoPacket.payloadPos + 3];
  int32_t tempDewp = inIthoPacket.dataDecoded[inIthoPacket.payloadPos + 4] << 8 | inIthoPacket.dataDecoded[inIthoPacket.payloadPos + 5];

  for (auto& item : ithoRF.device) {
    if (item.deviceId == tempID) {
      item.temp = tempTemp;
      item.hum = tempHum;
      item.dewpoint = tempDewp;
    }
  }

}


void IthoCC1101::handleCo2() {
  /*
     message length: 3
     message opcode: 0x1298
     byte[0]    : unknown
     bytes[1:2] : CO2 level

  */
  if(inIthoPacket.error > 0) return;
  uint32_t tempID = 0;
  if (inIthoPacket.deviceId0 != 0) tempID = inIthoPacket.deviceId0;
  else if (inIthoPacket.deviceId2 != 0) tempID = inIthoPacket.deviceId2;
  else return;

  if (!checkRFDevice( tempID )) return;
  int32_t tempVal = inIthoPacket.dataDecoded[inIthoPacket.payloadPos + 1] << 8 | inIthoPacket.dataDecoded[inIthoPacket.payloadPos + 2];

  for (auto& item : ithoRF.device) {
    if (item.deviceId == tempID) {
      item.co2 = tempVal;
    }
  }

}
void IthoCC1101::handleBattery() {
  /*
     message length: 3
     message opcode: 0x1060
     byte[0]  : zone_id
     byte[1]  : battery level percentage (0xFF = no percentage present)
     byte[2]  : battery state (0x01 OK, 0x00 LOW)

  */
  if(inIthoPacket.error > 0) return;
  uint32_t tempID = 0;
  if (inIthoPacket.deviceId0 != 0) tempID = inIthoPacket.deviceId0;
  else if (inIthoPacket.deviceId2 != 0) tempID = inIthoPacket.deviceId2;
  else return;

  if (!checkRFDevice( tempID )) return;
  int32_t tempVal = 10;
  if (inIthoPacket.dataDecoded[inIthoPacket.payloadPos + 1] == 0xFF) {
    if (inIthoPacket.dataDecoded[inIthoPacket.payloadPos + 2] == 0x01) tempVal = 100;
  }
  else {
    tempVal = (int32_t)inIthoPacket.dataDecoded[inIthoPacket.payloadPos + 1] / 2;
  }
  for (auto& item : ithoRF.device) {
    if (item.deviceId == tempID) {
      item.battery = tempVal;
    }
  }
}
