#pragma once

#include "Translator.h"

namespace krafix {
	class HlslTranslator2 : public Translator {
	public:
		HlslTranslator2(std::vector<unsigned>& spirv, ShaderStage stage) : Translator(spirv, stage) {}
		void outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes);
	};
}
