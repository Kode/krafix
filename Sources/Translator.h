#pragma once

#include <SPIRV/spirv.h>
#include "../glslang/glslang/Public/ShaderLang.h"
#include <vector>

namespace krafix {
	class Instruction {
	public:
		Instruction(std::vector<unsigned>& spirv, unsigned& index);

		spv::Op opcode;
		unsigned* operands;
		unsigned length;
		const char* string;
	};

	class Translator {
	public:
		Translator(std::vector<unsigned>& spirv, EShLanguage stage);
		virtual ~Translator() {}
		virtual void outputCode(const char* name) = 0;
	protected:
		std::vector<Instruction> instructions;
		EShLanguage stage;
	};
}
