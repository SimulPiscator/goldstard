#ifndef TDA7318_H
#define TDA7318_H

struct TDA7318
{
  enum
  {
    Address = 0x44,

    Volume = 0,
      VolumeGainBits = 6,
      VolumeGainRange = 80,

    SpkLFront = 0x80,
    SpkRFront = 0xa0,
    SpkLRear = 0xc0,
    SpkRRear = 0xe0,
      SpkGainBits = 5,
      SpkGainRange = 40,

    Input = 0x40,
      InputGainBits = 2,
      InputGainShift = 3,
      InputGainRange = 25,
      InputIndexMask = 0x3,

    Bass = 0x60,
    Treble = 0x70,
      BassTreble_Minus = 0,
      BassTreble_Plus = 8,
      BassTrebleGainBits = 3,
      BassTrebleGainRange = 14,
  };
};

#endif // TDA7318_H
