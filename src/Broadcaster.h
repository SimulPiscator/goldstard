#ifndef BROADCASTER_H
#define BROADCASTER_H

#include <string>
#include <vector>
#include <mutex>
#include <boost/function.hpp>

class Broadcaster
{
public:
  int AddListener( const boost::function< void()>& );
  int RemoveListener();
  int ListenerCount() const { return mListeners.size(); }
protected:
  void Broadcast();
private:
  std::vector<std::pair< std::string, boost::function<void()> >> mListeners;
  std::mutex mMutex;
};

#endif // BROADCASTER_H
