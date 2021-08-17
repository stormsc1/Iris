#pragma once

#include <mutex>
#include <deque>

namespace IRIS
{
	template<typename T>
	class ThreadSafeQueue
	{
	public:
		ThreadSafeQueue() = default;
		ThreadSafeQueue(const ThreadSafeQueue<T>&) = delete;
		virtual ~ThreadSafeQueue() { Clear(); }
	public:
		const T& Front()
		{
			std::scoped_lock lock(m_MutexQueue);
			return m_DequeQueue.front();
		}

		const T& Back()
		{
			std::scoped_lock lock(m_MutexQueue);
			return m_DequeQueue.back();
		}

		void PushFront(const T& item)
		{
			std::scoped_lock lock(m_MutexQueue);
			m_DequeQueue.emplace_front(std::move(item));
		}

		void PushBack(const T& item)
		{
			std::scoped_lock lock(m_MutexQueue);
			m_DequeQueue.emplace_back(std::move(item));
		}

		T PopFront()
		{
			std::scoped_lock lock(m_MutexQueue);
			auto t = std::move(m_DequeQueue.front());
			m_DequeQueue.pop_front();
			return t;
		}
		
		T PopBack()
		{
			std::scoped_lock lock(m_MutexQueue);
			auto t = std::move(m_DequeQueue.back());
			m_DequeQueue.pop_back();
			return t;
		}

		bool IsEmpty()
		{
			std::scoped_lock lock(m_MutexQueue);
			return m_DequeQueue.empty();
		}

		uint32_t GetCount()
		{
			std::scoped_lock lock(m_MutexQueue);
			return m_DequeQueue.size();
		}

		void Clear()
		{
			std::scoped_lock lock(m_MutexQueue);
			m_DequeQueue.clear();
		}

	protected:
		std::mutex m_MutexQueue;
		std::deque<T> m_DequeQueue;

	};
}