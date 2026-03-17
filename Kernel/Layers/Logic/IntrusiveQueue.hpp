/**
 * File: IntrusiveQueue.hpp
 * Author: Marwan Mostafa
 * Description: Intrusive queue container template.
 */

#pragma once

template <typename T, T* T::* NextMember> class IntrusiveQueue
{
private:
    T* HeadNode;
    T* TailNode;

    static T*& NextOf(T* Node)
    {
        return Node->*NextMember;
    }

public:
    IntrusiveQueue() : HeadNode(nullptr), TailNode(nullptr)
    {
    }

    bool IsEmpty() const
    {
        return HeadNode == nullptr;
    }

    T* Head() const
    {
        return HeadNode;
    }

    T* Next(const T* Node) const
    {
        if (Node == nullptr)
        {
            return nullptr;
        }

        return Node->*NextMember;
    }

    void PushBack(T* Node)
    {
        if (Node == nullptr)
        {
            return;
        }

        NextOf(Node) = nullptr;

        if (TailNode == nullptr)
        {
            HeadNode = Node;
            TailNode = Node;
            return;
        }

        NextOf(TailNode) = Node;
        TailNode         = Node;
    }

    T* PopFront()
    {
        if (HeadNode == nullptr)
        {
            return nullptr;
        }

        T* Node  = HeadNode;
        HeadNode = NextOf(Node);

        if (HeadNode == nullptr)
        {
            TailNode = nullptr;
        }

        NextOf(Node) = nullptr;
        return Node;
    }

    bool Remove(T* Node)
    {
        if (Node == nullptr || HeadNode == nullptr)
        {
            return false;
        }

        if (HeadNode == Node)
        {
            PopFront();
            return true;
        }

        T* Previous = HeadNode;
        while (Previous != nullptr && NextOf(Previous) != Node)
        {
            Previous = NextOf(Previous);
        }

        if (Previous == nullptr)
        {
            return false;
        }

        NextOf(Previous) = NextOf(Node);
        if (TailNode == Node)
        {
            TailNode = Previous;
        }

        NextOf(Node) = nullptr;
        return true;
    }

    template <typename ValueType> T* FindFirst(ValueType T::* Member, ValueType Value) const
    {
        T* Node = HeadNode;
        while (Node != nullptr)
        {
            if (Node->*Member == Value)
            {
                return Node;
            }

            Node = NextOf(Node);
        }

        return nullptr;
    }

    void RotateFrontToBack()
    {
        if (HeadNode == nullptr || HeadNode == TailNode)
        {
            return;
        }

        T* OldHead = PopFront();
        PushBack(OldHead);
    }

    void ClearAndDelete()
    {
        while (!IsEmpty())
        {
            T* Node = PopFront();
            delete Node;
        }
    }
};