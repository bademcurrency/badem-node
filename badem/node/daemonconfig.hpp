#pragma once

#include <badem/lib/errors.hpp>
#include <badem/node/node_pow_server_config.hpp>
#include <badem/node/node_rpc_config.hpp>
#include <badem/node/nodeconfig.hpp>
#include <badem/node/openclconfig.hpp>

#include <vector>

namespace badem
{
class jsonconfig;
class tomlconfig;
class daemon_config
{
public:
	daemon_config () = default;
	daemon_config (boost::filesystem::path const & data_path);
	badem::error deserialize_json (bool &, badem::jsonconfig &);
	badem::error serialize_json (badem::jsonconfig &);
	badem::error deserialize_toml (badem::tomlconfig &);
	badem::error serialize_toml (badem::tomlconfig &);
	bool rpc_enable{ false };
	badem::node_rpc_config rpc;
	badem::node_config node;
	bool opencl_enable{ false };
	badem::opencl_config opencl;
	badem::node_pow_server_config pow_server;
	boost::filesystem::path data_path;
	unsigned json_version () const
	{
		return 2;
	}
};

badem::error read_node_config_toml (boost::filesystem::path const &, badem::daemon_config & config_a, std::vector<std::string> const & config_overrides = std::vector<std::string> ());
badem::error read_and_update_daemon_config (boost::filesystem::path const &, badem::daemon_config & config_a, badem::jsonconfig & json_a);
}
