#include "Broadcaster.h"

#include <Wt/WApplication>
#include <Wt/WServer>

int
Broadcaster::AddListener( const boost::function< void()>& func )
{
  std::lock_guard<std::mutex> lock( mMutex );
  mListeners.push_back( std::make_pair( wApp->sessionId(), func ) );
  return mListeners.size();
}

int
Broadcaster::RemoveListener()
{
  std::lock_guard<std::mutex> lock( mMutex );
  std::remove_if(
    mListeners.begin(), mListeners.end(),
    [](decltype(mListeners.front())& c)
    { return c.first == wApp->sessionId(); }
  );
  return mListeners.size();
}

void
Broadcaster::Broadcast()
{
  std::lock_guard<std::mutex> lock( mMutex );
  for( auto c : mListeners )
    Wt::WServer::instance()->post(c.first, c.second);
}
