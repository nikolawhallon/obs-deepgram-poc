#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

#include <websocketpp/common/thread.hpp>
#include <websocketpp/common/memory.hpp>

#include <iostream>

#ifndef WEBSOCKET_ENDPOINT_HPP
#define WEBSOCKET_ENDPOINT_HPP

typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef std::shared_ptr<boost::asio::ssl::context> context_ptr;

class ConnectionMetadata {
public:
    typedef websocketpp::lib::shared_ptr<ConnectionMetadata> ptr;

    ConnectionMetadata(int id, websocketpp::connection_hdl hdl, std::string uri)
      : m_id(id)
      , m_hdl(hdl)
      , m_status("Connecting")
      , m_uri(uri)
      , m_server("N/A")
    {}

    void on_open(client * c, websocketpp::connection_hdl hdl) {
        m_status = "Open";

        client::connection_ptr con = c->get_con_from_hdl(hdl);
        m_server = con->get_response_header("Server");
    }

    void on_fail(client * c, websocketpp::connection_hdl hdl) {
        m_status = "Failed";

        client::connection_ptr con = c->get_con_from_hdl(hdl);
        m_server = con->get_response_header("Server");
        m_error_reason = con->get_ec().message();
    }
    
    void on_close(client * c, websocketpp::connection_hdl hdl) {
        m_status = "Closed";
        client::connection_ptr con = c->get_con_from_hdl(hdl);
        std::stringstream s;
        s << "close code: " << con->get_remote_close_code() << " (" 
          << websocketpp::close::status::get_string(con->get_remote_close_code()) 
          << "), close reason: " << con->get_remote_close_reason();
        m_error_reason = s.str();
    }

    void on_message(websocketpp::connection_hdl, client::message_ptr msg) {
        if (msg->get_opcode() == websocketpp::frame::opcode::text) {
    		blog(LOG_WARNING, "%s", msg->get_payload().c_str());
            m_messages.push_back("<< " + msg->get_payload());
        } else {
            m_messages.push_back("<< " + websocketpp::utility::to_hex(msg->get_payload()));
        }
    }

    websocketpp::connection_hdl get_hdl() const {
        return m_hdl;
    }
    
    int get_id() const {
        return m_id;
    }
    
    std::string get_status() const {
        return m_status;
    }

    void record_sent_message(std::string message) {
        m_messages.push_back(">> " + message);
    }

    friend std::ostream & operator<< (std::ostream & out, ConnectionMetadata const & data);
private:
    int m_id;
    websocketpp::connection_hdl m_hdl;
    std::string m_status;
    std::string m_uri;
    std::string m_server;
    std::string m_error_reason;
    std::vector<std::string> m_messages;
};

std::ostream & operator<< (std::ostream & out, ConnectionMetadata const & data) {
    out << "> URI: " << data.m_uri << "\n"
        << "> Status: " << data.m_status << "\n"
        << "> Remote Server: " << (data.m_server.empty() ? "None Specified" : data.m_server) << "\n"
        << "> Error/close reason: " << (data.m_error_reason.empty() ? "N/A" : data.m_error_reason) << "\n";
    out << "> Messages Processed: (" << data.m_messages.size() << ") \n";

    std::vector<std::string>::const_iterator it;
    for (it = data.m_messages.begin(); it != data.m_messages.end(); ++it) {
        out << *it << "\n";
    }

    return out;
}

class WebsocketEndpoint {
public:
    WebsocketEndpoint () : m_next_id(0) {
        m_endpoint.clear_access_channels(websocketpp::log::alevel::all);
        m_endpoint.clear_error_channels(websocketpp::log::elevel::all);

        m_endpoint.init_asio();
		m_endpoint.set_tls_init_handler(bind(&on_tls_init));
        m_endpoint.start_perpetual();

        m_thread = websocketpp::lib::make_shared<websocketpp::lib::thread>(&client::run, &m_endpoint);
    }

    ~WebsocketEndpoint() {
        std::cout << "DESTROYING WEBSOCKET MAYBE?" << std::endl;
        m_endpoint.stop_perpetual();
        
        for (con_list::const_iterator it = m_connection_list.begin(); it != m_connection_list.end(); ++it) {
            if (it->second->get_status() != "Open") {
                // Only close open connections
                continue;
            }
            
            std::cout << "> Closing connection " << it->second->get_id() << std::endl;
            
            websocketpp::lib::error_code ec;
            m_endpoint.close(it->second->get_hdl(), websocketpp::close::status::going_away, "", ec);
            if (ec) {
                std::cout << "> Error closing connection " << it->second->get_id() << ": "  
                          << ec.message() << std::endl;
            }
        }
        
        m_thread->join();
    }

static context_ptr on_tls_init() {
    // establishes a SSL connection
    context_ptr ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);

    try {
        ctx->set_options(boost::asio::ssl::context::default_workarounds |
                         boost::asio::ssl::context::no_sslv2 |
                         boost::asio::ssl::context::no_sslv3 |
                         boost::asio::ssl::context::single_dh_use);
    } catch (std::exception &e) {
        std::cout << "Error in context pointer: " << e.what() << std::endl;
    }
    return ctx;
}

    int connect(std::string const & uri) {
        websocketpp::lib::error_code ec;
		blog(LOG_WARNING, "About to get connection.");
        client::connection_ptr con = m_endpoint.get_connection(uri, ec);
        if (ec) {
            std::cout << "> Connect initialization error: " << ec.message() << std::endl;
            return -1;
        }

		// my guess is just I guess to do this later, right before the connect
		bool test = con->is_server();
		if (test) {
			blog(LOG_WARNING, "is server");
		} else {
			blog(LOG_WARNING, "is not server");
		}
		blog(LOG_WARNING, "About to try to append some header...");
		con->replace_header("Authorization", "Token 1eb45f404f76787f1eb8eb440505982264744776");
		blog(LOG_WARNING, "Successfully appended header, I guess.");

        int new_id = m_next_id++;
        ConnectionMetadata::ptr metadata_ptr = websocketpp::lib::make_shared<ConnectionMetadata>(new_id, con->get_handle(), uri);
        m_connection_list[new_id] = metadata_ptr;

        con->set_open_handler(websocketpp::lib::bind(
            &ConnectionMetadata::on_open,
            metadata_ptr,
            &m_endpoint,
            websocketpp::lib::placeholders::_1
        ));
        con->set_fail_handler(websocketpp::lib::bind(
            &ConnectionMetadata::on_fail,
            metadata_ptr,
            &m_endpoint,
            websocketpp::lib::placeholders::_1
        ));
        con->set_close_handler(websocketpp::lib::bind(
            &ConnectionMetadata::on_close,
            metadata_ptr,
            &m_endpoint,
            websocketpp::lib::placeholders::_1
        ));
        con->set_message_handler(websocketpp::lib::bind(
            &ConnectionMetadata::on_message,
            metadata_ptr,
            websocketpp::lib::placeholders::_1,
            websocketpp::lib::placeholders::_2
        ));

		blog(LOG_WARNING, "About to call the real inner connect function.");
        m_endpoint.connect(con);
		blog(LOG_WARNING, "Done calling the real inner connect function.");

        return new_id;
    }

    void close(int id, websocketpp::close::status::value code, std::string reason) {
        websocketpp::lib::error_code ec;
        
        con_list::iterator metadata_it = m_connection_list.find(id);
        if (metadata_it == m_connection_list.end()) {
            std::cout << "> No connection found with id " << id << std::endl;
            return;
        }
        
        m_endpoint.close(metadata_it->second->get_hdl(), code, reason, ec);
        if (ec) {
            std::cout << "> Error initiating close: " << ec.message() << std::endl;
        }
    }

    void send_text(int id, std::string message) {
        websocketpp::lib::error_code ec;
        
        con_list::iterator metadata_it = m_connection_list.find(id);
        if (metadata_it == m_connection_list.end()) {
            std::cout << "> No connection found with id " << id << std::endl;
            return;
        }
        
        m_endpoint.send(metadata_it->second->get_hdl(), message, websocketpp::frame::opcode::text, ec);
        if (ec) {
            std::cout << "> Error sending message: " << ec.message() << std::endl;
            return;
        }
        
        metadata_it->second->record_sent_message(message);
    }

    void send_binary(int id, void const * message, size_t len) {
        websocketpp::lib::error_code ec;
        
        con_list::iterator metadata_it = m_connection_list.find(id);
        if (metadata_it == m_connection_list.end()) {
            std::cout << "> No connection found with id " << id << std::endl;
            return;
        }
        
        m_endpoint.send(metadata_it->second->get_hdl(), message, len, websocketpp::frame::opcode::binary, ec);
        if (ec) {
            std::cout << "> Error sending message: " << ec.message() << std::endl;
            return;
        }
        
        metadata_it->second->record_sent_message("binary");
    }

    ConnectionMetadata::ptr get_metadata(int id) const {
        con_list::const_iterator metadata_it = m_connection_list.find(id);
        if (metadata_it == m_connection_list.end()) {
            return ConnectionMetadata::ptr();
        } else {
            return metadata_it->second;
        }
    }
private:
    typedef std::map<int,ConnectionMetadata::ptr> con_list;

    client m_endpoint;
    websocketpp::lib::shared_ptr<websocketpp::lib::thread> m_thread;

    con_list m_connection_list;
    int m_next_id;
};

#endif //WEBSOCKET_ENDPOINT_HPP