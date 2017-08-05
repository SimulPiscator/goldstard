#include "wt.hpp"
#include "AudioWidget.h"
#include "Hardware.h"
#include "Player.h"
#include "PipedResource.h"
#include "ControlResource.h"
#include <Wt/WLocalizedStrings>
#include <Wt/WLoadingIndicator>
#include <Wt/WFileResource>
#include <Wt/WServer>
#include <iostream>
#include <csignal>
#include <grp.h>
#include <pwd.h>

using namespace Wt;

class Application : public WApplication
{
  struct LocalizedStrings;
  
public:
  Application(const WEnvironment& env) : WApplication(env)
  {
    useStyleSheet("/src/style.css");
    setLocalizedStrings(new LocalizedStrings);
    loadingIndicator()->setMessage("Wait...");
    root()->addWidget(new AudioWidget(root()));
    enableUpdates();
  }
  static WApplication* Create(const WEnvironment& env)
  {
    return new Application(env);
  }
  
private:
  struct LocalizedStrings : WLocalizedStrings
  {
    bool resolveKey( const std::string& key, std::string& result ) override
    {
      result = key;
      return true;
    }
  };
};

int main(int argc, char **argv)
{
  const char* user = nullptr, *config = nullptr;
  std::vector<char*> argv_;
  for( int i = 0; i < argc - 1; ++i )
    if( !::strcmp( "--user", argv[i] ) )
      user = argv[++i];
    else if( !::strcmp( "--config", argv[i] ) )
      config = argv[++i];
    else
      argv_.push_back( argv[i] );
  if( argc > 1 )
    argv_.push_back( argv[argc-1] );
  
  struct passwd* pUserinfo = nullptr;
  if( user )
  {
    pUserinfo = ::getpwnam( user );
    if( !pUserinfo )
    {
      std::cerr << "Unknown user: " << user << std::endl;
      return 1;
    }
  }
  std::string configpath = "/etc/wt/wthttpd";
  if( config )
    configpath = config;

  try
  {
    WServer server;
    server.setServerConfiguration( argv_.size(), argv_.data(), configpath );
    server.addEntryPoint( Wt::Application, &Application::Create );
    
    PipedResource src;
    src.setCommand( "/bin/tar "
      "-cz "
      "--exclude='.[^/]*' "
      "--exclude='*.o' "
      "--exclude='*.gch' "
      "-C '" + server.appRoot() + "' ."
    );
    src.setMimeType( "application/gzip" );
    src.suggestFileName( APPNAME "-src.tgz" );
    server.addResource( &src, "/src.tgz" );
    
    ControlResource control;
    server.addResource( &control, "/control" );
    server.addResource( &control, "/state" );
    
    Wt::WFileResource info( "text/html", server.appRoot() + "doc/info.html" );
    server.addResource( &info, "/info" );

    if (server.start())
    {
      if( pUserinfo )
      {
        if( ::setgid( pUserinfo->pw_gid ) < 0 )
        {
          Wt::log("error") << "setgid(" << pUserinfo->pw_gid << "): " << ::strerror(errno);
          return 1;
        }
        if( ::initgroups( user, pUserinfo->pw_gid ) < 0 )
        {
          Wt::log("error") << "initgroups(" << user << ", " << pUserinfo->pw_gid << "): " << ::strerror(errno);
          return 1;
        }
        if( ::setuid( pUserinfo->pw_uid ) < 0 )
        {
          Wt::log("error") << "setuid(" << pUserinfo->pw_uid << "): " << ::strerror(errno);
          return 1;
        }
      }

      Hardware::Instance();
      Player::Instance();
      int sig = WServer::waitForShutdown(argv[0]);
      std::cerr << "Shutdown (signal = " << sig << ")" << std::endl;
      server.stop();
      if (sig == SIGHUP)
        WServer::restart(argc, argv, environ);
    }
  }
  catch( WServer::Exception& e )
  {
    std::cerr << e.what() << std::endl;
    return 1;
  }
  catch( std::exception& e )
  {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
