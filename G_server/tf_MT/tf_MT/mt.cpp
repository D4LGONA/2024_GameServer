#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>
#include <queue>
#include<concurrent_queue.h>

using namespace std::chrono;

volatile int g_sum = 0;
std::mutex sum_lock;
//std::atomic<std::queue<int>>my_queue; // �� ����� �����ڵ�? �� ������
// ������� �����Ѵٴ� �ǰ�? ��������ڶ� �̵������ڰ� �־����
// ������ queue���¾���(������ �ڷᱸ���̱� �x��)
concurrency::concurrent_queue<int> my_queue; // �긦 ��� ���

struct POS { int x, y, z; };
std::atomic <POS> my_pos; // ��� �����ϴ�

void thread_worker(const int num_th)
{
	volatile int local_sum{0};
	for (int i = 0; i < 50000000 / num_th; ++i) {
		local_sum = local_sum + 2;
	}
	sum_lock.lock(); 
	g_sum += local_sum;
	sum_lock.unlock();
}

std::atomic<int>a_sum;
void a_thread_worker(const int num_th)
{
	for (int i = 0; i < 50000000 / num_th; ++i) {
		a_sum += 2;
	}
}

int main()
{
	{
		volatile int sum = 0; // release���� ����ȭ �ȴ��ϰ� �ϴ� ��

		auto start_t = high_resolution_clock::now();
		for (int i = 0; i < 50000000; ++i)
			sum = sum + 2;
		auto end_t = high_resolution_clock::now();
		auto exec_t = end_t - start_t;
		auto exec_ms = duration_cast<milliseconds>(exec_t).count();

		std::cout << " Sum = " << sum << ", Time = " << exec_ms << "ms" << std::endl;
	}

	
	{
		for (int num_threads =1; num_threads <= 16; num_threads *= 2) {
			g_sum = 0;
			std::vector<std::thread> threads;
			auto start_t = high_resolution_clock::now();
			for (int i = 0; i < num_threads; ++i) {
				threads.emplace_back(thread_worker, num_threads);
			}
			
			for(auto&th:threads)
				th.join();

			auto end_t = high_resolution_clock::now();
			auto exec_t = end_t - start_t;
			auto exec_ms = duration_cast<milliseconds>(exec_t).count();

			std::cout << num_threads<<" threads Sum = " << g_sum << ", Time = " << exec_ms << "ms" << std::endl;
		}
	}

	{
		for (int num_threads = 1; num_threads <= 16; num_threads *= 2) {
			a_sum = 0;
			std::vector<std::thread> threads;
			auto start_t = high_resolution_clock::now();
			for (int i = 0; i < num_threads; ++i) {
				threads.emplace_back(a_thread_worker, num_threads);
			}

			for (auto& th : threads)
				th.join();

			auto end_t = high_resolution_clock::now();
			auto exec_t = end_t - start_t;
			auto exec_ms = duration_cast<milliseconds>(exec_t).count();

			std::cout << num_threads << " threads Sum = " << a_sum << ", Time = " << exec_ms << "ms" << std::endl;
		}
	}
}