// Test-only worker that never replies; used to prove process-tree timeout.
#include <chrono>
#include <thread>
int main(){
    std::this_thread::sleep_for(std::chrono::seconds(60));
    return 0;
}
