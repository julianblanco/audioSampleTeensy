//Class for fast sampling with MCP3008 and Arduino Uno
//Created by Dmitriy L. (arithmechanics.com), 2017

#ifndef __FAST_MCP3008_H__
#define __FAST_MCP3008_H__

#include <SPI.h>

/*
MCP3008   Atmega328P
V_DD      5v
V_REF     5v
AGND      GND
CLK       D13
D_OUT     D12
D_IN      D11
CS        D10
DGND      GND
*/

class FastMCP3008 {
public:
  void init(const bool bOverclock = true) {
    SPI.begin();
    SPI.setBitOrder(MSBFIRST);
    SPI.setDataMode(SPI_MODE0);
    SPI.setClockDivider(bOverclock ? SPI_CLOCK_DIV4 : SPI_CLOCK_DIV8); //4MHz -- slightly too fast for MCP3008, but oh well
    pinMode(m_CS_PIN, OUTPUT);
    digitalWrite(m_CS_PIN, HIGH);
  }

  void end() { SPI.end(); }

  inline byte getChannelMask(const int channel) { 
    if ((channel > 7) || (channel < 0)) { return B11000000; }
    return (channel << 3) | B11000000; 
  }

  inline int read(const byte channelMask) {

    static union { uint16_t val; struct { uint8_t lsb; uint8_t msb; }; } data;
    static uint8_t lastBit;

    PORTB &= ~B00000100; //equivalent of digitalWrite(m_CS_PIN, LOW);

    data.msb = SPI.transfer(channelMask);
    data.lsb = SPI.transfer(0);

    //equivalent of lastBit = digitalRead(12) == HIGH; 
    asm volatile("nop");
    lastBit = (PINB & B00010000 ? 1 : 0);
    
    PORTB |= B00000100; //equivalent of digitalWrite(m_CS_PIN, HIGH);

    return ((data.val << 1) + lastBit) & 1023;
  }

private:
  const byte m_CS_PIN { 10 }; //if changing this pin, you would have to change pin mask in the function above
};


#endif