#include <badem/lib/memory.hpp>
#include <badem/secure/common.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
/** This allocator records the size of all allocations that happen */
template <class T>
class record_allocations_new_delete_allocator
{
public:
	using value_type = T;

	explicit record_allocations_new_delete_allocator (std::vector<size_t> * allocated) :
	allocated (allocated)
	{
	}

	template <typename U>
	record_allocations_new_delete_allocator (const record_allocations_new_delete_allocator<U> & a)
	{
		allocated = a.allocated;
	}

	template <typename U>
	record_allocations_new_delete_allocator & operator= (const record_allocations_new_delete_allocator<U> &) = delete;

	T * allocate (size_t num_to_allocate)
	{
		auto size_allocated = (sizeof (T) * num_to_allocate);
		allocated->push_back (size_allocated);
		return static_cast<T *> (::operator new (size_allocated));
	}

	void deallocate (T * p, size_t num_to_deallocate)
	{
		::operator delete (p);
	}

	std::vector<size_t> * allocated;
};

template <typename T>
size_t get_allocated_size ()
{
	std::vector<size_t> allocated;
	record_allocations_new_delete_allocator<T> alloc (&allocated);
	std::allocate_shared<T, record_allocations_new_delete_allocator<T>> (alloc);
	assert (allocated.size () == 1);
	return allocated.front ();
}
}

TEST (memory_pool, validate_cleanup)
{
	// This might be turned off, e.g on Mac for instance, so don't do this test
	if (!badem::get_use_memory_pools ())
	{
		return;
	}

	badem::make_shared<badem::open_block> ();
	badem::make_shared<badem::receive_block> ();
	badem::make_shared<badem::send_block> ();
	badem::make_shared<badem::change_block> ();
	badem::make_shared<badem::state_block> ();
	badem::make_shared<badem::vote> ();

	ASSERT_TRUE (badem::purge_singleton_pool_memory<badem::open_block> ());
	ASSERT_TRUE (badem::purge_singleton_pool_memory<badem::receive_block> ());
	ASSERT_TRUE (badem::purge_singleton_pool_memory<badem::send_block> ());
	ASSERT_TRUE (badem::purge_singleton_pool_memory<badem::state_block> ());
	ASSERT_TRUE (badem::purge_singleton_pool_memory<badem::vote> ());

	// Change blocks have the same size as open_block so won't deallocate any memory
	ASSERT_FALSE (badem::purge_singleton_pool_memory<badem::change_block> ());

	ASSERT_EQ (badem::determine_shared_ptr_pool_size<badem::open_block> (), get_allocated_size<badem::open_block> () - sizeof (size_t));
	ASSERT_EQ (badem::determine_shared_ptr_pool_size<badem::receive_block> (), get_allocated_size<badem::receive_block> () - sizeof (size_t));
	ASSERT_EQ (badem::determine_shared_ptr_pool_size<badem::send_block> (), get_allocated_size<badem::send_block> () - sizeof (size_t));
	ASSERT_EQ (badem::determine_shared_ptr_pool_size<badem::change_block> (), get_allocated_size<badem::change_block> () - sizeof (size_t));
	ASSERT_EQ (badem::determine_shared_ptr_pool_size<badem::state_block> (), get_allocated_size<badem::state_block> () - sizeof (size_t));
	ASSERT_EQ (badem::determine_shared_ptr_pool_size<badem::vote> (), get_allocated_size<badem::vote> () - sizeof (size_t));
}
