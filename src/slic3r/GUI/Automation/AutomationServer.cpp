#include "AutomationServer.hpp"
#include "libslic3r/Thread.hpp" // create_thread / set_current_thread_name

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/log/trivial.hpp>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;

namespace Slic3r { namespace GUI { namespace Automation {

AutomationServer::AutomationServer(unsigned short port) : m_port(port) {}

AutomationServer::~AutomationServer() { stop(); }

void AutomationServer::start() {
    if (m_started) return;
    m_ioc = std::make_unique<net::io_context>(1);
    // Bind to loopback ONLY.
    tcp::endpoint endpoint(net::ip::make_address("127.0.0.1"), m_port);
    m_acceptor = std::make_unique<tcp::acceptor>(*m_ioc);
    m_acceptor->open(endpoint.protocol());
    m_acceptor->set_option(net::socket_base::reuse_address(true));
    m_acceptor->bind(endpoint);
    m_acceptor->listen(net::socket_base::max_listen_connections);
    m_started = true;

    do_accept();

    net::io_context* ioc = m_ioc.get();
    m_thread = create_thread([ioc] {
        set_current_thread_name("orca_automation");
        ioc->run();
    });
    BOOST_LOG_TRIVIAL(info) << "AutomationServer listening on 127.0.0.1:" << m_port;
}

void AutomationServer::stop() {
    if (!m_started) return;
    m_started = false;
    if (m_ioc) m_ioc->stop();
    if (m_thread.joinable()) m_thread.join();
    m_acceptor.reset();
    m_ioc.reset();
}

void AutomationServer::do_accept() {
    m_acceptor->async_accept([this](beast::error_code ec, tcp::socket socket) {
        if (!ec) {
            // v1: single-client, serialized — handle synchronously on the io thread.
            handle_session(std::move(socket));
        }
        if (m_started && m_acceptor && m_acceptor->is_open())
            do_accept();
    });
}

void AutomationServer::handle_session(tcp::socket socket) {
    beast::error_code ec;
    beast::flat_buffer buffer;
    http::request<http::string_body> req;
    http::read(socket, buffer, req, ec);
    if (ec) { socket.shutdown(tcp::socket::shutdown_send, ec); return; }

    http::response<http::string_body> res;
    res.version(req.version());
    res.keep_alive(false);

    if (req.method() == http::verb::post && req.target() == "/jsonrpc") {
        std::string body_out;
        try {
            body_out = m_handler ? m_handler(req.body())
                                 : R"({"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":"no handler"}})";
        } catch (const std::exception& e) {
            body_out = std::string(R"({"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":")")
                       + e.what() + R"("}})";
        }
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = std::move(body_out);
    } else if (req.method() == http::verb::get && req.target() == "/") {
        res.result(http::status::ok);
        res.set(http::field::content_type, "text/plain");
        res.body() = m_health;
    } else {
        res.result(http::status::not_found);
        res.set(http::field::content_type, "text/plain");
        res.body() = "not found";
    }
    res.set(http::field::server, "OrcaSlicer/automation");
    res.prepare_payload();
    http::write(socket, res, ec);
    socket.shutdown(tcp::socket::shutdown_send, ec);
}

}}} // namespace
