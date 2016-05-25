#pragma once

#include "Translator.h"

namespace krafix {
	class VarListTranslator : public Translator {
	public:
		VarListTranslator(std::vector<unsigned>& spirv, EShLanguage stage) : Translator(spirv, stage) {}
		void outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes);
	};
}
