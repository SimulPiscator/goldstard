#ifndef PLAYER_H
#define PLAYER_H

#include "Broadcaster.h"
#include <string>

class Player : public Broadcaster
{
public:
  static Player* Instance();

  int UpdateIntervalMs() const;
  void SetUpdateIntervalMs( int );

  void Play( const std::string& );
  void Pause();
  void Stop();

  bool IsPlaying() const;
  bool IsIdle() const;
  std::string StreamProperty( const std::string& ) const;

private:
  Player();
  ~Player();

  struct Private;
  Private* p;
};

#endif // PLAYER_H
