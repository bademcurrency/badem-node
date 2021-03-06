#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace badem
{
/** Distinct areas write locking is done, order is irrelevant */
enum class writer
{
	confirmation_height,
	process_batch,
	testing // Used in tests to emulate a write lock
};

class write_guard final
{
public:
	write_guard (badem::condition_variable & cv_a, std::function<void()> guard_finish_callback_a);
	~write_guard ();

private:
	badem::condition_variable & cv;
	std::function<void()> guard_finish_callback;
};

class write_database_queue final
{
public:
	write_database_queue ();
	/** Blocks until we are at the head of the queue */
	write_guard wait (badem::writer writer);

	/** Returns true if this writer is now at the front of the queue */
	bool process (badem::writer writer);

	/** Returns true if this writer is anywhere in the queue */
	bool contains (badem::writer writer);

	/** Doesn't actually pop anything until the returned write_guard is out of scope */
	write_guard pop ();

	/** This will release anything which is being blocked by the wait function */
	void stop ();

private:
	std::deque<badem::writer> queue;
	std::mutex mutex;
	badem::condition_variable cv;
	std::function<void()> guard_finish_callback;
	std::atomic<bool> stopped{ false };
};
}
