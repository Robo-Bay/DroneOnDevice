#include <cppSwarmLib/Logger.hpp>

int main() {
  swarm::BaseLogger l1("Swarm Logger 1");
  swarm::BaseLogger l2("Swarm Logger 2");
  swarm::BaseLogger l3("Swarm Logger 3");
  l1.error("hello1", "Drone 1");
  l1.info("hello1", "Drone 2");
  l2.warning("bye1", "Drone 1");
  l2.debug("bye2", "Drone 1");
  l3.fatal("hehe", "Fedor");
}