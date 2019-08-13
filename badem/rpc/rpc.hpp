#pragma once

#include <badem/boost/asio.hpp>
#include <badem/lib/logger_mt.hpp>
#include <badem/lib/rpc_handler_interface.hpp>
#include <badem/lib/rpcconfig.hpp>

namespace badem
{
class rpc_handler_interface;

class rpc
{
public:
	rpc (boost::asio::io_context & io_ctx_a, badem::rpc_config const & config_a, badem::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc ();
	void start ();
	virtual void accept ();
	void stop ();

	badem::rpc_config config;
	boost::asio::ip::tcp::acceptor acceptor;
	badem::logger_mt logger;
	boost::asio::io_context & io_ctx;
	badem::rpc_handler_interface & rpc_handler_interface;
	bool stopped{ false };
};

/** Returns the correct RPC implementation based on TLS configuration */
std::unique_ptr<badem::rpc> get_rpc (boost::asio::io_context & io_ctx_a, badem::rpc_config const & config_a, badem::rpc_handler_interface & rpc_handler_interface_a);
}
