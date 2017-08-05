#include "SlaveProcess.h"
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ext/stdio_filebuf.h>

namespace {

struct Blocksignal
{
  Blocksignal(int signum)
  {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, signum);
    pthread_sigmask(SIG_BLOCK, &mask, &mOldmask);
  }
  ~Blocksignal()
  {
    pthread_sigmask(SIG_SETMASK, &mOldmask, nullptr);
  }
private:
  sigset_t mOldmask;
};
} // namespace

struct SlaveProcess::Private
{
  std::ostream mInput;
  std::istream mOutput;
  int mFdInput = -1, mFdOutput = -1, mFdOutputWrite = -1;
  mutable int mPid = -1;
  
  Private()
  : mInput( nullptr ), mOutput( nullptr )
  {
  }
  ~Private()
  {
    KillChild();
  }
  bool StartChild(const std::vector<std::string>& args)
  {
    enum
    {
      input_ = 0, output_ = 2,
      read_ = 0, write_ = 1,
    };
    int fds[4];
    for( int idx : { input_, output_ } )
      if( ::pipe( fds + idx ) < 0 )
        return false;
    int pid = ::fork();
    if( pid > 0 ) // parent
    {
      mPid = pid;
      ::close(fds[input_|read_]);
      mFdInput = fds[input_|write_];
      mFdOutputWrite = fds[output_|write_];
      mFdOutput = fds[output_|read_];

      mInput.rdbuf( new __gnu_cxx::stdio_filebuf<char>(mFdInput, std::ios::out) );
      mOutput.rdbuf( new __gnu_cxx::stdio_filebuf<char>(mFdOutput, std::ios::in, 0) );
    }
    else if( pid == 0 ) // child
    {
      struct rlimit rlim;
      if( !::getrlimit(RLIMIT_NOFILE, &rlim) )
      {
        for (int i = 0; i < rlim.rlim_cur; ++i)
        {
           struct stat statbuf;
           if( !::fstat(i, &statbuf) && S_ISSOCK(statbuf.st_mode) )
             ::close( i );
        }
      }
      ::dup2(fds[input_|read_], STDIN_FILENO);
      ::dup2(fds[output_|write_], STDOUT_FILENO);
      ::dup2(fds[output_|write_], STDERR_FILENO);
      for( auto fd : fds )
        ::close(fd);
      std::vector<char> argmem;
      for(auto& arg : args)
      {
        argmem.insert(argmem.end(), arg.begin(), arg.end());
        argmem.push_back(0);
      }
      std::vector<char*> argv;
      for(auto i = argmem.begin(); i != argmem.end(); ++i)
      {
        argv.push_back(&*i);
        while(*i)
          ++i;
      }
      argv.push_back(nullptr);
      ::execv(*argv.data(), argv.data());
      ::exit(-errno);
    }
    else
      return false;
    return true;
  }
  void KillChild()
  {
    if( mFdOutputWrite >= 0 )
    {
      char c = '\n';
      if(Running())
      {
        Blocksignal b(SIGINT);
        ::write(mFdOutputWrite, &c, 1);
      }
      ::close(mFdOutputWrite);
      mFdOutputWrite = -1;
    }
    if( Running() )
    {
      ::kill( mPid, SIGKILL );
      ::waitpid( mPid, nullptr, 0 );
      mPid = -1;
    }
    delete mInput.rdbuf();
    mInput.rdbuf( nullptr );
    delete mOutput.rdbuf();
    mOutput.rdbuf( nullptr );
    mFdOutput = -1;
    mFdInput = -1;
  }
  void Raise(int signal)
  {
    if(mPid > 0)
      ::kill(mPid, signal);
  }
  bool Running() const
  {
    if(mPid > 0 && ::waitpid(mPid, nullptr, WNOHANG) > 0)
      mPid = 0;
    return mPid > 0;
  }
};

SlaveProcess::SlaveProcess()
: p( new Private )
{
}

SlaveProcess::~SlaveProcess()
{
  delete p;
}

bool
SlaveProcess::System( const std::string& cmd )
{
  std::vector<std::string> args;
  args.push_back("/bin/sh");
  args.push_back("-c");
  args.push_back(cmd);
  return Exec(args);
}

bool
SlaveProcess::Exec(const std::vector<std::string>& args)
{
  p->KillChild();
  return p->StartChild(args);
}

void
SlaveProcess::Kill()
{
  p->KillChild();
}

void
SlaveProcess::Raise( int signal )
{
  p->Raise(signal);
}

bool
SlaveProcess::Running() const
{
  return p->Running();
}

std::ostream&
SlaveProcess::Input()
{
  return p->mInput;
}

std::istream&
SlaveProcess::Output()
{
  return p->mOutput;
}

bool
SlaveProcess::WaitForOutputMs( int timeout ) const
{
  fd_set readfds, *fdsptr = nullptr;
  FD_ZERO(&readfds);
  int nfds = 0;
  if(p->mFdOutput >= 0)
  {
    FD_SET(p->mFdOutput, &readfds);
    fdsptr = &readfds;
    nfds = p->mFdOutput + 1;
  }
  struct timeval t = { timeout / 1000, 1000 * (timeout % 1000) };
  Blocksignal b(SIGPIPE);
  int n = ::select( p->mFdOutput + 1, fdsptr, nullptr, nullptr, &t );
  return n > 0;
}
