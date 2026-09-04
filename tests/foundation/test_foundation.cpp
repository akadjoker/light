#include <stdio.h>
#include <stdlib.h>

#include "light_api.h"
#include "light_types.h"

#include <ct/arena.hpp>
#include <ct/hashmap.hpp>
#include <ct/string.hpp>
#include <ct/vector.hpp>

using namespace light;

static int failures = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

struct CountingAlloc
{
    static usize live_bytes;
    ct::HeapAlloc base;

    void* allocate(usize bytes, usize align)
    {
        live_bytes += bytes;
        return base.allocate(bytes, align);
    }
    void deallocate(void* p, usize bytes)
    {
        live_bytes -= bytes;
        base.deallocate(p, bytes);
    }
    void* reallocate(void* p, usize old_bytes, usize new_bytes, usize align)
    {
        live_bytes += new_bytes - old_bytes;
        return base.reallocate(p, old_bytes, new_bytes, align);
    }
};
usize CountingAlloc::live_bytes = 0;

static void test_types()
{
    CHECK(sizeof(u8) == 1 && sizeof(i8) == 1);
    CHECK(sizeof(u16) == 2 && sizeof(i16) == 2);
    CHECK(sizeof(u32) == 4 && sizeof(i32) == 4);
    CHECK(sizeof(u64) == 8 && sizeof(i64) == 8);
    u8 max_u8 = 0xFF;
    i64 widened = (i64)(u64)max_u8;
    CHECK(widened == 255);
    i8 neg = -1;
    CHECK((i64)neg == -1);
}

static void test_vector_with_custom_alloc()
{
    {
        ct::Vector<i32, CountingAlloc> v;
        for (i32 i = 0; i < 1000; i++)
            v.push_back(i * 2);
        CHECK(v.size() == 1000);
        CHECK(v[999] == 1998);
        CHECK(CountingAlloc::live_bytes > 0);
    }
    CHECK(CountingAlloc::live_bytes == 0);
}

static void test_hashmap()
{
    ct::HashMap<u32, i64> m;
    for (u32 i = 0; i < 500; i++)
        m.put(i, (i64)i * 3);
    CHECK(m.size() == 500);
    i64* found = m.find(250);
    CHECK(found != nullptr && *found == 750);
    CHECK(m.find(9999) == nullptr);
}

static void test_arena_reset()
{
    ct::BasicArena<> arena(4096);
    struct Node
    {
        Node* left;
        Node* right;
        i64 value;
    };
    Node* root = arena.create<Node>();
    root->value = 42;
    root->left = arena.create<Node>();
    root->left->value = 7;
    CHECK(root->value == 42 && root->left->value == 7);

    arena.reset();
    Node* again = arena.create<Node>();
    again->value = 1;
    CHECK(again->value == 1);
}

static void test_string()
{
    ct::String s("light");
    s += " vm";
    CHECK(s.size() == 8);
    CHECK(s == "light vm");
}

static void test_api_header()
{
    CHECK(version_string() != nullptr);
    VmConfig cfg = {};
    CHECK(cfg.alloc == nullptr);
}

int main()
{
    test_types();
    test_vector_with_custom_alloc();
    test_hashmap();
    test_arena_reset();
    test_string();
    test_api_header();

    if (failures == 0)
    {
        printf("foundation: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "foundation: %d failure(s)\n", failures);
    return 1;
}
