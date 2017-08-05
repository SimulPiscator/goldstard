#include "PipedResource.h"
#include <Wt/Http/Response>
#include <cstdio>

static const int CHUNKSIZE = 64000;

PipedResource::PipedResource(Wt::WObject *parent)
: Wt::WStreamResource(parent),
  mMimeType( "plain/text" )
{
}

PipedResource::~PipedResource()
{
  beingDeleted();
}

void
PipedResource::setCommand( const std::string& s )
{
  mCommand = s;
}

void
PipedResource::setMimeType( const std::string& s )
{
  mMimeType = s;
}

void
PipedResource::handleRequest( const Wt::Http::Request& req, Wt::Http::Response& rsp )
{
  auto cont = req.continuation();
  if( !cont )
  {
    FILE* fp = ::popen( mCommand.c_str(), "r" );
    if( fp )
    {
      rsp.setMimeType( mMimeType );
      cont = rsp.createContinuation();
      cont->setData( fp );
    }
  }
  else
  {
    FILE* fp = boost::any_cast<FILE*>( cont->data() );
    if( fp )
    {
      char buf[CHUNKSIZE], *pos = buf, *end = buf + sizeof(buf);
      int count = 1;
      while( pos < end && count > 0 )
      {
        count = ::fread( pos, 1, end - pos, fp );
        if( count > 0 )
          pos += count;
      }
      if( pos > buf )
      {
        rsp.out().write( buf, pos - buf );
        cont = rsp.createContinuation();
        cont->setData( fp );
      }
      else
      {
        int err = ::pclose( fp );
        if( err )
          Wt::log("error") << mCommand << ": " << ::strerror(err==-1?errno:err);
      }
    }
  }
}
