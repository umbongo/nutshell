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
