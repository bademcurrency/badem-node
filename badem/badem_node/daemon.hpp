#include <boost/filesystem.hpp>

namespace badem
{
class node_flags;
}
namespace badem_daemon
{
class daemon
{
public:
	void run (boost::filesystem::path const &, badem::node_flags const & flags);
};
}
