#pragma once

#include "Translator.h"
#include <SPIRV/GLSL.std.450.h>

namespace krafix {
	class AgalTranslator : public Translator {
	public:
		AgalTranslator(std::vector<unsigned>& spirv, EShLanguage stage) : Translator(spirv, stage) {}
		void outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes);
	};
}
