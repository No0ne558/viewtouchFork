/*
 * list_utility.hh - revision 6 (8/10/98)
 * linked list template classes
 *
 * SList - single linked list
 *   required fields for items: next // see note below
 *
 * DList - double linked list
 *   required fields for items: fore, next // see note below
 *
 *			YIKES!!  Explicit reference to members of target structure
 *			(fore and next) is a very unsafe departure from the
 *			template/abstraction approach and constrains these list
 *			mechanisms to types that contain these members.
 *
 *			This needs to be fixed.
 */

#ifndef VT_LIST_UTILITY_HH
#define VT_LIST_UTILITY_HH

#include "basic.hh"
#include "fntrace.hh"
#include "utility.hh"
#include <utility>
#include <memory>
#include <vector>
#include <algorithm>

/**** Types ****/
template <typename T>
class SList
{
    struct Node
    {
        std::unique_ptr<T> val;
        Node *next{nullptr};
        explicit Node(std::unique_ptr<T> v) : val(std::move(v)), next(nullptr) {}
    };

    Node *head{nullptr};
    Node *tail{nullptr};
    int cached_count{0};

    Node *FindNode(T *item) noexcept
    {
        for (Node *n = head; n != nullptr; n = n->next)
            if (n->val.get() == item)
                return n;
        return nullptr;
    }

public:
    SList() = default;
    explicit SList(T *item) : head(nullptr), tail(nullptr), cached_count(item ? 1 : 0)
    {
        if (item)
        {
            head = new Node(std::unique_ptr<T>(item));
            tail = head;
            head->val->next = nullptr;
        }
    }

    SList(const SList&) = delete;
    SList& operator=(const SList&) = delete;

    SList(SList&& other) noexcept : head(other.head), tail(other.tail), cached_count(other.cached_count)
    {
        other.head = nullptr;
        other.tail = nullptr;
        other.cached_count = 0;
    }

    SList& operator=(SList&& other) noexcept
    {
        if (this != &other)
        {
            Purge();
            head = other.head;
            tail = other.tail;
            cached_count = other.cached_count;
            other.head = nullptr;
            other.tail = nullptr;
            other.cached_count = 0;
        }
        return *this;
    }

    ~SList() { Purge(); }

    [[nodiscard]] T *Head() noexcept { return head ? head->val.get() : nullptr; }
    [[nodiscard]] T *Tail() noexcept { return tail ? tail->val.get() : nullptr; }
    [[nodiscard]] const T *Head() const noexcept { return head ? head->val.get() : nullptr; }
    [[nodiscard]] const T *Tail() const noexcept { return tail ? tail->val.get() : nullptr; }

    [[nodiscard]] bool IsEmpty() const noexcept { return head == nullptr; }

    int AddToHead(T *item) noexcept
    {
        FnTrace("SList::AddToHead()");
        if (item == nullptr)
            return 1;

        Node *n = new Node(std::unique_ptr<T>(item));
        T *old_head = head ? head->val.get() : nullptr;
        n->val->next = old_head;
        n->next = head;
        if (!tail)
            tail = n;
        head = n;
        ++cached_count;
        return 0;
    }

    // overload taking ownership via unique_ptr
    int AddToHead(std::unique_ptr<T> item) noexcept
    {
        FnTrace("SList::AddToHead(unique_ptr)");
        if (!item)
            return 1;

        Node *n = new Node(std::move(item));
        T *old_head = head ? head->val.get() : nullptr;
        n->val->next = old_head;
        n->next = head;
        if (!tail)
            tail = n;
        head = n;
        ++cached_count;
        return 0;
    }

    int AddToTail(T *item) noexcept
    {
        FnTrace("SList::AddToTail()");
        if (item == nullptr)
            return 1;

        Node *n = new Node(std::unique_ptr<T>(item));
        n->val->next = nullptr;
        n->next = nullptr;
        if (tail)
        {
            tail->val->next = n->val.get();
            tail->next = n;
            tail = n;
        }
        else
        {
            head = tail = n;
        }
        ++cached_count;
        return 0;
    }

    // overload taking ownership via unique_ptr
    int AddToTail(std::unique_ptr<T> item) noexcept
    {
        FnTrace("SList::AddToTail(unique_ptr)");
        if (!item)
            return 1;

        Node *n = new Node(std::move(item));
        n->val->next = nullptr;
        n->next = nullptr;
        if (tail)
        {
            tail->val->next = n->val.get();
            tail->next = n;
            tail = n;
        }
        else
        {
            head = tail = n;
        }
        ++cached_count;
        return 0;
    }

    int AddAfterNode(T *node, T *item) noexcept
    {
        FnTrace("SList::AddAfterNode()");
        if (node == nullptr)
            return AddToHead(item);
        if (node == (tail ? tail->val.get() : nullptr))
            return AddToTail(item);

        Node *nnode = FindNode(node);
        if (!nnode)
            return AddToHead(item);

        Node *newn = new Node(std::unique_ptr<T>(item));
        Node *nextn = nnode->next;
        newn->val->next = nextn ? nextn->val.get() : nullptr;
        newn->next = nextn;
        nnode->val->next = newn->val.get();
        nnode->next = newn;
        ++cached_count;
        return 0;
    }

    // overload taking ownership via unique_ptr
    int AddAfterNode(T *node, std::unique_ptr<T> item) noexcept
    {
        FnTrace("SList::AddAfterNode(unique_ptr)");
        if (!item)
            return 1;
        if (node == nullptr)
            return AddToHead(std::move(item));
        if (node == (tail ? tail->val.get() : nullptr))
            return AddToTail(std::move(item));

        Node *nnode = FindNode(node);
        if (!nnode)
            return AddToHead(std::move(item));

        Node *newn = new Node(std::move(item));
        Node *nextn = nnode->next;
        newn->val->next = nextn ? nextn->val.get() : nullptr;
        newn->next = nextn;
        nnode->val->next = newn->val.get();
        nnode->next = newn;
        ++cached_count;
        return 0;
    }

    void Purge() noexcept
    {
        FnTrace("SList::Purge()");
        while (head)
        {
            Node *tmp = head;
            head = head->next;
            delete tmp;
        }
        tail = nullptr;
        cached_count = 0;
    }

    int Remove(T *node) noexcept
    {
        FnTrace("SList::Remove()");
        if (node == nullptr)
            return 1;

        Node *prev = nullptr;
        Node *cur = head;
        while (cur)
        {
            if (cur->val.get() == node)
                break;
            prev = cur;
            cur = cur->next;
        }
        if (!cur)
            return 1;

        if (prev)
        {
            prev->next = cur->next;
            // update raw pointer in prev value
            prev->val->next = cur->next ? cur->next->val.get() : nullptr;
        }
        else
        {
            head = cur->next;
        }

        if (tail == cur)
            tail = prev;

        // Release ownership to caller (old behavior: caller owns pointer after Remove)
        cur->val.release();
        cur->next = nullptr;
        delete cur;
        --cached_count;
        return 0;
    }

    int RemoveAndDelete(T *item) noexcept
    {
        FnTrace("SList::RemoveAndDelete()");
        if (item == nullptr)
            return 1;
        Node *prev = nullptr;
        Node *cur = head;
        while (cur)
        {
            if (cur->val.get() == item)
                break;
            prev = cur;
            cur = cur->next;
        }
        if (!cur)
            return 1;

        if (prev)
        {
            prev->next = cur->next;
            prev->val->next = cur->next ? cur->next->val.get() : nullptr;
        }
        else
            head = cur->next;

        if (tail == cur)
            tail = prev;

        // Delete occurs when Node is destroyed (unique_ptr)
        delete cur;
        --cached_count;
        return 0;
    }

    // Return ownership of the removed node's value as a std::unique_ptr
    [[nodiscard]] std::unique_ptr<T> RemoveReturningUnique(T *node) noexcept
    {
        FnTrace("SList::RemoveReturningUnique()");
        if (node == nullptr)
            return nullptr;

        Node *prev = nullptr;
        Node *cur = head;
        while (cur)
        {
            if (cur->val.get() == node)
                break;
            prev = cur;
            cur = cur->next;
        }
        if (!cur)
            return nullptr;

        if (prev)
        {
            prev->next = cur->next;
            // update raw pointer in prev value
            prev->val->next = cur->next ? cur->next->val.get() : nullptr;
        }
        else
        {
            head = cur->next;
        }

        if (tail == cur)
            tail = prev;

        // Move ownership to caller
        std::unique_ptr<T> ret = std::move(cur->val);
        cur->next = nullptr;
        delete cur;
        --cached_count;
        return ret;
    }

    [[nodiscard]] int Count() const noexcept { return cached_count; }

    [[nodiscard]] T *Index(int i) noexcept
    {
        FnTrace("SList::Index()");
        if (i < 0)
            return nullptr;
        Node *n = head;
        while (n != nullptr && i > 0)
        {
            --i;
            n = n->next;
        }
        return n ? n->val.get() : nullptr;
    }

    [[nodiscard]] T *operator[](int i) noexcept { return Index(i); }
};

template <typename T>
class DList
{
    struct Node
    {
        std::unique_ptr<T> val;
        Node *next{nullptr};
        Node *fore{nullptr};
        explicit Node(std::unique_ptr<T> v) : val(std::move(v)), next(nullptr), fore(nullptr) {}
    };

    Node *head{nullptr};
    Node *tail{nullptr};
    int cached_count{0};

    Node *FindNode(T *item) noexcept
    {
        for (Node *n = head; n != nullptr; n = n->next)
            if (n->val.get() == item)
                return n;
        return nullptr;
    }

public:
    DList() = default;
    explicit DList(T *item) : head(nullptr), tail(nullptr), cached_count(item ? 1 : 0)
    {
        if (item)
        {
            head = new Node(std::unique_ptr<T>(item));
            tail = head;
            head->val->next = nullptr;
            head->val->fore = nullptr;
        }
    }

    DList(const DList&) = delete;
    DList& operator=(const DList&) = delete;

    DList(DList&& other) noexcept : head(other.head), tail(other.tail), cached_count(other.cached_count)
    {
        other.head = nullptr;
        other.tail = nullptr;
        other.cached_count = 0;
    }

    DList& operator=(DList&& other) noexcept
    {
        if (this != &other)
        {
            Purge();
            head = other.head;
            tail = other.tail;
            cached_count = other.cached_count;
            other.head = nullptr;
            other.tail = nullptr;
            other.cached_count = 0;
        }
        return *this;
    }

    ~DList() { Purge(); }

    [[nodiscard]] T *Head() noexcept { return head ? head->val.get() : nullptr; }
    [[nodiscard]] T *Tail() noexcept { return tail ? tail->val.get() : nullptr; }
    [[nodiscard]] const T *Head() const noexcept { return head ? head->val.get() : nullptr; }
    [[nodiscard]] const T *Tail() const noexcept { return tail ? tail->val.get() : nullptr; }

    [[nodiscard]] bool IsEmpty() const noexcept { return head == nullptr; }

    int AddToHead(T *item) noexcept
    {
        FnTrace("DList::AddToHead()");
        if (item == nullptr)
            return 1;

        Node *n = new Node(std::unique_ptr<T>(item));
        n->fore = nullptr;
        n->next = head;
        if (head)
            head->fore = n;
        else
            tail = n;
        head = n;
        // update raw pointers in value
        head->val->fore = nullptr;
        head->val->next = head->next ? head->next->val.get() : nullptr;
        if (head->next)
            head->next->val->fore = head->val.get();
        ++cached_count;
        return 0;
    }

    int AddToTail(T *item) noexcept
    {
        FnTrace("DList::AddToTail()");
        if (item == nullptr)
            return 1;

        Node *n = new Node(std::unique_ptr<T>(item));
        n->fore = tail;
        n->next = nullptr;
        if (tail)
            tail->next = n;
        else
            head = n;
        tail = n;
        // update raw pointers in value
        tail->val->fore = tail->fore ? tail->fore->val.get() : nullptr;
        tail->val->next = nullptr;
        if (tail->fore)
            tail->fore->val->next = tail->val.get();
        ++cached_count;
        return 0;
    }

    int AddAfterNode(T *node, T *item) noexcept
    {
        FnTrace("DList::AddAfterNode()");
        if (node == nullptr)
            return AddToHead(item);
        if (node == (tail ? tail->val.get() : nullptr))
            return AddToTail(item);

        Node *nnode = FindNode(node);
        if (!nnode)
            return AddToHead(item);

        Node *newn = new Node(std::unique_ptr<T>(item));
        Node *nextn = nnode->next;
        newn->fore = nnode;
        newn->next = nextn;
        nnode->next = newn;
        if (nextn)
            nextn->fore = newn;

        // update raw pointers in values
        newn->val->fore = newn->fore ? newn->fore->val.get() : nullptr;
        newn->val->next = newn->next ? newn->next->val.get() : nullptr;
        if (newn->fore)
            newn->fore->val->next = newn->val.get();
        if (newn->next)
            newn->next->val->fore = newn->val.get();

        ++cached_count;
        return 0;
    }

    int AddBeforeNode(T *node, T *item) noexcept
    {
        FnTrace("DList::AddBeforeNode()");
        if (node == nullptr)
            return AddToTail(item);
        if (node == (head ? head->val.get() : nullptr))
            return AddToHead(item);

        Node *nnode = FindNode(node);
        if (!nnode)
            return AddToTail(item);

        Node *newn = new Node(std::unique_ptr<T>(item));
        Node *prevn = nnode->fore;
        newn->next = nnode;
        newn->fore = prevn;
        nnode->fore = newn;
        if (prevn)
            prevn->next = newn;

        // update raw pointers
        newn->val->next = newn->next ? newn->next->val.get() : nullptr;
        newn->val->fore = newn->fore ? newn->fore->val.get() : nullptr;
        if (newn->next)
            newn->next->val->fore = newn->val.get();
        if (newn->fore)
            newn->fore->val->next = newn->val.get();

        ++cached_count;
        return 0;
    }

    // overload taking ownership via unique_ptr
    int AddToHead(std::unique_ptr<T> item) noexcept
    {
        FnTrace("DList::AddToHead(unique_ptr)");
        if (!item)
            return 1;

        Node *n = new Node(std::move(item));
        n->fore = nullptr;
        n->next = head;
        if (head)
            head->fore = n;
        else
            tail = n;
        head = n;
        // update raw pointers in value
        head->val->fore = nullptr;
        head->val->next = head->next ? head->next->val.get() : nullptr;
        if (head->next)
            head->next->val->fore = head->val.get();
        ++cached_count;
        return 0;
    }

    // overload taking ownership via unique_ptr
    int AddToTail(std::unique_ptr<T> item) noexcept
    {
        FnTrace("DList::AddToTail(unique_ptr)");
        if (!item)
            return 1;

        Node *n = new Node(std::move(item));
        n->fore = tail;
        n->next = nullptr;
        if (tail)
            tail->next = n;
        else
            head = n;
        tail = n;
        // update raw pointers in value
        tail->val->fore = tail->fore ? tail->fore->val.get() : nullptr;
        tail->val->next = nullptr;
        if (tail->fore)
            tail->fore->val->next = tail->val.get();
        ++cached_count;
        return 0;
    }

    // overload taking ownership via unique_ptr
    int AddAfterNode(T *node, std::unique_ptr<T> item) noexcept
    {
        FnTrace("DList::AddAfterNode(unique_ptr)");
        if (!item)
            return 1;
        if (node == nullptr)
            return AddToHead(std::move(item));
        if (node == (tail ? tail->val.get() : nullptr))
            return AddToTail(std::move(item));

        Node *nnode = FindNode(node);
        if (!nnode)
            return AddToHead(std::move(item));

        Node *newn = new Node(std::move(item));
        Node *nextn = nnode->next;
        newn->fore = nnode;
        newn->next = nextn;
        nnode->next = newn;
        if (nextn)
            nextn->fore = newn;

        // update raw pointers in values
        newn->val->fore = newn->fore ? newn->fore->val.get() : nullptr;
        newn->val->next = newn->next ? newn->next->val.get() : nullptr;
        if (newn->fore)
            newn->fore->val->next = newn->val.get();
        if (newn->next)
            newn->next->val->fore = newn->val.get();

        ++cached_count;
        return 0;
    }

    // overload taking ownership via unique_ptr
    int AddBeforeNode(T *node, std::unique_ptr<T> item) noexcept
    {
        FnTrace("DList::AddBeforeNode(unique_ptr)");
        if (!item)
            return 1;
        if (node == nullptr)
            return AddToTail(std::move(item));
        if (node == (head ? head->val.get() : nullptr))
            return AddToHead(std::move(item));

        Node *nnode = FindNode(node);
        if (!nnode)
            return AddToTail(std::move(item));

        Node *newn = new Node(std::move(item));
        Node *prevn = nnode->fore;
        newn->next = nnode;
        newn->fore = prevn;
        nnode->fore = newn;
        if (prevn)
            prevn->next = newn;

        // update raw pointers
        newn->val->next = newn->next ? newn->next->val.get() : nullptr;
        newn->val->fore = newn->fore ? newn->fore->val.get() : nullptr;
        if (newn->next)
            newn->next->val->fore = newn->val.get();
        if (newn->fore)
            newn->fore->val->next = newn->val.get();

        ++cached_count;
        return 0;
    }

    

    [[nodiscard]] bool Exists(T *item, int (*cmp)(T *, T*)) noexcept
    {
        FnTrace("DList::Exists()");
        if (item == nullptr)
            return false;

        for (Node *n = head; n != nullptr; n = n->next)
        {
            if (cmp(item, n->val.get()) == 0)
                return true;
        }
        return false;
    }

    int Remove(T *item) noexcept
    {
        FnTrace("DList::Remove()");
        if (item == nullptr)
            return 1;

        Node *n = FindNode(item);
        if (!n)
            return 1;

        Node *prev = n->fore;
        Node *nxt = n->next;

        if (head == n)
            head = nxt;
        if (tail == n)
            tail = prev;
        if (nxt)
            nxt->fore = prev;
        if (prev)
            prev->next = nxt;

        // update raw pointers in surrounding values
        if (nxt)
            nxt->val->fore = prev ? prev->val.get() : nullptr;
        if (prev)
            prev->val->next = nxt ? nxt->val.get() : nullptr;

        // release ownership to caller (old behavior)
        n->val.release();
        delete n;
        --cached_count;
        return 0;
    }

    int RemoveSafe(T *node) noexcept
    {
        FnTrace("DList::RemoveSafe()");
        if (node == nullptr)
            return 1;
        for (Node *n = head; n != nullptr; n = n->next)
            if (n->val.get() == node)
                return Remove(node);
        return 1;
    }

    int RemoveAndDelete(T *item) noexcept
    {
        FnTrace("DList::RemoveAndDelete()");
        if (item == nullptr)
            return 1;

        Node *n = FindNode(item);
        if (!n)
            return 1;

        Node *prev = n->fore;
        Node *nxt = n->next;

        if (head == n)
            head = nxt;
        if (tail == n)
            tail = prev;
        if (nxt)
            nxt->fore = prev;
        if (prev)
            prev->next = nxt;

        // update raw pointers in surrounding values
        if (nxt)
            nxt->val->fore = prev ? prev->val.get() : nullptr;
        if (prev)
            prev->val->next = nxt ? nxt->val.get() : nullptr;

        // Node destructor will delete the held value
        delete n;
        --cached_count;
        return 0;
    }

    // Return ownership of the removed node's value as a std::unique_ptr
    [[nodiscard]] std::unique_ptr<T> RemoveReturningUnique(T *item) noexcept
    {
        FnTrace("DList::RemoveReturningUnique()");
        if (item == nullptr)
            return nullptr;

        Node *n = FindNode(item);
        if (!n)
            return nullptr;

        Node *prev = n->fore;
        Node *nxt = n->next;

        if (head == n)
            head = nxt;
        if (tail == n)
            tail = prev;
        if (nxt)
            nxt->fore = prev;
        if (prev)
            prev->next = nxt;

        // update raw pointers in surrounding values
        if (nxt)
            nxt->val->fore = prev ? prev->val.get() : nullptr;
        if (prev)
            prev->val->next = nxt ? nxt->val.get() : nullptr;

        // Move ownership to caller
        std::unique_ptr<T> ret = std::move(n->val);
        delete n;
        --cached_count;
        return ret;
    }

    void Purge() noexcept
    {
        FnTrace("DList::Purge()");
        while (head)
        {
            Node *tmp = head;
            head = head->next;
            delete tmp;
        }
        tail = nullptr;
        cached_count = 0;
    }

    [[nodiscard]] int Count() const noexcept { return cached_count; }

    [[nodiscard]] T *Index(int i) noexcept
    {
        FnTrace("DList::Index()");
        if (i < 0)
            return nullptr;
        Node *n = head;
        while (n != nullptr && i > 0)
        {
            --i;
            n = n->next;
        }
        return n ? n->val.get() : nullptr;
    }

    int Sort(int (*cmp)(T *, T *)) noexcept
    {
        FnTrace("DList::Sort()");
        if (!head || !head->next)
            return 0;

        // collect nodes
        std::vector<Node*> nodes;
        for (Node *n = head; n != nullptr; n = n->next)
            nodes.push_back(n);

        std::stable_sort(nodes.begin(), nodes.end(), [&](Node *a, Node *b){ return cmp(a->val.get(), b->val.get()) < 0; });

        // relink
        head = nodes.front();
        head->fore = nullptr;
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            Node *cur = nodes[i];
            Node *nxt = (i + 1 < nodes.size()) ? nodes[i + 1] : nullptr;
            Node *prv = (i > 0) ? nodes[i - 1] : nullptr;
            cur->next = nxt;
            cur->fore = prv;
            cur->val->next = nxt ? nxt->val.get() : nullptr;
            cur->val->fore = prv ? prv->val.get() : nullptr;
        }
        tail = nodes.back();
        return 0;
    }

    [[nodiscard]] T *operator[](int i) noexcept { return Index(i); }
};

#endif
