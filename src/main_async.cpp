#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

std::mutex cout_mutex;

int main() {
	int odd_value = 1;
	int even_value = 2;
	bool update_state = false;

	std::thread render([&] {
		while (true) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			{
				std::lock_guard<std::mutex> lock(cout_mutex);

				if (update_state) {
					odd_value += 2;
					std::cout << "thread_1: update odd_value to " << odd_value << std::endl;
				} else {
					even_value += 2;
					std::cout << "thread_1: update even_value to " << even_value << std::endl;
				}
			}

			update_state = !update_state;
		}
	});

	std::thread print_odd([&] {
		while (true) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));

			std::lock_guard<std::mutex> lock(cout_mutex);
			std::cout << "thread_2: odd_value is " << odd_value << std::endl;
		}
	});

	std::thread print_even([&] {
		while (true) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1100));

			std::lock_guard<std::mutex> lock(cout_mutex);
			std::cout << "thread_3: even_value is " << even_value << std::endl;
		}
	});

	render.join();
	print_odd.join();
	print_even.join();
}
