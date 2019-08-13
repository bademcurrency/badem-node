#pragma once

#include <badem/lib/ipc.hpp>
#include <badem/lib/numbers.hpp>
#include <badem/node/node_rpc_config.hpp>

#include <atomic>

namespace badem
{
class node;

namespace ipc
{
	/** The IPC server accepts connections on one or more configured transports */
	class ipc_server
	{
	public:
		ipc_server (badem::node & node_a, badem::node_rpc_config const & node_rpc_config);

		virtual ~ipc_server ();
		void stop ();

		badem::node & node;
		badem::node_rpc_config const & node_rpc_config;

		/** Unique counter/id shared across sessions */
		std::atomic<uint64_t> id_dispenser{ 0 };

	private:
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<badem::ipc::transport>> transports;
	};
}
}
