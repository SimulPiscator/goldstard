#include "RemoteControl.h"
#include "Hardware.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <string>

static int OpenUnixSocket( const char* path )
{
  sockaddr_un addr;
  ::memset( &addr, 0, sizeof(addr) );
  addr.sun_family = AF_UNIX;
  ::strncpy( addr.sun_path, path, sizeof(addr.sun_path)-1 );
  int sockfd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if( sockfd >= 0 )
  {
    if( ::connect( sockfd, (sockaddr*)&addr, sizeof(addr) ) < 0 )
    {
      ::close( sockfd );
      sockfd = -1;
    }
  }
  return sockfd;
}

static bool WriteLine( int fd, const std::string& s )
{
  std::string line = s + "\n";
  const char* p = line.data();
  int len = line.length();
  while( len > 0 )
  {
    int r = ::write( fd, p, len );
    if( r > 0 )
    {
      len -= r;
      p += r;
    }
    else
      return false;
  }
  return true;
}
  
static bool ReadLine( int fd, std::string& line )
{
  line.clear();
  bool done = false;
  while( !done )
  {
    char c;
    int r = ::read( fd, &c, 1 );
    if( r == 1 )
    {
      switch( c )
      {
        case '\n':
          done = true;
          break;
        default:
          line += c;
      }
    }
    else
      return false;
  }
  return true;
}


static const char* sLircSocket = "/var/run/lirc/lircd";
static const char* sRemoteName = "goldstard";

static const struct
{
  int key;
  const char* code;
} sRcCodes[] =
{
  { Key::Power, "power" },
  { Key::SourceCD, "cd" },
  { Key::SourceAUX, "aux" },
  { Key::SourceTape, "tape" },
  { Key::CDPlay, "play" },
  { Key::CDStop, "stop" },
  { Key::CDPrev, "prev" },
  { Key::CDNext, "next" },
  { Key::CDRepeat, "repeat" },
  { Key::CDRandom, "random" },
};

struct RemoteControl::Private
{
  int mLircFd = -1;
  bool Execute( const char*, int );
};


RemoteControl::RemoteControl()
: p( new Private )
{
  p->mLircFd = OpenUnixSocket( sLircSocket );
  if( p->mLircFd < 0 )
    Wt::log( "error" )
      << "Could not connect to " << sLircSocket
      << ": " << ::strerror( errno );
}

RemoteControl::~RemoteControl()
{
  ::close( p->mLircFd );
  delete p;
}

bool
RemoteControl::SendOnce( int key )
{
  return p->Execute( "SEND_ONCE", key );
}

bool
RemoteControl::StartRepeating( int key )
{
  return p->Execute( "SEND_START", key );
}

bool
RemoteControl::StopRepeating( int key )
{
  return p->Execute( "SEND_STOP", key );
}

bool
RemoteControl::Private::Execute( const char* inCmd, int inKey )
{
  const char* pCode = nullptr;
  for( const auto& c : sRcCodes )
    if( c.key == inKey )
      pCode = c.code;
  
  if( pCode )
  {
    std::string cmd = inCmd;
    cmd += " ";
    cmd += sRemoteName;
    cmd += " ";
    cmd += pCode;
    if( !WriteLine( mLircFd, cmd ) )
      return false;
    std::string response = "lircd: ", line;
    bool done = false;
    enum { unknown, success, error } result = unknown;
    while( !done && ReadLine( mLircFd, line ) )
    {
      if( line == "SUCCESS" )
        result = success;
      else if( line == "ERROR" )
        result = error;
      else if( line == "END" && result != unknown )
        done = true;
      response += line + "\\n";
    }
    if( result != success )
      Wt::log("error") << response;
    else
      Wt::log("info") << response;
    return result == success;
  }
  return false;
}


