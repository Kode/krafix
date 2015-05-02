#pragma once

#include "Translator.h"

namespace krafix {
	class HlslTranslator : public Translator {
	public:
		HlslTranslator(std::vector<unsigned>& spirv, EShLanguage stage) : Translator(spirv, stage) {}
		void outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes);
	};
}
