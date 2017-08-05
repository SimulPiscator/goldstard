#include "AudioWidget.h"
#include "Hardware.h"
#include "Player.h"

#include <Wt/WPushButton>
#include <Wt/WCheckBox>
#include <Wt/WSlider>
#include <Wt/WButtonGroup>
#include <Wt/WRadioButton>
#include <Wt/WComboBox>
#include <Wt/WTemplate>
#include <Wt/WLabel>
#include <Wt/WTimer>

#include <fstream>
#include <string>
#include <sstream>
#include <iomanip>
#include <map>

template<class T> struct Control
{
  int id;
  const char* name, *label;
};

namespace Key
{
  enum
  {
    delta = Key::Count,
    VolumeL_label = VolumeL + delta,
    VolumeR_label = VolumeR + delta,
    Treble_label = Treble + delta,
    Bass_label = Bass + delta,
    GainCD_label = GainCD + delta,
    GainAUX_label = GainAUX + delta,
    GainNetwork_label = GainNetwork + delta,
    Stream_label = Stream + delta,
    CoupleLR,
    NetworkPlay, NetworkStop,
    NumControlKeys,
  };
}

static const Control<Wt::WCheckBox> sCheckBoxes[] =
{
  { Key::Power, "power-check", "Power" },
  { Key::Mute, "mute-check", "Mute" },
  { Key::CoupleLR, "couple-lr-check", "Couple LR" },
  { 0 }
};

static const Control<Wt::WRadioButton> sRadioButtons[] =
{
  { Key::SourceCD, "source-cd-button", "CD" },
  { Key::SourceAUX, "source-aux-button", "AUX" },
  { Key::SourceNetwork, "source-network-button", "Network" },
  { 0 }
};

static const Control<Wt::WPushButton> sPushButtons[] =
{
  { Key::CDPlay, "cd-play-button", "Play/Pause" },
  { Key::CDStop, "cd-stop-button", "Stop" },
  { Key::CDPrev, "cd-prev-button", "Prev" },
  { Key::CDNext, "cd-next-button", "Next" },
  { Key::CDRepeat, "cd-repeat-button", "Repeat" },
  { Key::CDRandom, "cd-random-button", "Random" },
  { Key::NetworkPlay, "network-play-button", "Play" },
  { Key::NetworkStop, "network-stop-button", "Stop" },
  { 0 }
};

static const Control<Wt::WComboBox> sDropDowns[] =
{
  { Key::Stream, "stream-dropdown", "", },
  { 0 }
};

template<> struct Control<Wt::WSlider>
{
  int id;
  const char* name;
  int min, max;
  float Hardware::State::* value;
};
static const Control<Wt::WSlider> sSliders[] =
{
  { Key::VolumeL, "volume-l-slider", -48, 0, &Hardware::State::VolumeL },
  { Key::VolumeR, "volume-r-slider", -48, 0, &Hardware::State::VolumeR },
  { Key::Treble, "treble-slider", -14, 14, &Hardware::State::Treble },
  { Key::Bass, "bass-slider", -14, 14, &Hardware::State::Bass },
  { 0 }
};

template<> struct Control<Wt::WLabel>
{
  int id;
  const char* name, *label;
  float Hardware::State::* value;
};
static const Control<Wt::WLabel> sLabels[] =
{
  { Key::GainCD_label, "gain-cd-label", "?", &Hardware::State::GainCD },
  { Key::GainAUX_label, "gain-aux-label", "?", &Hardware::State::GainAUX },
  { Key::GainNetwork_label, "gain-network-label", "?", &Hardware::State::GainNetwork },
  { Key::VolumeL_label, "volume-l-label", "?", &Hardware::State::VolumeL },
  { Key::VolumeR_label, "volume-r-label", "?", &Hardware::State::VolumeR },
  { Key::Treble_label, "treble-label", "?", &Hardware::State::Treble },
  { Key::Bass_label, "bass-label", "?", &Hardware::State::Bass },
  { Key::Stream_label, "stream-label", "", nullptr },
  { 0 }
};

struct AudioWidget::Private
{
  AudioWidget* mpSelf;
  Wt::WTemplate* mpTemplate;
  Wt::WButtonGroup* mpSourceGroup;
  std::map<int, Wt::WWidget*> mWidgets;
  bool mCoupleLR;
  std::vector<std::string> mStreams;
  Hardware::State mState;

  Private( AudioWidget* );
  ~Private();
  template<class T> void Create( const Control<T>* );
  template<class T> void Configure( T*, const Control<T>* );
  template<class T> T* Widget( int id ) { return dynamic_cast<T*>( mWidgets[id] ); }
  static int Id( Wt::WObject* );
  void SetStateFromControls();
  void SetControlsFromState();
  void OnHardwareChanged();
  void OnPlayerChanged();
  void OnAction( Wt::WObject*, int );
};

AudioWidget::Private::Private( AudioWidget* pSelf )
{
  mpSelf = pSelf;

  std::ifstream f( wApp->appRoot() + "src/AudioWidget.xhtml" );
  std::string s;
  std::getline( f, s, '\0' );
  f.close();
  mpTemplate = new Wt::WTemplate( s, pSelf );
  Create(sCheckBoxes);
  Create(sPushButtons);
  Create(sDropDowns);
  Create(sRadioButtons);
  Create(sSliders);
  Create(sLabels);
  mpSourceGroup = new Wt::WButtonGroup(mpSelf);
  for( int i = Key::SourceCD; i <= Key::SourceNetwork; ++i )
    mpSourceGroup->addButton( Widget<Wt::WRadioButton>(i) );

  Hardware::Instance()->AddListener( boost::bind(&Private::OnHardwareChanged, this) );
  Player::Instance()->AddListener( boost::bind(&Private::OnPlayerChanged, this) );
  Hardware::Instance()->GetState(mState);
  mCoupleLR = (mState.VolumeL == mState.VolumeR);

  f.open(wApp->appRoot() + "etc/network_streams.conf");
  while(std::getline(f, s))
  {
    size_t pos = s.find('=');
    if(pos < s.length())
    {
      Widget<Wt::WComboBox>(Key::Stream)->addItem(Wt::WString::fromUTF8(s.substr(0, pos)));
      mStreams.push_back(s.substr(pos + 1));
    }
  }
  f.close();

  // workaround: sliders must be enabled on load or won't work
  Wt::WTimer::singleShot(10, this, &AudioWidget::Private::OnHardwareChanged);
  if( !mState.Power )
  {
    mState.Power = true;
    SetControlsFromState();
    mState.Power = false;
  }
  else
    SetControlsFromState();
}

AudioWidget::Private::~Private()
{
  Hardware::Instance()->RemoveListener();
}

template<class T> void
AudioWidget::Private::Create( const Control<T>* p )
{
  while( p->id )
  {
    T* c = new T;
    Configure( c, p );
    std::ostringstream oss;
    oss << "ID_" << p->id;
    c->setId( oss.str().c_str() );
    mpTemplate->bindWidget( p->name, c );
    mWidgets[p->id] = c;
    ++p;
  }
}

template<class T> void
AudioWidget::Private::Configure( T* c, const Control<T>* p )
{
  c->setText(T::tr(p->label));
  c->clicked().connect( mpSelf, &AudioWidget::OnAction_void );
}

template<> void
AudioWidget::Private::Configure<Wt::WComboBox>( Wt::WComboBox* c, const Control<Wt::WComboBox>* p )
{
  c->setStyleClass("fill");
  c->changed().connect( mpSelf, &AudioWidget::OnAction_void );
}

template<> void
AudioWidget::Private::Configure<Wt::WSlider>( Wt::WSlider* c, const Control<Wt::WSlider>* p )
{
  c->setRange(p->min, p->max);
  c->setValue(p->min);
  c->setHeight(20);
  c->sliderMoved().connect( mpSelf, &AudioWidget::OnAction_int );
}

int
AudioWidget::Private::Id( Wt::WObject* c )
{
  if( c->id().find( "ID_" ) == 0 )
    return ::atoi( c->id().substr(3).c_str() );
  return Key::None;
}

void
AudioWidget::Private::OnHardwareChanged()
{
  Hardware::Instance()->GetState( mState );
  SetControlsFromState();
}

void
AudioWidget::Private::OnPlayerChanged()
{
  Player& player = *Player::Instance();
  std::string time, info;
  if( player.IsPlaying() )
  {
    std::ostringstream oss;

    std::istringstream iss;
    float f = 0;
    int i = 0;
    std::string s;

    iss.str(player.StreamProperty("packets_lost"));
    iss.clear();
    if(iss >> i)
      oss << "&Delta;n=" << i << "&nbsp;";

    iss.str(player.StreamProperty("buffer_delay"));
    iss.clear();
    if(iss >> f)
      oss << "&Delta;t=" << floor(f*1e3+0.5) << "ms";

    std::string time_position = player.StreamProperty("time_position");
    if( !time_position.empty() )
    {
      int t = ::floor(::atof( time_position.c_str()) );
      int s = t % 60, m = (t / 60) % 60, h = t / 3600;
      oss << std::setfill('0')
          << std::setw(2) << h << ':'
          << std::setw(2) << m << ':'
          << std::setw(2) << s;
    }
    time = oss.str();

    oss.str( "" );
    oss.clear();

    iss.str( player.StreamProperty("audio_samples") );
    iss.clear();
    if( iss >> f >> s )
      oss << " Sampling rate: " << f*1e-3 << "kHz ";
    if( iss >> f >> s )
      oss << " Channels: " << f << " ";
    iss.clear();
    iss.str(player.StreamProperty("audio_bitrate"));
    if( iss >> f >> s && f > 0 )
      oss << " Bitrate: " << f << "kbps ";

    info = oss.str();
    Widget<Wt::WPushButton>(Key::NetworkPlay)->setEnabled(false);
    Widget<Wt::WPushButton>(Key::NetworkStop)->setEnabled(true);
  }
  else
  {
    Widget<Wt::WPushButton>(Key::NetworkPlay)->setEnabled(!mState.Stream.empty() && player.IsIdle());
    Widget<Wt::WPushButton>(Key::NetworkStop)->setEnabled(false);
  }
  Widget<Wt::WLabel>( Key::Stream_label )->setText( time );
  Widget<Wt::WLabel>( Key::Stream_label )->setToolTip( info );
  wApp->triggerUpdate();
}

void
AudioWidget::Private::SetControlsFromState()
{
  for( auto s = sLabels; s->id; ++s )
  {
    if( s->value )
    {
      float newValue = mState.*s->value;
      auto* pLabel = Widget<Wt::WLabel>( s->id );
      const char* plus = (newValue > 0) ? "+" : "";
      pLabel->setText( Wt::WString("{1}{2}dB").arg(plus).arg(newValue) );
    }
  }
  for( auto s = sSliders; s->id; ++s )
  {
    int newValue = ::floor( mState.*s->value + 0.5 );
    Wt::WSlider* pSlider = Widget<Wt::WSlider>( s->id );
    pSlider->setValue( newValue );
  }
  Wt::WCheckBox* p = Widget<Wt::WCheckBox>( Key::Power );
  p->setChecked( mState.Power );
  p->enable();
  for( int i = Key::Power + 1; i <= Key::NumControlKeys; ++i )
  {
    Wt::WWidget* p = Widget<Wt::WWidget>( i );
    if( p )
      p->setDisabled( !mState.Power );
  }
  Widget<Wt::WCheckBox>( Key::Mute )->setChecked( mState.Mute );
  int streamId = std::find(mStreams.begin(), mStreams.end(), mState.Stream) - mStreams.begin();
  auto pDropDown = Widget<Wt::WComboBox>(Key::Stream);
  if(streamId == mStreams.size())
  {
    mStreams.push_back(mState.Stream);
    pDropDown->addItem(mState.Stream);
  }
  pDropDown->setCurrentIndex(streamId);
  pDropDown->setToolTip(mState.Stream);
  if(mState.Power)
  {
    Widget<Wt::WPushButton>(Key::NetworkPlay)->setEnabled(!mState.Stream.empty() && Player::Instance()->IsIdle());
    Widget<Wt::WPushButton>(Key::NetworkStop)->setEnabled(Player::Instance()->IsPlaying());
  }
  Widget<Wt::WCheckBox>( Key::CoupleLR )->setChecked( mCoupleLR );
  mpSourceGroup->setSelectedButtonIndex( mState.Source - Key::SourceCD );
  wApp->triggerUpdate();
}

void
AudioWidget::Private::SetStateFromControls()
{
  for( auto s = sSliders; s->id; ++s )
  {
    Wt::WSlider* pSlider = Widget<Wt::WSlider>( s->id );
    mState.*s->value = pSlider->value();
  }
  Wt::WCheckBox* p = Widget<Wt::WCheckBox>( Key::Power );
  bool powerChecked = p->isChecked();
  if( mState.Power != powerChecked && !p->isDisabled() )
  {
    p->disable();
    wApp->triggerUpdate();
  }
  mState.Power = powerChecked;
  mState.Mute = Widget<Wt::WCheckBox>( Key::Mute )->isChecked();
  mState.Source = Key::SourceCD + mpSourceGroup->selectedButtonIndex();
  mCoupleLR = Widget<Wt::WCheckBox>( Key::CoupleLR )->isChecked();
  auto pDropDown = Widget<Wt::WComboBox>(Key::Stream);
  if(pDropDown->isEnabled() && pDropDown->currentIndex() < mStreams.size())
  {
    if(mState.Stream != mStreams[pDropDown->currentIndex()])
    {
      mState.Stream = mStreams[pDropDown->currentIndex()];
      pDropDown->setToolTip(mState.Stream);
      wApp->triggerUpdate();
    }
  }
}

void
AudioWidget::Private::OnAction( Wt::WObject* obj, int value )
{
  SetStateFromControls();
  int objectID = Id(obj);
  switch( objectID )
  {
    case Key::CoupleLR:
      if( mCoupleLR )
        mState.VolumeR = mState.VolumeL = (mState.VolumeL + mState.VolumeR)/2;
      break;
    case Key::GainCD:
      mState.GainCD = value;
      break;
    case Key::GainAUX:
      mState.GainAUX = value;
      break;
    case Key::GainNetwork:
      mState.GainNetwork = value;
      break;
    case Key::VolumeL:
      mState.VolumeL = value;
      if( mCoupleLR )
        mState.VolumeR = mState.VolumeL;
      mState.Mute = false;
      break;
    case Key::VolumeR:
      mState.VolumeR = value;
      if( mCoupleLR )
        mState.VolumeL = mState.VolumeR;
      mState.Mute = false;
      break;
    case Key::Treble:
      mState.Treble = value;
      break;
    case Key::Bass:
      mState.Bass = value;
      break;
    case Key::SourceNetwork:
    case Key::SourceAUX:
    case Key::SourceCD:
      // CD mode will switch off after some time of CD player inactivity
      mState.Source = objectID;
      break;
    case Key::CDPlay:
    case Key::CDStop:
    case Key::CDRandom:
    case Key::CDNext:
    case Key::CDPrev:
    case Key::CDRepeat:
      mState.RemoteKey = objectID;
      break;
    case Key::NetworkPlay:
      Widget<Wt::WPushButton>(Key::NetworkPlay)->setEnabled(false);
      Player::Instance()->Play(mState.Stream);
      break;
    case Key::Stream:
      mState.Stream = mStreams[Widget<Wt::WComboBox>(Key::Stream)->currentIndex()];
      /* fall through */
    case Key::NetworkStop:
      Player::Instance()->Stop();
      break;
    case Key::Power:
      if( !mState.Power )
        Player::Instance()->Stop();
      break;
  }
  Hardware::Instance()->SetState( mState );
  mState.RemoteKey = Key::None;
}

AudioWidget::AudioWidget( WContainerWidget* parent )
: WContainerWidget( parent ), p( new Private( this ) )
{
}

AudioWidget::~AudioWidget()
{
  delete p;
}

void
AudioWidget::OnAction_void()
{
  p->OnAction(sender(), 0);
}

void
AudioWidget::OnAction_int( int value )
{
  p->OnAction(sender(), value);
}

