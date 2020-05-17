#pragma once

#include <badem/secure/blockstore.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>

namespace
{
inline bool is_read (badem::transaction const & transaction_a)
{
	return (dynamic_cast<const badem::read_transaction *> (&transaction_a) != nullptr);
}

inline rocksdb::ReadOptions const & snapshot_options (badem::transaction const & transaction_a)
{
	assert (is_read (transaction_a));
	return *static_cast<const rocksdb::ReadOptions *> (transaction_a.get_handle ());
}
}

namespace badem
{
using rocksdb_val = db_val<rocksdb::Slice>;

template <typename T, typename U>
class rocksdb_iterator : public store_iterator_impl<T, U>
{
public:
	rocksdb_iterator (rocksdb::DB * db, badem::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle_a)
	{
		rocksdb::Iterator * iter;
		if (is_read (transaction_a))
		{
			iter = db->NewIterator (snapshot_options (transaction_a), handle_a);
		}
		else
		{
			rocksdb::ReadOptions ropts;
			ropts.fill_cache = false;
			iter = tx (transaction_a)->GetIterator (ropts, handle_a);
		}

		cursor.reset (iter);
		cursor->SeekToFirst ();

		if (cursor->Valid ())
		{
			current.first.value = cursor->key ();
			current.second.value = cursor->value ();
		}
		else
		{
			clear ();
		}
	}

	rocksdb_iterator () = default;

	rocksdb_iterator (rocksdb::DB * db, badem::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle_a, rocksdb_val const & val_a)
	{
		rocksdb::Iterator * iter;
		if (is_read (transaction_a))
		{
			iter = db->NewIterator (snapshot_options (transaction_a), handle_a);
		}
		else
		{
			iter = tx (transaction_a)->GetIterator (rocksdb::ReadOptions (), handle_a);
		}

		cursor.reset (iter);
		cursor->Seek (val_a);

		if (cursor->Valid ())
		{
			current.first = cursor->key ();
			current.second = cursor->value ();
		}
		else
		{
			clear ();
		}
	}

	rocksdb_iterator (badem::rocksdb_iterator<T, U> && other_a)
	{
		cursor = other_a.cursor;
		other_a.cursor = nullptr;
		current = other_a.current;
	}

	rocksdb_iterator (badem::rocksdb_iterator<T, U> const &) = delete;

	badem::store_iterator_impl<T, U> & operator++ () override
	{
		cursor->Next ();
		if (cursor->Valid ())
		{
			current.first = cursor->key ();
			current.second = cursor->value ();

			if (current.first.size () != sizeof (T))
			{
				clear ();
			}
		}
		else
		{
			clear ();
		}

		return *this;
	}

	std::pair<badem::rocksdb_val, badem::rocksdb_val> * operator-> ()
	{
		return &current;
	}

	bool operator== (badem::store_iterator_impl<T, U> const & base_a) const override
	{
		auto const other_a (boost::polymorphic_downcast<badem::rocksdb_iterator<T, U> const *> (&base_a));

		if (!current.first.data () && !other_a->current.first.data ())
		{
			return true;
		}
		else if (!current.first.data () || !other_a->current.first.data ())
		{
			return false;
		}

		auto result (std::memcmp (current.first.data (), other_a->current.first.data (), current.first.size ()) == 0);
		assert (!result || (current.first.size () == other_a->current.first.size ()));
		assert (!result || (current.second.data () == other_a->current.second.data ()));
		assert (!result || (current.second.size () == other_a->current.second.size ()));
		return result;
	}

	bool is_end_sentinal () const override
	{
		return current.first.size () == 0;
	}

	void fill (std::pair<T, U> & value_a) const override
	{
		{
			if (current.first.size () != 0)
			{
				value_a.first = static_cast<T> (current.first);
			}
			else
			{
				value_a.first = T ();
			}
			if (current.second.size () != 0)
			{
				value_a.second = static_cast<U> (current.second);
			}
			else
			{
				value_a.second = U ();
			}
		}
	}
	void clear ()
	{
		current.first = badem::rocksdb_val{};
		current.second = badem::rocksdb_val{};
		assert (is_end_sentinal ());
	}
	badem::rocksdb_iterator<T, U> & operator= (badem::rocksdb_iterator<T, U> && other_a)
	{
		cursor = std::move (other_a.cursor);
		current = other_a.current;
		return *this;
	}
	badem::store_iterator_impl<T, U> & operator= (badem::store_iterator_impl<T, U> const &) = delete;

	std::unique_ptr<rocksdb::Iterator> cursor;
	std::pair<badem::rocksdb_val, badem::rocksdb_val> current;

private:
	rocksdb::Transaction * tx (badem::transaction const & transaction_a) const
	{
		return static_cast<rocksdb::Transaction *> (transaction_a.get_handle ());
	}
};
}
