#ifndef SCROLLER_LIST_H
#define SCROLLER_LIST_H

#include <algorithm>
#include <cstddef>
#include <utility>

template<typename T> class List;

template<typename T>
class ListNode {
public:
    ListNode() = default;
    explicit ListNode(T data) : m_data(std::move(data)) {}
    ~ListNode() = default;

    T &data() { return m_data; }
    const T &data() const { return m_data; }
    ListNode *next() const { return m_next; }
    ListNode *prev() const { return m_prev; }

private:
    friend class List<T>;
    ListNode *m_prev = nullptr;
    ListNode *m_next = nullptr;
    T m_data{};
};

template<typename T>
class List {
public:
    List() = default;
    // Prevent accidental copies which would cause double-free
    List(const List&) = delete;
    List& operator=(const List&) = delete;
    List(List&& other) noexcept
        : m_size(std::exchange(other.m_size, 0)),
          m_first(std::exchange(other.m_first, nullptr)),
          m_last(std::exchange(other.m_last, nullptr)) {}
    List& operator=(List&& other) noexcept {
        if (this == &other)
            return *this;
        clear();
        m_size = std::exchange(other.m_size, 0);
        m_first = std::exchange(other.m_first, nullptr);
        m_last = std::exchange(other.m_last, nullptr);
        return *this;
    }
    ~List() {
        clear();
    }
    void clear() {
        auto node = m_last;
        while (node) {
            auto prev = node->prev();
            delete node;
            node = prev;
        }
        m_size = 0;
        m_first = m_last = nullptr;
    }

    ListNode<T> *first() const { return m_first; }
    ListNode<T> *last() const { return m_last; }

    size_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }

    void push_back(T value) {
        emplace_after(m_last, std::move(value));
    }
    void push_front(T value) {
        emplace_before(m_first, std::move(value));
    }

    void pop_back() {
        if (m_size > 0)
            erase(m_last);
    }
    void pop_front() {
        if (m_size > 0)
            erase(m_first);
    }

    void insert_before(ListNode<T> *it, T value) {
        emplace_before(it, std::move(value));
    }
    void insert_after(ListNode<T> *it, T value) {
        emplace_after(it, std::move(value));
    }

    ListNode<T> *emplace_after(ListNode<T> *it, T value) {
        // A null insertion point means append. This also handles the empty list.
        if (!it && !empty())
            it = m_last;
        auto next = new ListNode<T>(std::move(value));
        if (it == m_last) {
            m_last = next;
            // check here to see if size is stil zero
            if (m_size == 0)
                m_first = next;
        }
        if (it) {
            next->m_next = it->m_next;
            next->m_prev = it;
            it->m_next = next;
            if (next->m_next != nullptr) {
                next->m_next->m_prev = next;
            }
        }
        ++m_size;
        return next;
    }

    ListNode<T> *emplace_before(ListNode<T> *it, T value) {
        // A null insertion point means prepend. This also handles the empty list.
        if (!it && !empty())
            it = m_first;
        auto prev = new ListNode<T>(std::move(value));
        if (it == m_first) {
            m_first = prev;
            // check here to see if size is stil zero
            if (m_size == 0)
                m_last = prev;
        }
        if (it) {
            prev->m_next = it;
            prev->m_prev = it->m_prev;
            it->m_prev = prev;
            if (prev->m_prev != nullptr) {
                prev->m_prev->m_next = prev;
            }
        }
        ++m_size;
        return prev;
    }

    void erase(ListNode<T> *it) {
        if (!it) {
            return;
        }
        if (it->m_prev != nullptr) {
            it->m_prev->m_next = it->m_next;
        } else {
            m_first = it->m_next;
        }
        if(it->m_next != nullptr) {
            it->m_next->m_prev = it->m_prev;
        } else {
            m_last = it->m_prev;
        }
        delete it;
        --m_size;
    }

    // swaps the contents and the iterators
    void swap(ListNode<T> *&it1, ListNode<T> *&it2) {
        if (!it1 || !it2 || it1 == it2)
            return;
        std::swap(it1->m_data, it2->m_data);
        std::swap(it1, it2);
    }

    void move_before(ListNode<T> *dst, ListNode<T> *src) {
        if (src == dst || src == nullptr || dst == nullptr)
            return;
        if (src->m_prev != nullptr) {
            src->m_prev->m_next = src->m_next;
        } else {
            m_first = src->m_next;
        }
        if (src->m_next != nullptr) {
            src->m_next->m_prev = src->m_prev;
        } else {
            m_last = src->m_prev;
        }
        src->m_prev = dst->m_prev;
        if (dst->m_prev != nullptr) {
            dst->m_prev->m_next = src;
        } else {
            m_first = src;
        }
        src->m_next = dst;
        dst->m_prev = src;
    }

    void move_after(ListNode<T> *dst, ListNode<T> *src) {
        if (src == dst || src == nullptr || dst == nullptr)
            return;
        if (src->m_prev != nullptr) {
            src->m_prev->m_next = src->m_next;
        } else {
            m_first = src->m_next;
        }
        if (src->m_next != nullptr) {
            src->m_next->m_prev = src->m_prev;
        } else {
            m_last = src->m_prev;
        }
        src->m_prev = dst;
        if (dst->m_next != nullptr) {
            dst->m_next->m_prev = src;
        } else {
            m_last = src;
        }
        src->m_next = dst->m_next;
        dst->m_next = src;
    }

private:
    size_t m_size = 0;
    ListNode<T> *m_first = nullptr;
    ListNode<T> *m_last = nullptr;
};

#endif  // SCROLLER_LIST_H
