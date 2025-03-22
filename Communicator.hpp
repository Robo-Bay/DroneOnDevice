#pragma once
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/bind/bind.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;
using namespace boost::placeholders;

class Communicator {
public:
  Communicator(uint16_t port = 12345)
      : io_context_(),
        udp_socket_(io_context_,
                    asio::ip::udp::endpoint(asio::ip::udp::v4(), port)),
        tcp_acceptor_(io_context_,
                      asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
        discovery_endpoint_(asio::ip::address_v4::broadcast(), port) {
    udp_socket_.set_option(asio::socket_base::broadcast(true));
    start_discovery();
    start_tcp_server();
  }

  ~Communicator() { stop(); }

  void start() {
    running_ = true;
    io_thread_ = std::thread([this]() { io_context_.run(); });
    discovery_thread_ = std::thread([this]() { send_discovery_loop(); });
  }

  void stop() {
    running_ = false;
    io_context_.stop();
    if (io_thread_.joinable())
      io_thread_.join();
    if (discovery_thread_.joinable())
      discovery_thread_.join();
  }

  void send_message(const std::string &message) {
    for (const auto &peer : peers_) {
      asio::ip::tcp::endpoint ep(asio::ip::make_address(peer), port_);
      asio::ip::tcp::socket socket(io_context_);
      try {
        socket.connect(ep);
        socket.write_some(asio::buffer(message + "\n"));
      } catch (...) {
        // Обработка ошибок подключения
      }
    }
  }

private:
  const uint16_t port_ = 12345;
  std::atomic<bool> running_{false};
  std::vector<std::string> peers_;
  std::thread io_thread_;
  std::thread discovery_thread_;

  // Сетевые компоненты
  asio::io_context io_context_;
  asio::ip::udp::socket udp_socket_;
  asio::ip::tcp::acceptor tcp_acceptor_;
  asio::ip::udp::endpoint discovery_endpoint_;

  void start_discovery() {
    udp_socket_.async_receive_from(
        asio::buffer(recv_buffer_), remote_endpoint_,
        [this](boost::system::error_code ec, size_t bytes) {
          if (!ec && bytes > 0) {
            std::string msg(recv_buffer_, bytes);
            if (msg == "DISCOVERY" &&
                remote_endpoint_.address().to_string() != get_local_ip()) {
              add_peer(remote_endpoint_.address().to_string());
            }
          }
          if (running_)
            start_discovery();
        });
  }

  void send_discovery_loop() {
    while (running_) {
      udp_socket_.send_to(asio::buffer("DISCOVERY"), discovery_endpoint_);
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  void start_tcp_server() {
    tcp_acceptor_.async_accept(
        [this](boost::system::error_code ec, asio::ip::tcp::socket socket) {
          if (!ec) {
            std::make_shared<Session>(std::move(socket))->start();
          }
          if (running_)
            start_tcp_server();
        });
  }

  struct Session : std::enable_shared_from_this<Session> {
    Session(asio::ip::tcp::socket socket) : socket_(std::move(socket)) {}

    void start() {
      asio::async_read_until(
          socket_, asio::dynamic_buffer(data_), '\n',
          [self = shared_from_this()](boost::system::error_code ec, size_t) {
            if (!ec) {
              std::cout << "Received: " << self->data_;
              self->data_.clear();
            }
          });
    }

    asio::ip::tcp::socket socket_;
    std::string data_;
  };

  void add_peer(const std::string &ip) {
    if (std::find(peers_.begin(), peers_.end(), ip) == peers_.end()) {
      peers_.push_back(ip);
      std::cout << "Discovered new peer: " << ip << std::endl;
    }
  }

  std::string get_local_ip() {
    boost::asio::io_context io_context;
    boost::asio::ip::udp::resolver resolver(io_context);

    auto results = resolver.resolve(boost::asio::ip::udp::v4(),
                                    boost::asio::ip::host_name(), "");

    // Перебираем все результаты пока не найдем IPv4 адрес
    for (const auto &entry : results) {
      auto addr = entry.endpoint().address();
      if (addr.is_v4() && !addr.is_loopback()) {
        return addr.to_string();
      }
    }

    return ""; // Если ничего не найдено
  }

  char recv_buffer_[1024];
  asio::ip::udp::endpoint remote_endpoint_;
};
