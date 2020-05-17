#pragma once

#include <badem/lib/rpcconfig.hpp>

#include <boost/filesystem.hpp>

#include <string>

namespace badem
{
class tomlconfig;
class rpc_child_process_config final
{
public:
	bool enable{ false };
	std::string rpc_path{ get_default_rpc_filepath () };
};

class node_rpc_config final
{
public:
	badem::error serialize_json (badem::jsonconfig &) const;
	badem::error deserialize_json (bool & upgraded_a, badem::jsonconfig &, boost::filesystem::path const & data_path);
	badem::error serialize_toml (badem::tomlconfig & toml) const;
	badem::error deserialize_toml (badem::tomlconfig & toml);

	bool enable_sign_hash{ false };
	badem::rpc_child_process_config child_process;
	static unsigned json_version ()
	{
		return 1;
	}

private:
	void migrate (badem::jsonconfig & json, boost::filesystem::path const & data_path);
};
}
