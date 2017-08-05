#include "ControlResource.h"
#include "Hardware.h"
#include "Player.h"
#include <Wt/Http/Response>

ControlResource::ControlResource(Wt::WObject *parent)
: Wt::WStreamResource(parent)
{
}

ControlResource::~ControlResource()
{
  beingDeleted();
}

void
ControlResource::handleRequest( const Wt::Http::Request& req, Wt::Http::Response& rsp )
{
  rsp.setMimeType( "text/plain" );
  bool control = req.path().find( "control" ) != std::string::npos;

  Hardware::State state;
  Hardware::Instance()->GetState(state);

  static const struct { const char* name; float Hardware::State::* value; }
  numbers[] =
  {
#define _(x) { #x, &Hardware::State::x },
    _(VolumeL) _(VolumeR) _(Treble) _(Bass) _(GainCD) _(GainAUX) _(GainNetwork) _(AutoPowerOff)
#undef _
  };

  if( control )
  {
    bool ok = true;
    const auto& params = req.getParameterMap();

    auto power = params.find( "Power" );
    if( power != params.end() )
    {
      bool newValue = ::atoi( power->second.back().c_str() );
      if( !(newValue && state.Power) && params.size() > 1 )
        ok = false;
      state.Power = newValue;
    }
    else if( !state.Power )
      ok = false;

    for( const auto& n : numbers )
    {
      auto param = params.find( n.name );
      if( param != params.end() )
        state.*n.value = ::atof( param->second.back().c_str() );
    }

    auto mute = params.find( "Mute" );
    if( mute != params.end() )
      state.Mute = ::atoi( mute->second.back().c_str() );

    auto source = params.find( "Source" );
    if( source != params.end() )
    {
      int id = Key::SourceTape;
      if( source->second.back() == "CD" )
        id = Key::SourceCD;
      else if( source->second.back() == "AUX" )
        id = Key::SourceAUX;
      else if( source->second.back() == "Network" )
        id = Key::SourceNetwork;
      state.Source = id;
    }

    bool streamChanged = false;
    auto stream = params.find( "Stream" );
    if( stream != params.end() && stream->second.back() != state.Stream )
    {
      state.Stream = stream->second.back();
      streamChanged = true;
    }
    ok = ok && Hardware::Instance()->SetState( state );
    if( ok && streamChanged )
    {
      Player::Instance()->Stop();
      if( !state.Stream.empty() )
        Player::Instance()->Play( state.Stream );
    }
    rsp.out() << ok << std::endl;
  }
  else
  {
    std::string s = "Tape";
    switch( state.Source )
    {
      case Key::SourceCD:
        s = "CD";
        break;
      case Key::SourceAUX:
        s = "AUX";
        break;
      case Key::SourceNetwork:
        s = "Network";
        break;
    }
    rsp.out() << "Power=" << state.Power << "\n";
    rsp.out() << "Mute=" << state.Mute << "\n";
    rsp.out() << "Source=" << s << "\n";
    for( const auto& n : numbers )
      rsp.out() << n.name << "=" << state.*n.value << "\n";
    rsp.out() << "Stream=" << state.Stream << "\n";
  }
  rsp.out() << std::endl;
}
