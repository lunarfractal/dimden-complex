#define ASIO_STANDALONE

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

#include <asio.hpp>
#include <asio/steady_timer.hpp>

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <memory>
#include <vector>


using websocketpp::connection_hdl;
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;
typedef websocketpp::client<websocketpp::config::asio_tls_client> ws_client;

class WebSocket {
public:
    WebSocket(const std::string& uri, std::shared_ptr<asio::io_context> io_context)
        : client_(), uri_(uri), reconnect_timer_(*io_context), message_timer_(*io_context),
           reconnect_attempts_(0), reconnect_interval_(1000), max_reconnect_attempts_(4),
           message_interval_(16.7) {
        client_.clear_access_channels(websocketpp::log::alevel::all);
        client_.init_asio(io_context.get());

        client_.set_open_handler([this](connection_hdl hdl) {
            hdl_ = hdl;
            client_.get_alog().write(websocketpp::log::alevel::connect, "Connected!");
            reconnect_attempts_ = 0;

            start_message_timer();
        });

        //client_.set_message_handler([this](connection_hdl, message_ptr msg) {});

        client_.set_fail_handler([this](connection_hdl hdl) {
            auto con = client_.get_con_from_hdl(hdl);
            client_.get_elog().write(websocketpp::log::elevel::rerror, "Error: " + con->get_ec().message());

            start_reconnect_timer();
        });

        client_.set_tls_init_handler([](websocketpp::connection_hdl) {
            return websocketpp::lib::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12_client);
        });


        client_.set_close_handler([this](connection_hdl) {
            start_reconnect_timer();
        });

        client_.get_alog().write(websocketpp::log::alevel::app, "Connecting to " + uri + "...");

        connect();
    }

    void connect() {
        if(reconnect_attempts_ > max_reconnect_attempts_) {
            return;
        }

        reconnect_attempts_++;
      
        websocketpp::lib::error_code ec;
        auto con = client_.get_connection(uri_, ec);
        if (ec) {
            client_.get_elog().write(websocketpp::log::elevel::rerror, "socket error: " + ec.message());
            return;
        }

        con->set_proxy("http://localhost:8087");
        client_.connect(con);
    }

    void set_reconnect_interval(int n) {
        reconnect_interval_ = n;
    }

    void set_reconnect_attempts(int n) {
        max_reconnect_attempts_ = n;
    }

    void set_message_interval(int n) {
        message_interval_ = n;
    }

    void start_reconnect_timer() {
        reconnect_timer_.expires_after(std::chrono::milliseconds(reconnect_interval_));
        reconnect_timer_.async_wait([this](const websocketpp::lib::error_code& ec) {
            if (ec) {
                client_.get_elog().write(websocketpp::log::elevel::rerror, "Reconnect timer failed: " + ec.message());
            } else {
                connect();
            }
        });
    }

    void start_message_timer() {
        message_timer_.expires_after(std::chrono::milliseconds(message_interval_));

        message_timer_.async_wait([this](const websocketpp::lib::error_code& ec) {
            if (ec) {
                client_.get_elog().write(websocketpp::log::elevel::rerror, "Message timer failed: " + ec.message());
               return;
            }

            static std::string data = "{\"operation\":\"ping\",\"music\":\"never meant to leave you hurting.. never meant to do the worst thing.. not to you.. I can't take no more.. no more.. no more... no.. I got nobody.. here on my own.. i got no body so I do it solo...\"}";

            websocketpp::lib::error_code send_ec;
            client_.send(hdl_, data, websocketpp::frame::opcode::text, send_ec);

            if (send_ec) {
                client_.get_elog().write(websocketpp::log::elevel::rerror,
                    "Send failed: " + send_ec.message());
            }

            start_message_timer();
        });
    }


private:
    ws_client client_;
    std::string uri_;

    connection_hdl hdl_;

    asio::steady_timer reconnect_timer_;
    asio::steady_timer message_timer_;

    int reconnect_attempts_;
    int reconnect_interval_;
    int max_reconnect_attempts_;
    int message_interval_;
};

int main() {
    std::shared_ptr<asio::io_context> io_context = std::make_shared<asio::io_context>();
    auto work = asio::make_work_guard(*io_context);

    std::vector<std::unique_ptr<WebSocket>> clients;
    std::string uri = "wss://dimden.dev/services/chat";

    for(int i = 0; i < 200; i++) {
        for (int j = 0; j < 4; j++) {
            clients.emplace_back(std::make_unique<WebSocket>(uri, io_context));
        }
        change_proxy();// implement this later
    }
    
    io_context->run();
  
    io_context->stop();
    for (auto& t : thread_pool) {
        t.join();
    }

    return 0;
}
