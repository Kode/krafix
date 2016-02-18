#include "SpirVTranslator.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <string.h>
#include <sstream>

using namespace krafix;

void SpirVTranslator::outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) {
	using namespace spv;
	
	std::ofstream out;
	out.open(filename, std::ios::binary | std::ios::out);

	for (unsigned i = 0; i < spirv.size(); ++i) {
		out.put(spirv[i] & 0xff);
		out.put((spirv[i] >> 8) & 0xff);
		out.put((spirv[i] >> 16) & 0xff);
		out.put((spirv[i] >> 24) & 0xff);
	}

	out.close();
}
