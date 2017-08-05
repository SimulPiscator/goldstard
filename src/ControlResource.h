#ifndef CONTROL_RESOURCE_H
#define CONTROL_RESOURCE_H

#include <Wt/WStreamResource>

class ControlResource : public Wt::WStreamResource
{
public:
  ControlResource(Wt::WObject *parent = 0);
  ~ControlResource();
  void handleRequest( const Wt::Http::Request&, Wt::Http::Response& );
};

#endif // CONTROL_RESOURCE_H
