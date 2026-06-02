#pragma once
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace Slic3r { namespace GUI { namespace Automation {

// Localhost-only HTTP/1.1 server. POST /jsonrpc -> handler(body) -> response body.
// GET / -> a tiny health/version page. The handler runs on the server's own
// io thread; it is responsible for any further thread marshaling.
class AutomationServer {
public:
    using RequestHandler = std::function<std::string(const std::string& body)>;

    explicit AutomationServer(unsigned short port);
    ~AutomationServer();

    void set_handler(RequestHandler handler) { m_handler = std::move(handler); }
    void set_health_text(std::string text)   { m_health = std::move(text); }

    void start();           // binds to 127.0.0.1:port, starts the io thread
    void stop();            // stops the io thread, joins
    bool is_started() const { return m_started; }
    unsigned short port() const { return m_port; }

private:
    void do_accept();
    void handle_session(boost::asio::ip::tcp::socket socket);

    unsigned short                                m_port;
    std::atomic<bool>                             m_started{false};
    RequestHandler                                m_handler;
    std::string                                   m_health{"OrcaSlicer automation server"};
    std::unique_ptr<boost::asio::io_context>      m_ioc;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> m_acceptor;
    boost::thread                                 m_thread;
};

}}} // namespace
