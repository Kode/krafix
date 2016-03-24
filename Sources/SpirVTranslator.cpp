#include "SpirVTranslator.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <string.h>
#include <sstream>

using namespace krafix;

namespace {
	void writeInstruction(std::ofstream& out, unsigned word) {
		out.put(word & 0xff);
		out.put((word >> 8) & 0xff);
		out.put((word >> 16) & 0xff);
		out.put((word >> 24) & 0xff);
	}
}

void SpirVTranslator::writeInstructions(const char* filename, std::vector<Instruction>& instructions) {
	std::ofstream out;
	out.open(filename, std::ios::binary | std::ios::out);

	writeInstruction(out, magicNumber);
	writeInstruction(out, version);
	writeInstruction(out, generator);
	writeInstruction(out, bound);
	writeInstruction(out, schema);

	for (unsigned i = 0; i < instructions.size(); ++i) {
		Instruction& inst = instructions[i];
		writeInstruction(out, ((inst.length + 1) << 16) | (unsigned)inst.opcode);
		for (unsigned i2 = 0; i2 < inst.length; ++i2) {
			writeInstruction(out, inst.operands[i2]);
		}
	}

	out.close();
}

void SpirVTranslator::outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) {
	using namespace spv;
		
	/*
	for (unsigned i = 0; i < spirv.size(); ++i) {
		out.put(spirv[i] & 0xff);
		out.put((spirv[i] >> 8) & 0xff);
		out.put((spirv[i] >> 16) & 0xff);
		out.put((spirv[i] >> 24) & 0xff);
	}
	*/
	
	writeInstructions(filename, instructions);
}
