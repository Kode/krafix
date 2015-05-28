#pragma once

#include "CStyleTranslator.h"

namespace krafix {
	class HlslTranslator : public CStyleTranslator {
	public:
		HlslTranslator(std::vector<unsigned>& spirv, EShLanguage stage) : CStyleTranslator(spirv, stage) {}
		void outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes);
		void outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst);
		void outputLibraryInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst, GLSL_STD_450::Entrypoints entrypoint);
	};
}
