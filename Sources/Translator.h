#pragma once

#include <SPIRV/spirv.h>
#include <vector>

namespace krafix {
	class Instruction {
	public:
		Instruction(std::vector<unsigned>& spirv, unsigned& index);

		spv::OpCode opcode;
		unsigned* operands;
		const char* string;
	};

	class Translator {
	public:
		Translator(std::vector<unsigned>& spirv);
		virtual void outputCode(const char* name) = 0;
	protected:
		std::vector<Instruction> instructions;
	};
}
