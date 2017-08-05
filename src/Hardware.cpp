#include "Hardware.h"

#include "Broadcaster.h"

#include "RemoteControl.h"
#include "TDA7318.h"

#include <thread>
#include <condition_variable>
#include <cassert>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef I2C_SLAVE
# define I2C_SLAVE 0x0703
#endif

static const char* sI2cBus = "/dev/i2c-1";

static const int sTimerIntervalMs = 500;
static const int sStateUpdateIntervalSeconds = 2;

static bool SaveState( const Hardware::State& s, const std::string& path )
{
  std::ofstream f( path );
  union { const void* p; const char* c; }
    begin = { &s },
    end = { &s.Stream };
  f.write( begin.c, end.c - begin.c );
  f.write( s.Stream.c_str(), s.Stream.length() + 1 );
  return f;
}

static bool RestoreState( Hardware::State& s, const std::string& path )
{
  std::ifstream f( path );
  union { void* p; char* c; }
    begin = { &s },
    end = { &s.Stream };
  f.read( begin.c, end.c - begin.c );
  std::getline( f, s.Stream, '\0' );
  return f;
}

static int StateToTDA7318(const Hardware::State& s, char* buf, bool mutedTransition)
{
  unsigned int source = 0;
  float gain = 0;
  switch( s.Source )
  {
    case Key::SourceAUX:
      source = 0;
      gain = s.GainAUX;
      break;
    case Key::SourceNetwork:
      source = 1;
      gain = s.GainNetwork;
      break;
    case Key::SourceCD:
      source = 2;
      gain = s.GainCD;
      break;
    default:
      source = 3;
  }
  float volL = gain + s.VolumeL, volR = gain + s.VolumeR;
  volL = std::min<float>( TDA7318::InputGainRange, volL );
  volR = std::min<float>( TDA7318::InputGainRange, volR );

  const float preampStep =
    (1.0 * TDA7318::InputGainRange) / (1 << TDA7318::InputGainBits);
  gain = std::max( volL, volR );
  gain = std::max( 0.f, gain );
  gain = ::ceil( gain / preampStep );
  unsigned int preamp = gain;
  preamp = (1 << TDA7318::InputGainBits) - 1 - preamp;
  assert( preamp < (1 << TDA7318::InputGainBits) );
  gain *= preampStep;
  volL -= gain;
  volR -= gain;

  const float volumeStep =
    (1.0 * TDA7318::VolumeGainRange) / (1 << TDA7318::VolumeGainBits);
  gain = std::max( volL, volR );
  gain = std::min( 0.f, gain );
  gain = std::max<float>( -TDA7318::VolumeGainRange + volumeStep, gain );
  gain = ::floor( gain / volumeStep + 0.5 );
  unsigned int volume = -gain;
  assert( volume < (1 << TDA7318::VolumeGainBits) );
  gain *= volumeStep;
  volL -= gain;
  volR -= gain;

  const float speakerStep =
    (1.0 * TDA7318::SpkGainRange) / (1 << TDA7318::SpkGainBits);
  gain = ::fabs( volL - volR );
  gain = ::floor( gain / speakerStep + 0.5 );
  gain = std::min<float>( gain, (1 << TDA7318::SpkGainBits) - 1 );
  unsigned int spkL = gain, spkR = 0;
  assert( spkL < (1<< TDA7318::SpkGainBits) );
  if( volL > volR )
    std::swap( spkL, spkR );

  const float bassTrebleStep =
    (1.0 * TDA7318::BassTrebleGainRange) / (1 << TDA7318::BassTrebleGainBits);
  unsigned int bass = ::fabs( ::floor( s.Bass / bassTrebleStep + 0.5 ) );
  bass = (1 << TDA7318::BassTrebleGainBits) - 1 - bass;
  bass |= (s.Bass > 0) ? TDA7318::BassTreble_Plus : TDA7318::BassTreble_Minus;
  unsigned int treble = ::fabs( ::floor( s.Treble / bassTrebleStep + 0.5 ) );
  treble = (1 << TDA7318::BassTrebleGainBits) - 1 - treble;
  treble |= (s.Treble > 0) ? TDA7318::BassTreble_Plus : TDA7318::BassTreble_Minus;

  unsigned int spkMute = (1 << TDA7318::SpkGainBits) - 1;
  if( s.Mute )
    spkL = spkR = spkMute;

  char* p = buf;
  if(mutedTransition)
  {
    *p++ = TDA7318::SpkLFront | spkMute;
    *p++ = TDA7318::SpkRFront | spkMute;
  }
  *p++ = TDA7318::Volume | volume;
  *p++ = TDA7318::Input | (preamp << TDA7318::InputGainShift) | source;
  *p++ = TDA7318::Treble | treble;
  *p++ = TDA7318::Bass | bass;
  *p++ = TDA7318::SpkLFront | spkL;
  *p++ = TDA7318::SpkRFront | spkR;
  return p - buf;
}

struct Hardware::Private : Broadcaster
{
  struct : State
  { std::mutex mutex;
    time_t ts = 0, change_ts = 0;
    using State::operator=;
  } mCurrentState, mNextState;
  std::string mStatePath = "/var/local/" APPNAME "/state";

  bool mPowerTransition = false;
  RemoteControl mRemote;
  std::vector<std::pair<time_t, boost::function<void()>>> mSchedule;

  int mTDA7318;

  std::thread* mpThread;
  int mTimerInterval = -1;
  enum { None, SetState, Wakeup, Stop };
  struct
  {
    void Set( int code )
    {
      std::lock_guard<std::mutex> lock(mutex);
      what = code;
      cond.notify_one();
    }
    int Wait( int timeoutMs )
    {
      auto wakeIf = [this](){ return what != None; };
      std::unique_lock<std::mutex> lock(mutex);
      if( timeoutMs < 0 )
        cond.wait( lock, wakeIf );
      else
        cond.wait_for( lock, std::chrono::milliseconds(timeoutMs), wakeIf );
      int result = what;
      what = None;
      return result;
    }
    int what;
    std::mutex mutex;
    std::condition_variable cond;
  } mTrigger;

  Private()
  : mpThread( nullptr )
  {
    mTDA7318 = ::open( sI2cBus, O_RDWR );
    if( mTDA7318 >= 0 && ::ioctl( mTDA7318, I2C_SLAVE, TDA7318::Address ) < 0 )
    {
      ::close( mTDA7318 );
      mTDA7318 = -1;
    }
    if( mTDA7318 < 0 )
      Wt::log("error") << "Could not open " << sI2cBus << ": " << ::strerror(errno);
    if( RestoreState( mCurrentState, mStatePath ) )
      Wt::log("info") << "Restored state from " << mStatePath;
    else
      Wt::log("error") << "Could not restore state from " << mStatePath;
    mCurrentState.Power = IsPoweredOn();
  }

  ~Private()
  {
    StopThread();
    ::close(mTDA7318);
  }

  void StartThread()
  {
    if( !mpThread )
    {
      mTrigger.what = None;
      mpThread = new std::thread(&Private::ThreadFunc, this);
    }
  }

  void StopThread()
  {
    mTrigger.Set(Stop);
    mpThread->join();
    delete mpThread;
    mpThread = nullptr;
  }

  bool ApplyAudioConfig( int maxTries )
  {
    char buf[8];
    int len = StateToTDA7318(mNextState, buf, mNextState.Source != mCurrentState.Source);
    while( ::write(mTDA7318, buf, len) != len && --maxTries > 0 )
      std::this_thread::sleep_for( std::chrono::milliseconds(50) );
    if( maxTries <= 0 )
      Wt::log("error") << "i2c: " << ::strerror(errno);
    return maxTries > 0;
  }

  static void ThreadFunc( Private* p )
  {
    p->mTimerInterval = -1;
    while( true ) switch( p->mTrigger.Wait( p->mTimerInterval ) )
    {
      case None:
        p->OnTimer();
        break;
      case SetState:
        p->OnSetState();
        break;
      case Wakeup:
        Wt::log("info") << "Hardware: Waking up";
        p->mTimerInterval = sTimerIntervalMs;
        break;
      case Stop:
        return;
    }
  }

  void Schedule( time_t when, const boost::function<void()>& what )
  {
    mSchedule.push_back( std::make_pair( when, what ) );
  }

  void OnTimer()
  {
    bool changed = false;
    time_t now = ::time( nullptr );
    if( mPowerTransition || (now > mCurrentState.ts + sStateUpdateIntervalSeconds) )
    {
      std::lock_guard<std::mutex> lock( mCurrentState.mutex );
      bool poweredOn = IsPoweredOn();
      if( mCurrentState.Power != poweredOn )
      {
        if(mPowerTransition)
        {
          mRemote.StopRepeating(Key::Power);
          mPowerTransition = false;
          if(!poweredOn)
          {
            if( SaveState( mCurrentState, mStatePath ) )
              Wt::log("info") << "Saved state to " << mStatePath;
            else
              Wt::log("error") << "Could not save state to " << mStatePath;
          }
        }
        if(poweredOn)
        {
            ApplyAudioConfig( 10 );
            int key = Key::SourceAUX; // avoid built-in auto-off behavior
            if(mCurrentState.Source == Key::SourceCD)
              key = Key::SourceCD;
            Schedule( now + 3, boost::bind( &Private::ApplyAudioConfig, this, 10 ) );
            Schedule( now + 3, boost::bind( &RemoteControl::StartRepeating, &mRemote, key ) );
            Schedule( now + 5, boost::bind( &RemoteControl::StopRepeating, &mRemote, key ) );
            Schedule( now + 5, boost::bind( &Private::ApplyAudioConfig, this, 10 ) );
            Schedule( now + 5, boost::bind( &Private::Broadcast, this ) );
            changed = false;
        }
        else
        {
            changed = true;
        }
        mCurrentState.Power = poweredOn;
      }
      mCurrentState.ts = now;
      if(mCurrentState.change_ts)
      {
        if(mCurrentState.AutoPowerOff && now > mCurrentState.change_ts + mCurrentState.AutoPowerOff)
        {
          if(poweredOn && !mPowerTransition)
          {
            mCurrentState.Power = false;
            mPowerTransition = true;
            mRemote.StartRepeating(Key::Power);
            changed = true;
          }
        }
      }
      if( ListenerCount() == 0 && !poweredOn )
      {
        Wt::log("info") << "Hardware: Going to sleep";
        mTimerInterval = -1;
      }
    }
    if( changed )
    {
      Broadcast();
      mCurrentState.change_ts = now;
    }

    for( auto i = mSchedule.begin(); i != mSchedule.end(); )
    {
      if( now > i->first )
      {
        i->second();
        i = mSchedule.erase( i );
      }
      else
        ++i;
    }
  }

  void OnSetState()
  {
    bool changed = false;
    {
      std::lock_guard<std::mutex> lock1( mCurrentState.mutex );
      std::lock_guard<std::mutex> lock2( mNextState.mutex );

      if( mNextState.Power != mCurrentState.Power && !mPowerTransition )
      {
        mRemote.StartRepeating( Key::Power );
        mPowerTransition = true;
      }
      if(mNextState.Source != mCurrentState.Source)
      {
        if(mNextState.Source == Key::SourceCD)
          mNextState.RemoteKey = Key::SourceCD;
        else if(mCurrentState.Source == Key::SourceCD)
          mNextState.RemoteKey = Key::SourceAUX;
      }

      int key = mNextState.RemoteKey;
      mNextState.RemoteKey = Key::None;
      if( mCurrentState.Power && key != Key::None )
      {
        changed = true;
        mRemote.SendOnce( key );
        Schedule( ::time(nullptr) + 1, boost::bind( &Private::ApplyAudioConfig, this, 10 ) );
      }

      if( mCurrentState.Power && mNextState.Power )
      {
        if( ApplyAudioConfig( 10 ) )
        {
          changed = true;
          mCurrentState.State::operator=( mNextState );
        }
      }
    }
    if( changed )
    {
      Broadcast();
      mCurrentState.change_ts = ::time(nullptr);
    }
  }

  bool IsPoweredOn()
  {
    int fd = ::open("/var/local/" APPNAME "/powersensor", O_RDONLY | O_CLOEXEC);
    if(fd < 0)
      return false;
    char c = '0';
    if(::read(fd, &c, 1) != 1)
      return false;
    ::close(fd);
    return c == '1';
  }
};

Hardware*
Hardware::Instance()
{
  static Hardware sHardware;
  return &sHardware;
}

Hardware::Hardware()
: p( new Private )
{
  p->StartThread();
}

Hardware::~Hardware()
{
  p->StopThread();
  delete p;
}

void
Hardware::AddListener( const boost::function<void()>& func )
{
  if( p->AddListener( func ) == 1 )
    p->mTrigger.Set( Private::Wakeup );
}

void
Hardware::RemoveListener()
{
  p->RemoveListener();
}

bool
Hardware::SetState( const State& s )
{
  std::lock_guard<std::mutex> lock( p->mNextState.mutex );
  if( p->mPowerTransition )
    return false;
  p->mNextState = s;
  p->mTrigger.Set( Private::SetState );
  return true;
}

void
Hardware::GetState( State& s )
{
  std::lock_guard<std::mutex> lock( p->mCurrentState.mutex );
  s = p->mCurrentState;
}


