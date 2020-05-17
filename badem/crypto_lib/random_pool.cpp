#include <badem/crypto_lib/random_pool.hpp>

std::mutex badem::random_pool::mutex;
CryptoPP::AutoSeededRandomPool badem::random_pool::pool;

void badem::random_pool::generate_block (unsigned char * output, size_t size)
{
	std::lock_guard<std::mutex> guard (mutex);
	pool.GenerateBlock (output, size);
}

unsigned badem::random_pool::generate_word32 (unsigned min, unsigned max)
{
	std::lock_guard<std::mutex> guard (mutex);
	return pool.GenerateWord32 (min, max);
}

unsigned char badem::random_pool::generate_byte ()
{
	std::lock_guard<std::mutex> guard (mutex);
	return pool.GenerateByte ();
}
