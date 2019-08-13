#pragma once

#include <badem/lib/errors.hpp>
#include <badem/node/node_rpc_config.hpp>
#include <badem/node/nodeconfig.hpp>
#include <badem/node/openclconfig.hpp>

namespace badem
{
class daemon_config
{
public:
	daemon_config (boost::filesystem::path const & data_path);
	badem::error deserialize_json (bool &, badem::jsonconfig &);
	badem::error serialize_json (badem::jsonconfig &);
	/**
	 * Returns true if an upgrade occurred
	 * @param version The version to upgrade to.
	 * @param config Configuration to upgrade.
	 */
	bool upgrade_json (unsigned version, badem::jsonconfig & config);
	bool rpc_enable{ false };
	badem::node_rpc_config rpc;
	badem::node_config node;
	bool opencl_enable{ false };
	badem::opencl_config opencl;
	boost::filesystem::path data_path;
	unsigned json_version () const
	{
		return 2;
	}
};

badem::error read_and_update_daemon_config (boost::filesystem::path const &, badem::daemon_config & config_a);
}
