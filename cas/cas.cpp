#include <iostream>
#include <atomic>

void test01()
{
    std::atomic<int> a;
    a.fetch_add(2, std::memory_order_release);

    char *p = "Hello \n";
    p[0] = 'j';

    std::cout << p << std::endl;
}