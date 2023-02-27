#pragma once

#include <array>

namespace data_structure
{
    template<typename T, size_t N>
    struct container_array
    {
        using value_type = T;

        value_type* begin() { return buffer.begin(); }
        value_type* end() { return buffer.end(); }

        std::array<T, N> buffer;
    };

    template<typename T>
    struct container_range
    {
        using value_type = T;

        value_type* begin() const { return begin_addr; }
        value_type* end() const { return end_addr; }

        container_range() = default;
        container_range(value_type* begin_addr, value_type* end_addr)
            : begin_addr(begin_addr)
            , end_addr(end_addr)
        {}

        value_type* begin_addr = nullptr;
        value_type* end_addr = nullptr;
    };

    template<typename Cntr> class circular_buffer
    {
    public:
        using value_type = typename Cntr::value_type;
        using pointer_type = value_type*;
        using const_pointer_type = const value_type*;

        template<typename ...Args>
        circular_buffer(Args&& ...args)
            : m_container(args...)
        {
            m_end_addr = m_container.end();
        }

        pointer_type begin() { return m_container.begin(); }
        pointer_type end() { return m_end_addr; }
        const_pointer_type begin() const { return const_cast<circular_buffer*>(this)->begin(); }
        const_pointer_type end() const { return const_cast<circular_buffer*>(this)->end(); }

        size_t distance(const_pointer_type limit, const_pointer_type start) const
        {
            dbg_assert(start >= begin() && start <= end());
            dbg_assert(limit >= begin() && limit <= end());

            if(start > limit)
                return (end() - start) + (limit - begin());
            else
                return limit - start;
        }

        size_t distance(const_pointer_type start) const
        {
            dbg_assert(start >= begin() && start <= end());
            return end() - start;
        }
        
        size_t capacity() const 
        {
             return m_container.end() - m_container().begin(); 
        }

        size_t size() const 
        {
             return end() - begin(); 
        }

        void resize(size_t count)
        {
            m_end_addr = m_container.begin() + count;
            dbg_assert(m_end_addr <= m_container.end());
        }

        pointer_type copy_to(const_pointer_type limit, const_pointer_type start, pointer_type dest, size_t count) const
        {
            dbg_assert(start >= begin() && start <= end());
            dbg_assert(limit >= begin() && limit <= end());

            count = std::min(count, distance(limit, start));
            return copy_to(start, dest, count);
        }

        pointer_type copy_to(const_pointer_type start, pointer_type dest, size_t count) const
        {
            dbg_assert(start >= begin() && start <= end());

            while(count > 0)
            {
                const auto sz = std::min<size_t>(end() - start, count);
                dest = std::copy(start, start + sz, dest);
                start += sz;
                if(start == end())
                    start = begin();
                count -= sz;
            }

            return const_cast<pointer_type>(start);
        }
        
        pointer_type write(const_pointer_type start, const_pointer_type src, size_t count)
        {
            dbg_assert(start >= begin() && start <= end());

            while(count)
            {
                const auto n = std::min<size_t>(end() - start, count);
                start = std::copy(src, src + n, start);
                if(start == end())
                    start = begin();
                count -= n;
            }

            return const_cast<pointer_type>(start);
        }

        pointer_type advance(const_pointer_type limit, const_pointer_type start, size_t count) const
        {
            dbg_assert(start >= begin() && start <= end());
            dbg_assert(limit >= begin() && limit <= end());

            auto result = start + count;
            if(start > limit)
            {
                if(result >= end())
                {
                    result = begin() + (result - end());
                    if(result > limit)
                        result = limit;
                }
            }
            else
            {
                if(result > limit)
                    result = limit;
            }

            return const_cast<pointer_type>(result);
        }

        pointer_type advance(const_pointer_type start, size_t count) const
        {
            dbg_assert(start >= begin() && start <= end());

            start += count;
            while(start >= end())
                start = begin() + (start - end());
            return const_cast<pointer_type>(start);
        }

        template<typename Fn>
        pointer_type apply_linear(const_pointer_type limit, const_pointer_type start, Fn&& fn)
        {
            dbg_assert(start >= begin() && start <= end());
            dbg_assert(limit >= begin() && limit <= end());

            size_t count;
            if(start > limit)
            {
                count = fn(start, end());
                dbg_assert(count <= (end() - start));
                if(start + count < end())
                   return const_cast<pointer_type>(start + count);
                start = begin();
            }
            count = fn(start, limit);
            dbg_assert(count <= (limit - start));
            return advance(limit, start, count);
        }

    private:
        Cntr    m_container;
        pointer_type m_end_addr = nullptr;
    };
}