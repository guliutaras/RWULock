#include "../RWLock/rwlock.h"

concurency::UpgradableMutex mtx;

void reader()
{
    for (size_t i = 0; i < 10; ++i)
    {
        concurency::RWULock lock(mtx, concurency::shared_state);
    }
}

void writer()
{
    for (size_t i = 0; i < 10; ++i)
    {
        concurency::RWULock lock(mtx, concurency::exclusive_state);
    }
}
int main()
{
    std::thread r1(reader);
    std::thread r2(reader);
    std::thread w1(writer);
    std::thread r4(reader);

    std::thread w2(writer);
    std::thread r3(reader);

    w1.join();
    w2.join();
    r1.join();
    r2.join();
    r3.join();
    r4.join();
}