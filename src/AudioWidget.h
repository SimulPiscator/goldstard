#ifndef AUDIO_WIDGET_H
#define AUDIO_WIDGET_H

#include "wt.hpp"

class AudioWidget : public Wt::WContainerWidget
{
public:
  AudioWidget( Wt::WContainerWidget* parent );
  ~AudioWidget();
private:
  void OnAction_int( int );
  void OnAction_void();
  struct Private;
  Private* p;
};

#endif // AUDIO_WIDGET_H
