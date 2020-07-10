#include "pch.h"
#include "../RWLock/rwlock.h"

concurency::UpgradableMutex mtx;
TEST(RWLock, DeadLockDetect) {
  EXPECT_EQ(1, 1);
  EXPECT_TRUE(true);
}

TEST(RWLock, RecurencyLockCheck) {
  EXPECT_EQ(1, 1);
  EXPECT_TRUE(true);
}

void Read(void)
{
    concurency::RWULock lock(mtx, concurency::shared_state);

    // DO WORK
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

void Write(void)
{
    concurency::RWULock lock(mtx, concurency::exclusive_state);

    // DO WORK
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

void ReadWrite(void)
{
    concurency::RWULock lock(mtx, concurency::shared_state);

    // DO SHARED WORK
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    lock.TryUpgrade();

    // DO EXCLUSIVE WORK
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
TEST(RWLock, UpdatableLockCheck) {
    std::thread t1(Read);
    std::thread t2(Write);
    std::thread t4(Write);
    std::thread t5(Read);
    std::thread t7(Read);
    std::thread t9(ReadWrite);
    std::thread t10(Read);

    t1.join();
    t2.join();
    t4.join();
    t5.join();
    t7.join();
    t9.join();
    t10.join();
    EXPECT_TRUE(true);

}
