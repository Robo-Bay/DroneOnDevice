#pragma once
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/asio/ip/udp.hpp>
#include <chrono>
#include <cppSwarmLib/Logger.hpp>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace boost::asio;
using ip::tcp;
using namespace std::chrono_literals;

class Communicator {
  boost::asio::io_context io_context;
  std::unique_ptr<ip::udp::socket> discovery_socket;
  std::unique_ptr<tcp::acceptor> tcp_acceptor;
  std::atomic<bool> running{false};
  std::thread network_thread;
  swarm::BaseLogger bl;

  const unsigned discovery_port = 55555;
  const unsigned tcp_port = 55556;

  void start_discovery_listener() {
    try {
      discovery_socket = std::make_unique<ip::udp::socket>(io_context);

      // Открываем сокет перед установкой опций
      discovery_socket->open(ip::udp::v4());
      discovery_socket->set_option(
          boost::asio::socket_base::reuse_address(true));
      discovery_socket->bind(ip::udp::endpoint(ip::udp::v4(), discovery_port));

      auto *endpoint = new ip::udp::endpoint();

      char recv_buffer[1024];
      discovery_socket->async_receive_from(
          boost::asio::buffer(recv_buffer, sizeof(recv_buffer)),
          *endpoint, // endpoint передается по ссылке
          [this, endpoint, recv_buffer](boost::system::error_code ec,
                                        size_t bytes) {
            if (!ec && bytes > 0) {
              const std::string response = "SWARM_DEVICE_RESPONSE";
              discovery_socket->send_to(boost::asio::buffer(response),
                                        *endpoint);
            }
            if (running) {
              delete endpoint;            // Удаляем старый endpoint
              start_discovery_listener(); // Перезапускаем слушатель
            }
          });
    } catch (const boost::system::system_error &ex) {
      bl.error("UDP bind failed: " + std::string(ex.what()));
      throw;
    }
  }

  void start_tcp_server() {
    try {
      tcp_acceptor = std::make_unique<tcp::acceptor>(io_context);

      // Устанавливаем опции перед биндом
      tcp_acceptor->open(tcp::v4());
      tcp_acceptor->set_option(boost::asio::socket_base::reuse_address(true));
      tcp_acceptor->bind(tcp::endpoint(tcp::v4(), tcp_port));
      tcp_acceptor->listen();

      auto accept_handler = [this](boost::system::error_code ec,
                                   tcp::socket socket) {
        if (!ec) {
          std::thread([this, s = std::move(socket)]() mutable {
            try {
              char data[1024];
              size_t length = s.read_some(boost::asio::buffer(data));
              bl.info("Message from " +
                      s.remote_endpoint().address().to_string() + ": " +
                      std::string(data, length));
            } catch (...) {
            }
          }).detach();
        }
        if (running)
          start_tcp_server();
      };

      tcp_acceptor->async_accept(accept_handler);
    } catch (const boost::system::system_error &ex) {
      bl.error("TCP bind failed: " + std::string(ex.what()));
      throw;
    }
  }

public:
  explicit Communicator() : bl("communicator") {}

  ~Communicator() {
    running = false;
    io_context.stop();
    if (network_thread.joinable())
      network_thread.join();
    bl.info("Communicator shutdown complete");
  }

  void init() {
    running = true;
    network_thread = std::thread([this]() {
      start_discovery_listener();
      start_tcp_server();
      io_context.run();
    });
    bl.info("Network subsystem initialized");
  }

  std::vector<std::string> scan_network() {
    boost::asio::io_context svc;
    ip::udp::socket socket(svc);
    socket.open(ip::udp::v4());
    socket.set_option(socket_base::broadcast(true));

    // Отправка broadcast
    const std::string msg = "DISCOVER_SWARM_DEVICE";
    socket.send_to(
        boost::asio::buffer(msg),
        ip::udp::endpoint(ip::address_v4::broadcast(), discovery_port));

    std::vector<std::string> devices;
    char recv_buffer[1024];
    ip::udp::endpoint sender;

    // Исправленный таймаут
    socket.non_blocking(true); // Включаем неблокирующий режим
    deadline_timer timer(svc);
    // timer.expires_from_now(500ms);
    timer.expires_from_now(deadline_timer::duration_type(0, 0, 0.5));

    // Асинхронное ожидание
    bool timeout = false;
    timer.async_wait([&](const boost::system::error_code &) {
      timeout = true;
      socket.cancel();
    });

    while (running && !timeout) {
      boost::system::error_code ec;
      size_t bytes =
          socket.receive_from(boost::asio::buffer(recv_buffer, 1024), sender,
                              0, // flags
                              ec);

      if (ec) {
        if (std::string(recv_buffer, bytes) == "SWARM_DEVICE_RESPONSE") {
          devices.push_back(sender.address().to_string());
        }
      } else if (ec != error::would_block) {
        break;
      }

      svc.poll(); // Обрабатываем таймер
    }

    bl.info("Discovered " + std::to_string(devices.size()) + " devices");
    return devices;
  }

  std::unique_ptr<tcp::socket> connect_to(const std::string &ip) {
    auto socket = std::make_unique<tcp::socket>(io_context);
    try {
      socket->connect(tcp::endpoint(ip::make_address(ip), tcp_port));
      bl.info("Connected to " + ip);
      return socket;
    } catch (const boost::system::system_error &ex) {
      bl.error("Connection to " + ip + " failed: " + ex.what());
      return nullptr;
    }
  }

  void send_message(const std::string &message, tcp::socket &socket) {
    try {
      write(socket, boost::asio::buffer(message + "\n"));
    } catch (const boost::system::system_error &ex) {
      bl.error("Send failed: " + std::string(ex.what()));
    }
  }
};