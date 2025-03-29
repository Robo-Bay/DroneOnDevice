#include "Communicator.hpp"
// #include <cppSwarmLib/Logger.hpp>

int main() {
  Communicator comm;
  comm.init();

  auto devices = comm.scan_network();
  if(devices.size() != 0)
  if (auto socket = comm.connect_to(devices[0])) {
    comm.send_message("Hello device!", *socket);
  }
}