#include "GameBoy.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

/**
 * Runs a ROM headless and prints what it sends over the serial port.
 * Stops when a test ROM reports "Passed" / "Failed", the CPU locks up, or after `max-steps` steps.
 * Exit code: 0 passed, 1 failed or no result, 2 error.
 */
int main(int argc, char *argv[]){
	if (argc < 2){
		std::cerr << "usage: emulator <rom file> [max-steps]\n";
		return 2;
	}
	try {
		const std::uint64_t maxSteps = (argc > 2)? std::stoull(argv[2]): 200'000'000;
		GameBoy gameBoy(GameBoy::readRomFile(argv[1]));

		std::uint64_t steps = 0;
		const std::string &output = gameBoy.serialOutput();
		while (steps < maxSteps && !gameBoy.cpuState().locked
				&& output.find("Passed") == std::string::npos && output.find("Failed") == std::string::npos){
			gameBoy.step();
			++steps;
		}

		std::cout << output << '\n';
		std::cerr << steps << " steps" << (gameBoy.cpuState().locked? ", CPU locked": "") << '\n';
		const bool passed = output.find("Passed") != std::string::npos && output.find("Failed") == std::string::npos;
		return passed? 0: 1;
	} catch (const std::exception &e){
		std::cerr << "error: " << e.what() << '\n';
		return 2;
	}
}
