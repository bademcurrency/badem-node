#pragma once

#include <badem/lib/rpcconfig.hpp>

#include <boost/filesystem.hpp>

#include <string>

namespace badem
{
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
	bool enable_sign_hash{ false };
	uint64_t max_work_generate_difficulty{ 0xffffffffc0000000 };
	badem::rpc_child_process_config child_process;
	static unsigned json_version ()
	{
		return 1;
	}

private:
	void migrate (badem::jsonconfig & json, boost::filesystem::path const & data_path);
};
}
