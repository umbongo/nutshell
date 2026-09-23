#include "test_framework.h"
#include "vector.h"

int test_vector_init(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);
    ASSERT_EQ(vec_size(&v), (size_t)0);
    ASSERT_NULL(vec_get(&v, 0));
    vec_free(&v);
    TEST_END();
}

int test_vector_push_and_get(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);

    vec_push(&v, (void *)1);
    vec_push(&v, (void *)2);
    vec_push(&v, (void *)3);

    ASSERT_EQ(vec_size(&v), (size_t)3);
    ASSERT_EQ(vec_get(&v, 0), (void *)1);
    ASSERT_EQ(vec_get(&v, 1), (void *)2);
    ASSERT_EQ(vec_get(&v, 2), (void *)3);

    vec_free(&v);
    TEST_END();
}

int test_vector_get_oob(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);
    vec_push(&v, (void *)42);

    ASSERT_NULL(vec_get(&v, 1));   /* index == size, out of bounds */
    ASSERT_NULL(vec_get(&v, 99));

    vec_free(&v);
    TEST_END();
}

int test_vector_set(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);
    vec_push(&v, (void *)1);
    vec_push(&v, (void *)2);

    vec_set(&v, 0, (void *)99);
    ASSERT_EQ(vec_get(&v, 0), (void *)99);
    ASSERT_EQ(vec_get(&v, 1), (void *)2);
    ASSERT_EQ(vec_size(&v), (size_t)2);

    /* set out of bounds — no-op */
    vec_set(&v, 5, (void *)0);
    ASSERT_EQ(vec_size(&v), (size_t)2);

    vec_free(&v);
    TEST_END();
}

int test_vector_remove(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);
    vec_push(&v, (void *)10);
    vec_push(&v, (void *)20);
    vec_push(&v, (void *)30);

    vec_remove(&v, 1);  /* remove middle */

    ASSERT_EQ(vec_size(&v), (size_t)2);
    ASSERT_EQ(vec_get(&v, 0), (void *)10);
    ASSERT_EQ(vec_get(&v, 1), (void *)30);

    vec_free(&v);
    TEST_END();
}

int test_vector_remove_first(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);
    vec_push(&v, (void *)1);
    vec_push(&v, (void *)2);
    vec_push(&v, (void *)3);

    vec_remove(&v, 0);

    ASSERT_EQ(vec_size(&v), (size_t)2);
    ASSERT_EQ(vec_get(&v, 0), (void *)2);
    ASSERT_EQ(vec_get(&v, 1), (void *)3);

    vec_free(&v);
    TEST_END();
}

int test_vector_remove_last(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);
    vec_push(&v, (void *)1);
    vec_push(&v, (void *)2);

    vec_remove(&v, 1);

    ASSERT_EQ(vec_size(&v), (size_t)1);
    ASSERT_EQ(vec_get(&v, 0), (void *)1);

    vec_free(&v);
    TEST_END();
}

int test_vector_remove_oob(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);
    vec_push(&v, (void *)1);

    vec_remove(&v, 5);  /* no-op */
    ASSERT_EQ(vec_size(&v), (size_t)1);

    vec_free(&v);
    TEST_END();
}

int test_vector_grows(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);

    /* Push more than the initial capacity (8) to force a realloc */
    for (int i = 0; i < 20; i++) {
        vec_push(&v, (void *)(size_t)i);
    }

    ASSERT_EQ(vec_size(&v), (size_t)20);
    ASSERT_EQ(vec_get(&v, 0),  (void *)(size_t)0);
    ASSERT_EQ(vec_get(&v, 19), (void *)(size_t)19);

    vec_free(&v);
    TEST_END();
}

int test_vector_free_reuse(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);
    vec_push(&v, (void *)1);
    vec_free(&v);

    /* After free, the vector should be usable again */
    ASSERT_EQ(vec_size(&v), (size_t)0);
    vec_push(&v, (void *)2);
    ASSERT_EQ(vec_size(&v), (size_t)1);
    vec_free(&v);
    TEST_END();
}

int test_vector_insert_at_zero_into_empty(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);

    vec_insert(&v, 0, (void *)42);

    ASSERT_EQ(vec_size(&v), (size_t)1);
    ASSERT_EQ(vec_get(&v, 0), (void *)42);

    vec_free(&v);
    TEST_END();
}

int test_vector_insert_at_zero_shifts_existing(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);
    vec_push(&v, (void *)1);
    vec_push(&v, (void *)2);

    vec_insert(&v, 0, (void *)99);

    ASSERT_EQ(vec_size(&v), (size_t)3);
    ASSERT_EQ(vec_get(&v, 0), (void *)99);
    ASSERT_EQ(vec_get(&v, 1), (void *)1);
    ASSERT_EQ(vec_get(&v, 2), (void *)2);

    vec_free(&v);
    TEST_END();
}

int test_vector_insert_in_middle(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);
    vec_push(&v, (void *)1);
    vec_push(&v, (void *)2);
    vec_push(&v, (void *)3);

    vec_insert(&v, 1, (void *)77);

    ASSERT_EQ(vec_size(&v), (size_t)4);
    ASSERT_EQ(vec_get(&v, 0), (void *)1);
    ASSERT_EQ(vec_get(&v, 1), (void *)77);
    ASSERT_EQ(vec_get(&v, 2), (void *)2);
    ASSERT_EQ(vec_get(&v, 3), (void *)3);

    vec_free(&v);
    TEST_END();
}

int test_vector_insert_at_size_appends(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);
    vec_push(&v, (void *)1);
    vec_push(&v, (void *)2);

    vec_insert(&v, 2, (void *)3);

    ASSERT_EQ(vec_size(&v), (size_t)3);
    ASSERT_EQ(vec_get(&v, 0), (void *)1);
    ASSERT_EQ(vec_get(&v, 1), (void *)2);
    ASSERT_EQ(vec_get(&v, 2), (void *)3);

    vec_free(&v);
    TEST_END();
}

int test_vector_insert_past_size_appends(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);
    vec_push(&v, (void *)1);

    vec_insert(&v, 99, (void *)2);

    ASSERT_EQ(vec_size(&v), (size_t)2);
    ASSERT_EQ(vec_get(&v, 0), (void *)1);
    ASSERT_EQ(vec_get(&v, 1), (void *)2);

    vec_free(&v);
    TEST_END();
}

int test_vector_insert_null_vector_is_noop(void)
{
    TEST_BEGIN();
    vec_insert(NULL, 0, (void *)1);  /* must not crash */
    ASSERT_TRUE(1);
    TEST_END();
}

int test_vector_insert_grows_past_initial_capacity(void)
{
    TEST_BEGIN();
    Vector v;
    vec_init(&v);

    /* Initial capacity is 8; insert enough to force reallocation. */
    for (int i = 0; i < 20; i++) {
        vec_insert(&v, 0, (void *)(size_t)i);
    }

    ASSERT_EQ(vec_size(&v), (size_t)20);
    /* Each insert at 0 pushes the previous ones back, so the last
     * inserted item (0) ends up at the front and the first inserted
     * (19) ends up at the back. */
    ASSERT_EQ(vec_get(&v, 0),  (void *)(size_t)19);
    ASSERT_EQ(vec_get(&v, 19), (void *)(size_t)0);

    vec_free(&v);
    TEST_END();
}
