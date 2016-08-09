#pragma once

#include "CStyleTranslator.h"

namespace krafix {
	class GlslTranslator : public CStyleTranslator {
	public:
		GlslTranslator(std::vector<unsigned>& spirv, ShaderStage stage) : CStyleTranslator(spirv, stage) {}
		void outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes);
		void outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst);
	};
}
