#include <badem/node/openclconfig.hpp>

badem::opencl_config::opencl_config (unsigned platform_a, unsigned device_a, unsigned threads_a) :
platform (platform_a),
device (device_a),
threads (threads_a)
{
}

badem::error badem::opencl_config::serialize_json (badem::jsonconfig & json) const
{
	json.put ("platform", platform);
	json.put ("device", device);
	json.put ("threads", threads);
	return json.get_error ();
}

badem::error badem::opencl_config::deserialize_json (badem::jsonconfig & json)
{
	json.get_optional<unsigned> ("platform", platform);
	json.get_optional<unsigned> ("device", device);
	json.get_optional<unsigned> ("threads", threads);
	return json.get_error ();
}
