// Reads lines from stdin and prints each one scrubbed by include/Vitals/Scrub.h. A first character 'C' means a crash
// log line, anything else plain text; the rest of the line is the input. Used by server/test/cross.test.mjs to check
// that server/scrub.js gives byte-for-byte the same results on random input.
#include "Vitals/Scrub.h"

#include <iostream>
#include <string>

int main()
{
	namespace S = Vitals::Scrub;
	const S::Context ctx{ "Robin Hale", "DESKTOP-4F7K2Q", "", { { "D:\\Games\\Fallout 4", "<game>" }, { "C:\\Users\\Robin Hale\\Documents", "<docs>" } } };
	std::string      line;
	while (std::getline(std::cin, line)) {
		if (line.empty()) {
			std::cout << "\n";
			continue;
		}
		S::Counts n;
		std::cout << (line[0] == 'C' ? S::CrashLog(line.substr(1), ctx, n) : S::Text(line.substr(1), ctx, n)) << "\n";
	}
}
