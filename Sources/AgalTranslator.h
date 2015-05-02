#pragma once

#include "Translator.h"

namespace krafix {
	class AgalTranslator : public Translator {
	public:
		AgalTranslator(std::vector<unsigned>& spirv, EShLanguage stage) : Translator(spirv, stage) {}
		void outputCode(const Target& target, const char* filename);
	};
}
