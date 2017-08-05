#include "Player.h"
#include "SlaveProcess.h"

#include <atomic>
#include <map>
#include <thread>
#include <signal.h>

static const char* sQueryProperties[] =
{
  "file_name",
  "audio_samples",
  "audio_bitrate",
  "audio_codec",
  "time_pos",
};

enum { idle, playPending, playing, terminating, };
enum { none, MPlayer, Audiocast };
struct Player::Private
{
  Player* mpSelf;
  SlaveProcess mProcess;
  std::atomic<int> mProcessKind;
  std::thread* mpThread = nullptr;

  std::mutex mMutex;
  std::atomic<int> mState;
  bool mPosPending = false, mPaused = false;
  std::map<std::string, std::string> mProperties;
  int mUpdateIntervalMs = 500;

  static void ThreadFunc( Private* );
};

void
Player::Private::ThreadFunc( Private* p )
{
  while( true )
  {
    bool changed = false;
    if(p->mProcessKind == MPlayer)
    {
      if( !p->mProcess.WaitForOutputMs( p->mUpdateIntervalMs ) )
      {
        std::lock_guard<std::mutex> lock(p->mMutex);
        if(!p->mProcess.Running())
        {
          p->mProcessKind = none;
          p->mPosPending = false;
          p->mState = idle;
          p->mProperties.clear();
          changed = true;
        }
        else if( p->mState == playing && !p->mPosPending )
        {
          p->mProcess.Input() << "get_time_pos" << std::endl;
          p->mPosPending = true;
        }
      }
      else
      {
        std::string line;
        if( std::getline( p->mProcess.Output(), line ) )
        {
          static const std::string tag = "ANS_";
          size_t pos = line.find("=");
          if( line.find( tag ) == 0 && pos != std::string::npos )
          {
            std::string name = line.substr( tag.length(), pos - tag.length() ),
              value = line.substr( pos + 1 );
            if( !value.empty() && value.front() == '\'' && value.back() == '\'' )
              value = value.substr( 1, value.length() - 2 );
            for( auto& c : name )
              c = ::tolower(c);
            std::lock_guard<std::mutex> lock(p->mMutex);
            p->mProperties[name] = value;
            if( name == "time_position" )
            {
              p->mPosPending = false;
              if( p->mState == playPending )
                p->mState = playing;
              changed = true;
            }
          }
        }
      }
    }
    else if(p->mProcessKind == Audiocast)
    {
      static const int audiocastUpdateIntervalMs = 1000;
      if(!p->mProcess.WaitForOutputMs(audiocastUpdateIntervalMs))
      {
        std::lock_guard<std::mutex> lock(p->mMutex);
        if(!p->mProcess.Running())
        {
          p->mProcessKind = none;
          p->mPosPending = false;
          p->mState = idle;
          p->mProperties.clear();
          changed = true;
        }
        else if( p->mState == playing && !p->mPosPending )
        {
          p->mProcess.Input() << "get_statistics" << std::endl;
          p->mPosPending = true;
        }
      }
      else
      {
        p->mPosPending = false;
        if( p->mState == playPending )
          p->mState = playing;
        changed = true;

        std::string line;
        while(p->mProcess.WaitForOutputMs(0))
        {
          if(std::getline(p->mProcess.Output(), line))
          {
            std::lock_guard<std::mutex> lock(p->mMutex);
            size_t pos = line.find('=');
            if(pos < line.length())
            {
              std::string key = line.substr(0, pos);
              p->mProperties[key] = line.substr(pos + 1);
            }
          }
        }
      }
    }
    else if( p->mState == terminating )
      return;
    else
      p->mProcess.WaitForOutputMs(250);
    if( changed )
      p->mpSelf->Broadcast();
  }
}

Player*
Player::Instance()
{
  static Player sPlayer;
  return &sPlayer;
}

Player::Player()
: p( new Private )
{
  p->mpSelf = this;
  p->mProcessKind = none;
  p->mState = idle;
  p->mpThread = new std::thread( &Private::ThreadFunc, p );
}

Player::~Player()
{
  Stop();
  if( p->mpThread && p->mpThread->joinable() )
    p->mpThread->join();
  delete p->mpThread;
  delete p;
}

int
Player::UpdateIntervalMs() const
{
  std::lock_guard<std::mutex> lock(p->mMutex);
  return p->mUpdateIntervalMs;
}

void
Player::SetUpdateIntervalMs( int interval )
{
  std::lock_guard<std::mutex> lock(p->mMutex);
  p->mUpdateIntervalMs = interval;
}

void
Player::Play( const std::string& file )
{
  Stop();
  std::string audiocast_tag = "audiocast://";
  if(file.find(audiocast_tag) == 0)
  {
    std::vector<std::string> args =
    { "/usr/local/bin/audiocast_client", "--quiet", "--stdin-control" };
    std::istringstream iss(file);
    iss.ignore(audiocast_tag.length());
    std::string s;
    std::getline(iss, s, '/');
    args.push_back("--server=" + s);
    if(iss.peek() == '?')
      iss.ignore();
    while(std::getline(iss, s, '&'))
      args.push_back("--" + s);
    if( !p->mProcess.Exec(args) )
    {
      Wt::log("error") << "Could not run " << args[0] << ": " << ::strerror(errno);
      return;
    }
    std::lock_guard<std::mutex> lock(p->mMutex);
    p->mProcessKind = Audiocast;
    p->mState = playing;
  }
  else if(!file.empty())
  {
    std::vector<std::string> args =
    { "/usr/bin/mplayer", "-idle", "-slave", "-quiet", "-ao", "alsa" };
    if( !p->mProcess.Exec(args) )
    {
      Wt::log("error") << "Could not run " << args[0] << ": " << ::strerror(errno);
      return;
    }
    std::lock_guard<std::mutex> lock(p->mMutex);
    p->mProcessKind = MPlayer;
    p->mState = playPending;
    p->mProcess.Input() << "loadfile " << file << std::endl;
    for( const auto& s : sQueryProperties )
      p->mProcess.Input() << "get_" << s << std::endl;
  }
}

void
Player::Pause()
{
  switch(p->mProcessKind)
  {
  case MPlayer:
    p->mProcess.Input() << "pause" << std::endl;
    break;
  case none:
    break;
  default:
    p->mProcess.Raise(p->mPaused ? SIGCONT : SIGSTOP);
  }
  p->mPaused = !p->mPaused;
}

void
Player::Stop()
{
  if( p->mPaused )
    Pause(); // continue
  std::lock_guard<std::mutex> lock( p->mMutex );
  switch(p->mProcessKind)
  {
  case none:
    break;
  case MPlayer:
    p->mProcess.Input() << "quit" << std::endl;
    break;
  default:
    p->mProcess.Kill();
  }
}

bool
Player::IsPlaying() const
{
  std::lock_guard<std::mutex> lock(p->mMutex);
  return p->mState == playing;
}

bool
Player::IsIdle() const
{
  std::lock_guard<std::mutex> lock(p->mMutex);
  return p->mState == idle;
}

std::string
Player::StreamProperty( const std::string& name ) const
{
  std::lock_guard<std::mutex> lock(p->mMutex);
  auto i = p->mProperties.find( name );
  if( i == p->mProperties.end() )
    return "";
  return i->second;
}

