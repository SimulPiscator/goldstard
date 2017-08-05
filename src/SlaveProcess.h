#ifndef SLAVE_PROCESS_H
#define SLAVE_PROCESS_H

#include <string>
#include <vector>
#include <iostream>

class SlaveProcess
{
public:
  SlaveProcess();
  ~SlaveProcess();
  
  bool System( const std::string& );
  
  bool Exec(const std::vector<std::string>& argc);
  void Kill();
  void Raise(int signal);
  bool Running() const;
  
  std::ostream& Input();
  std::istream& Output();
  bool WaitForOutputMs( int ) const;
  
private:
  struct Private;
  Private* p;
};

#endif // SLAVE_PROCESS_H
