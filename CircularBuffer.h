#pragma once

/// @file CircularBuffer.h
/// Contains a utility class that implements a circular buffer.
///
/// @author Bob Hood

#include <mutex>

/// @class CircularBuffer
/// @brief Implementation of a circular buffer
///
/// Provides a highly optimized, parameterized manager of data in
/// a circular buffer.
///
/// -------------------------------------------------------
/// An insertion/extraction test was performed using 50,000
/// iterations of randomized amounts, ranging from 1 to
/// 75,000 units, with each inserted amount being CRC validated
/// when extracted.  The class was specialized with uint8_t data
/// units, with only the insertion (insert_units()) and extractions
/// (extract_units()) calls being timed. All other overhead
/// (e.g., calculating CRC values for validation) were
/// excluded.  Three consecutive runs were completed for each
/// level, and their total execution times averaged.
///
/// Original baseline test: ~3475.87ms
/// Data mapping-optimized insertions: ~1784.356ms (49% execution time decrease over original)
/// Data mapping-optimized extractions: ~40.332ms (98% execution time decrease over original)

template <typename Unit>
class CircularBuffer
{
public:
	CircularBuffer(int unit_count = 0) : m_unit_size(sizeof(Unit)), m_unit_count(unit_count)
	{
		if (unit_count)
			m_buffer = BufferPtr(new Unit[unit_count]);
		reset();
	}

	/*!
	Copy semantics for the class will transfer any existing data from the source
	buffer as long as it does not exceed our m_unit_count size.

	\note This action will destroy any existing data in the buffer.
	*/
	CircularBuffer(const CircularBuffer& source) { transfer(source); }
	CircularBuffer& operator=(const CircularBuffer& source)
	{
		transfer(source);
		return *this;
	}

	// enable default move semantics
	CircularBuffer(CircularBuffer&&) = default;
	CircularBuffer& operator=(CircularBuffer&&) = default;

	/*!
	Reset the circular buffer's head and tail pointers to their starting positions.
	*/
	void reset() noexcept
	{
		m_buffer_head = 0;
		m_buffer_tail = 0;
	}

	/*!
	Insert data units into the circular buffer.

	\param data A pointer to the buffer holding an array of one or more units to place.
	\param unit_count The number of units from the data buffer to place.
	\return A Boolean that indicates the data was successfully inserted.  A false return means the data would not fit.
	*/
	bool insert_units(const Unit* data, int unit_count) noexcept
	{
		if (!m_unit_count)
			return false;

		BufferLock lock(m_buffer_lock);

		auto fs = static_cast<int>(m_unit_count - m_used_slots);
		if (fs < unit_count)
			return false;

		auto p = get_buffer_head();

#if 1
		// data-mapping insertion code
		if (m_buffer_head < m_buffer_tail)
		{
			// how much space is available from head to the tail?
			auto left_in_buffer = m_buffer_tail - m_buffer_head;
			if (left_in_buffer < unit_count)
				return false; // not enough; we've collided with the tail
			::memcpy(p, data, unit_count * m_unit_size);
			m_used_slots += unit_count;
			m_buffer_head += unit_count;
		}
		else
		{
			// how much space is available from head to the end of the buffer...or the tail?
			auto left_in_buffer = m_unit_count - m_buffer_head;
			if (left_in_buffer >= unit_count)
			{
				::memcpy(p, data, unit_count * m_unit_size);
				m_used_slots += unit_count;
				m_buffer_head += unit_count;
			}
			else
			{
				if ((left_in_buffer + (m_buffer_tail - 1)) < unit_count)
					return false; // won't fit; we've collided with the tail

				::memcpy(p, data, left_in_buffer * m_unit_size);
				m_used_slots += left_in_buffer;
				unit_count -= left_in_buffer;
				data += left_in_buffer;

				m_buffer_head = 0;
				p = get_buffer_head();

				::memcpy(p, data, unit_count * m_unit_size);
				m_used_slots += unit_count;
				m_buffer_head += unit_count;
			}
		}
#else
		// original baseline insertion code
		for (auto i = 0; i < unit_count; ++i)
		{
			*p++ = *data++;
			++m_used_slots;
			if (++m_buffer_head == m_unit_count)
			{
				m_buffer_head = 0; // wrap around
				p = get_buffer_head();
			}
		}
#endif
		return true;
	}

	/*!
	Extract data units from the circular buffer.

	\param data A pointer to the buffer to receive the data units extracted.
	\param unit_count The number of units to extract from the circular buffer.
	\return A Boolean that indicates the data was successfully extracted.  A false return means there weren't enough data units available to satisfy the request.
	*/
	bool extract_units(Unit* data, int unit_count) noexcept
	{
		if (!m_unit_count)
			return false;

		BufferLock lock(m_buffer_lock);
		if (m_used_slots < unit_count)
			return false;

		auto p = get_buffer_tail();

#if 1
		// data-mapping extraction code
		if (m_buffer_head < m_buffer_tail)
		{
			// how much data is available from here to the end of the buffer?
			auto data_count = m_unit_count - m_buffer_tail;
			if (data_count < unit_count)
			{
				::memcpy(data, p, data_count * m_unit_size);
				data += data_count;
				unit_count -= data_count;
				m_used_slots -= data_count;

				m_buffer_tail = 0;
				auto p = get_buffer_tail();
				::memcpy(data, p, unit_count * m_unit_size);
				m_used_slots -= unit_count;
				m_buffer_tail += unit_count;
			}
			else
			{
				::memcpy(data, p, unit_count * m_unit_size);
				m_used_slots -= unit_count;
				m_buffer_tail += unit_count;
			}
		}
		else
		{
			// how much data is available from here to the end of the buffer?
			auto data_count = m_buffer_head - m_buffer_tail;
			if (data_count < unit_count)
				return false; // shouldn't happen here

			::memcpy(data, p, unit_count * m_unit_size);
			m_used_slots -= unit_count;
			m_buffer_tail += unit_count;
		}
#else
		// original baseline extraction code
		for (auto i = 0; i < unit_count; ++i)
		{
			*data++ = *p++;
			--m_used_slots;
			if (++m_buffer_tail == m_unit_count)
			{
				m_buffer_tail = 0; // wrap around
				p = get_buffer_tail();
			}
		}
#endif
		return true;
	}

	/*!
	Reports how many data units are currently being held by the circular buffer.

	\return An integer count of the number of data units.
	*/
	int used_space() noexcept
	{
		BufferLock lock(m_buffer_lock);
		return m_used_slots;
	}

	/*!
	Reports how many empty data unit slots are currently available in the circular buffer.
	This is simply the inverse of the used space.

	\return An integer count of the number of unused data units available.
	*/
	int free_space() noexcept
	{
		BufferLock lock(m_buffer_lock);
		return static_cast<int>(m_unit_count - m_used_slots);
	}

protected: // aliases and enums
	using BufferPtr = std::unique_ptr<Unit>;
	using BufferLock = std::unique_lock<std::mutex>;

protected: // methods
	Unit* get_buffer_head()
	{
		auto offset = m_buffer_head * m_unit_size;
		Unit* p = m_buffer.get() + offset;
		return p;
	}

	Unit* get_buffer_tail()
	{
		auto offset = m_buffer_tail * m_unit_size;
		Unit* p = m_buffer.get() + offset;
		return p;
	}

	void transfer(const CircularBuffer& source)
	{
		if (source.m_buffer_head != source.m_buffer_tail)
		{
			// in order to be able to transfer successfully, the source
			// buffer size must be smaller-than-or-equal-to ours

			assert(source.m_unit_size == m_unit_size || source.m_unit_count <= m_unit_count);

			if (source.m_buffer_head > source.m_buffer_tail)
			{
				// the easy way (linear copy)
				auto tail_offset = source.m_buffer_tail * source.m_unit_size;
				::memcpy(m_buffer.get(), source.m_buffer.get() + tail_offset, source.m_used_slots * source.m_unit_size);
			}
			else
			{
				// a little trickier (double copy)
				Unit* p = m_buffer.get();
				auto tail_offset = source.m_buffer_tail * source.m_unit_size;
				auto unit_count = source.m_unit_count - source.m_buffer_tail;
				::memcpy(p, source.m_buffer.get() + tail_offset, unit_count * source.m_unit_size);
				p += unit_count;
				::memcpy(p, source.m_buffer.get(), source.m_buffer_head * source.m_unit_size);
			}

			m_buffer_tail = 0;
			m_used_slots = m_buffer_head = source.m_used_slots;
		}
	}

protected: // data members
	int m_unit_size{0};
	int m_unit_count{0};
	int m_buffer_head{0};
	int m_buffer_tail{0};

	BufferPtr m_buffer;
	std::mutex m_buffer_lock;

	// tracks the number of data units currently in use
	int m_used_slots{0};
};
