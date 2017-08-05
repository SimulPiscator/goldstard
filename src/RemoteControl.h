#ifndef REMOTE_CONTROL_H
#define REMOTE_CONTROL_H

class RemoteControl
{
public:
  RemoteControl();
  ~RemoteControl();
  bool SendOnce( int key );
  bool StartRepeating( int key );
  bool StopRepeating( int key );
  
private:
  struct Private;
  Private* p;
};

#endif // REMOTE_CONTROL_H