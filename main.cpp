#pragma once

#include <array>
#include <iostream>
#include <cassert>
#include <random>
#include <numeric>
#include <functional>

#include "CircularBuffer.h"

// Generates a lookup table for the checksums of all 8-bit values.
std::array<std::uint_fast32_t, 256> generate_crc_lookup_table() noexcept
{
	auto const reversed_polynomial = std::uint_fast32_t{0xEDB88320uL};

	// This is a function object that calculates the checksum for a value,
	// then increments the value, starting from zero.
	struct byte_checksum
	{
		std::uint_fast32_t operator()() noexcept
		{
			auto checksum = static_cast<std::uint_fast32_t>(n++);

			for (auto i = 0; i < 8; ++i)
				checksum = (checksum >> 1) ^ ((checksum & 0x1u) ? reversed_polynomial : 0);

			return checksum;
		}

		unsigned n = 0;
	};

	auto table = std::array<std::uint_fast32_t, 256>{};
	std::generate(table.begin(), table.end(), byte_checksum{});

	return table;
}

// Calculates the CRC for any sequence of values. (You could use type traits and
// a static assert to ensure the values can be converted to 8 bits.)
template <typename InputIterator>
std::uint_fast32_t crc(InputIterator first, InputIterator last)
{
	// Generate lookup table only on first use then cache it - this is thread-safe.
	static auto const table = generate_crc_lookup_table();

	// Calculate the checksum - make sure to clip to 32 bits, for systems that
	// don't have a true (fast) 32-bit type.
	return std::uint_fast32_t{0xFFFFFFFFuL} &
		   ~std::accumulate(
			   first, last, ~std::uint_fast32_t{0} & std::uint_fast32_t{0xFFFFFFFFuL}, [](std::uint_fast32_t checksum, std::uint_fast8_t value) {
				   return table[(checksum ^ value) & 0xFFu] ^ (checksum >> 8);
			   });
}

template <typename... Args>
double time_run(std::function<void(CircularBuffer<uint8_t>&, std::size_t, int)> f, Args... args)
{
	auto start = std::chrono::steady_clock::now();
	f(std::forward<Args>(args)...);
	auto diff = std::chrono::steady_clock::now() - start;
	return std::chrono::duration<double, std::milli>(diff).count();
}

double run_circular_buffer_test(CircularBuffer<uint8_t>& cb, std::size_t max_data_size, int iterations = -1)
{
	double total_time{0.0};

	std::random_device rd;
	std::mt19937 rd_mt(rd());

	// generate a random buffer size between 1 and add_max*2
	std::uniform_int_distribution<> add(1, static_cast<int>(max_data_size) * 2);
	// generate random byte values for initializing the adding_buffer
	std::uniform_int_distribution<> byte(0, 255);

	// input/output buffers (can't use std::array here because
	// it's size is fixed at compile time)

	std::vector<uint8_t> adding_buffer;
	adding_buffer.reserve(max_data_size * 2);
	std::vector<uint8_t> extraction_buffer;
	extraction_buffer.reserve(max_data_size * 2);

	while (iterations)
	{
		// send the data through the CircularBuffer
		if (add(rd_mt) < (max_data_size / 2))
		{
			// initialize a random chunk of the buffer
			auto amount = add(rd_mt);
			for (auto i = 0; i < amount; ++i)
			{
				auto b = byte(rd_mt);
				adding_buffer[i] = static_cast<uint8_t>(b);
			}

			// calculate the CRC of the initialized buffer chunk
			auto i_start = std::begin(adding_buffer);
			auto i_end = std::begin(adding_buffer);
			for (auto i = 0; i < amount; ++i)
				++i_end;
			std::size_t h1 = ::crc(i_start, i_end);

			auto start = std::chrono::steady_clock::now();

			// write the size of the random buffer data
			cb.insert_units(reinterpret_cast<uint8_t*>(&amount), sizeof(amount));
			// write the random buffer data
			cb.insert_units(adding_buffer.data(), amount);
			// write its CRC value
			cb.insert_units(reinterpret_cast<uint8_t*>(&h1), sizeof(std::size_t));

			auto diff = std::chrono::steady_clock::now() - start;
			total_time += std::chrono::duration<double, std::milli>(diff).count();
		}

		// receive the data from the CircularBuffer
		if (cb.used_space() >= sizeof(int))
		{
			int amount{0};
			std::size_t h1{0};

			auto start = std::chrono::steady_clock::now();

			cb.extract_units(reinterpret_cast<uint8_t*>(&amount), sizeof(int));
			cb.extract_units(extraction_buffer.data(), amount);
			cb.extract_units(reinterpret_cast<uint8_t*>(&h1), sizeof(std::size_t));

			auto diff = std::chrono::steady_clock::now() - start;
			total_time += std::chrono::duration<double, std::milli>(diff).count();

			// calculate the CRC of the extracted buffer
			auto i_start = std::begin(extraction_buffer);
			auto i_end = std::begin(extraction_buffer);
			for (auto i = 0; i < amount; ++i)
				++i_end;
			std::size_t h2 = ::crc(i_start, i_end);

			assert(h1 == h2); // make sure data is extracted accurately
		}

		if (iterations != -1)
			--iterations;
	}

	return total_time;
}

int main(int argc, char* argv[])
{
	// allocate twice our expected storage size
	auto max = 75000;
	auto cb_units = 500000;
	auto cb = CircularBuffer<uint8_t>(cb_units);
	auto millis = run_circular_buffer_test(cb, max, 50000);
	std::cout << millis << " ms" << std::endl;
}
