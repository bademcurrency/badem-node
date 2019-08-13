#include <badem/node/lmdb/lmdb_env.hpp>

badem::mdb_env::mdb_env (bool & error_a, boost::filesystem::path const & path_a, int max_dbs_a, bool use_no_mem_init_a, size_t map_size_a)
{
	boost::system::error_code error_mkdir, error_chmod;
	if (path_a.has_parent_path ())
	{
		boost::filesystem::create_directories (path_a.parent_path (), error_mkdir);
		badem::set_secure_perm_directory (path_a.parent_path (), error_chmod);
		if (!error_mkdir)
		{
			auto status1 (mdb_env_create (&environment));
			release_assert (status1 == 0);
			auto status2 (mdb_env_set_maxdbs (environment, max_dbs_a));
			release_assert (status2 == 0);
			auto map_size = map_size_a;
			auto max_valgrind_map_size = 16 * 1024 * 1024;
			if (running_within_valgrind () && map_size_a > max_valgrind_map_size)
			{
				// In order to run LMDB under Valgrind, the maximum map size must be smaller than half your available RAM
				map_size = max_valgrind_map_size;
			}
			auto status3 (mdb_env_set_mapsize (environment, map_size));
			release_assert (status3 == 0);
			// It seems if there's ever more threads than mdb_env_set_maxreaders has read slots available, we get failures on transaction creation unless MDB_NOTLS is specified
			// This can happen if something like 256 io_threads are specified in the node config
			// MDB_NORDAHEAD will allow platforms that support it to load the DB in memory as needed.
			// MDB_NOMEMINIT prevents zeroing malloc'ed pages. Can provide improvement for non-sensitive data but may make memory checkers noisy (e.g valgrind).
			auto environment_flags = MDB_NOSUBDIR | MDB_NOTLS | MDB_NORDAHEAD;
			if (!running_within_valgrind () && use_no_mem_init_a)
			{
				environment_flags |= MDB_NOMEMINIT;
			}
			auto status4 (mdb_env_open (environment, path_a.string ().c_str (), environment_flags, 00600));
			if (status4 != 0)
			{
				std::cerr << "Could not open lmdb environment: " << status4;
				char * error_str (mdb_strerror (status4));
				if (error_str)
				{
					std::cerr << ", " << error_str;
				}
				std::cerr << std::endl;
			}
			release_assert (status4 == 0);
			error_a = status4 != 0;
		}
		else
		{
			error_a = true;
			environment = nullptr;
		}
	}
	else
	{
		error_a = true;
		environment = nullptr;
	}
}

badem::mdb_env::~mdb_env ()
{
	if (environment != nullptr)
	{
		mdb_env_close (environment);
	}
}

badem::mdb_env::operator MDB_env * () const
{
	return environment;
}

badem::read_transaction badem::mdb_env::tx_begin_read (mdb_txn_callbacks mdb_txn_callbacks) const
{
	return badem::read_transaction{ std::make_unique<badem::read_mdb_txn> (*this, mdb_txn_callbacks) };
}

badem::write_transaction badem::mdb_env::tx_begin_write (mdb_txn_callbacks mdb_txn_callbacks) const
{
	return badem::write_transaction{ std::make_unique<badem::write_mdb_txn> (*this, mdb_txn_callbacks) };
}

MDB_txn * badem::mdb_env::tx (badem::transaction const & transaction_a) const
{
	return static_cast<MDB_txn *> (transaction_a.get_handle ());
}
