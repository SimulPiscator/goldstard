#ifndef HARDWARE_H
#define HARDWARE_H

#include <string>

namespace Key
{
  enum
  {
    None = 0,

    Power, Mute,
    SourceUnknown, SourceCD, SourceAUX, SourceNetwork, SourceTape,
    GainCD, GainAUX, GainNetwork,
    CDPlay, CDStop, CDPrev, CDNext, CDRepeat, CDRandom,
    VolumeL, VolumeR, Treble, Bass,
    Stream,

    Count
  };
} // namespace

class Hardware
{
public:
  struct State
  {
    bool Power = false, Mute = false;
    int Source = Key::SourceCD, RemoteKey = 0;
    float GainCD = 0, GainAUX = 0, GainNetwork = 0,
          VolumeL = -36, VolumeR = -36,
          Treble = 0, Bass = 0;
    std::string Stream;
    float AutoPowerOff = 4*24*3600;
  };
  static Hardware* Instance();

  void AddListener( const boost::function<void()>& );
  void RemoveListener();
  bool SetState( const State& );
  void GetState( State& );

private:
  Hardware();
  ~Hardware();

  struct Private;
  Private* p;
};

#endif // HARDWARE_H
