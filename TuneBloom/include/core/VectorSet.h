#pragma once

#include <vector>
#include <unordered_set>

template <typename T, typename THasher = std::hash<T>, typename TKeyEq = std::equal_to<T>>
class VectorSet
{
public:
    using iterator                     = typename std::vector<T>::iterator;
    using const_iterator               = typename std::vector<T>::const_iterator;
    iterator begin()                   { return theVector.begin(); }
    iterator end()                     { return theVector.end(); }
    const_iterator begin() const       { return theVector.begin(); }
    const_iterator end() const         { return theVector.end(); }
    const T& front() const             { return theVector.front(); }
    const T& back() const              { return theVector.back(); }
    void insert(const T& item)         { if (theSet.insert(item).second) theVector.push_back(item); }
    size_t count(const T& item) const  { return theSet.count(item); }
    bool empty() const                 { return theSet.empty(); }
    size_t size() const                { return theSet.size(); }
    bool contains(const T& item) const { return theSet.contains(item); }

    void insert(const iterator& it, const T& item)
    {
        if (theSet.insert(item).second)
        {
            theVector.insert(it, item);
        }
    }

    void resize(u32 newSize)
    {
        const size_t oldSize = size();
        SEAD_ASSERT(newSize <= oldSize);

        if (newSize == oldSize)
        {
            return;
        }

        for (u32 i = newSize; i < oldSize; i++)
        {
            theSet.erase(theVector[i]);
        }

        theVector.resize(newSize);
    }

    void reserve(u32 size)
    {
        theSet.reserve(size);
        theVector.reserve(size);
    }

    bool operator==(const std::vector<T>& rhs) const
    {
        return theVector == rhs;
    }

    const T& operator[](u32 idx) const
    {
        return theVector[idx];
    }

    T& operator[](u32 idx)
    {
        return theVector[idx];
    }

private:
    std::vector<T> theVector;
    std::unordered_set<T, THasher, TKeyEq> theSet;
};
