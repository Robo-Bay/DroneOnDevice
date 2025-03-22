#include "Communicator.hpp"

int main() {
  Communicator comm;
  comm.start();

  char b;
  do {
    comm.send_message("Hello from Device!");
    std::cin >> b;
  } while (b != 'b');

  std::cin.get();
  comm.stop();
  return 0;
}