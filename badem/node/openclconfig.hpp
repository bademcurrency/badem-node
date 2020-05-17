#pragma once

#include <badem/lib/errors.hpp>

namespace badem
{
class jsonconfig;
class tomlconfig;
class opencl_config
{
public:
	opencl_config () = default;
	opencl_config (unsigned, unsigned, unsigned);
	badem::error serialize_json (badem::jsonconfig &) const;
	badem::error deserialize_json (badem::jsonconfig &);
	badem::error serialize_toml (badem::tomlconfig &) const;
	badem::error deserialize_toml (badem::tomlconfig &);
	unsigned platform{ 0 };
	unsigned device{ 0 };
	unsigned threads{ 1024 * 1024 };
};
}
