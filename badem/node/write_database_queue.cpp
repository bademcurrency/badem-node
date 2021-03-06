#include <badem/lib/utility.hpp>
#include <badem/node/write_database_queue.hpp>

#include <algorithm>

badem::write_guard::write_guard (badem::condition_variable & cv_a, std::function<void()> guard_finish_callback_a) :
cv (cv_a),
guard_finish_callback (guard_finish_callback_a)
{
}

badem::write_guard::~write_guard ()
{
	guard_finish_callback ();
	cv.notify_all ();
}

badem::write_database_queue::write_database_queue () :
// clang-format off
guard_finish_callback ([&queue = queue, &mutex = mutex]() {
	badem::lock_guard<std::mutex> guard (mutex);
	queue.pop_front ();
})
// clang-format on
{
}

badem::write_guard badem::write_database_queue::wait (badem::writer writer)
{
	badem::unique_lock<std::mutex> lk (mutex);
	// Add writer to the end of the queue if it's not already waiting
	auto exists = std::find (queue.cbegin (), queue.cend (), writer) != queue.cend ();
	if (!exists)
	{
		queue.push_back (writer);
	}

	while (!stopped && queue.front () != writer)
	{
		cv.wait (lk);
	}

	return write_guard (cv, guard_finish_callback);
}

bool badem::write_database_queue::contains (badem::writer writer)
{
	badem::lock_guard<std::mutex> guard (mutex);
	return std::find (queue.cbegin (), queue.cend (), writer) != queue.cend ();
}

bool badem::write_database_queue::process (badem::writer writer)
{
	auto result = false;
	{
		badem::lock_guard<std::mutex> guard (mutex);
		// Add writer to the end of the queue if it's not already waiting
		auto exists = std::find (queue.cbegin (), queue.cend (), writer) != queue.cend ();
		if (!exists)
		{
			queue.push_back (writer);
		}

		result = (queue.front () == writer);
	}

	if (!result)
	{
		cv.notify_all ();
	}

	return result;
}

badem::write_guard badem::write_database_queue::pop ()
{
	return write_guard (cv, guard_finish_callback);
}

void badem::write_database_queue::stop ()
{
	stopped = true;
	cv.notify_all ();
}
