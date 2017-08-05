#ifndef PIPED_RESOURCE_H
#define PIPED_RESOURCE_H

#include <Wt/WStreamResource>

class PipedResource : public Wt::WStreamResource
{
public:
  PipedResource(Wt::WObject *parent = 0);
  ~PipedResource();
  void setCommand( const std::string& );
  void setMimeType( const std::string& );
  void handleRequest( const Wt::Http::Request&, Wt::Http::Response& );
private:
  std::string mCommand, mMimeType;
};

#endif // PIPED_RESOURCE_H
