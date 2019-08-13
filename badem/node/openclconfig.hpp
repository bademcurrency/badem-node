#pragma once

#include <badem/lib/errors.hpp>
#include <badem/lib/jsonconfig.hpp>

namespace badem
{
class opencl_config
{
public:
	opencl_config () = default;
	opencl_config (unsigned, unsigned, unsigned);
	badem::error serialize_json (badem::jsonconfig &) const;
	badem::error deserialize_json (badem::jsonconfig &);
	unsigned platform{ 0 };
	unsigned device{ 0 };
	unsigned threads{ 1024 * 1024 };
};
}
