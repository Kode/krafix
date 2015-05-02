#pragma once

#include "Translator.h"

namespace krafix {
	class GlslTranslator : public Translator {
	public:
		GlslTranslator(std::vector<unsigned>& spirv, EShLanguage stage) : Translator(spirv, stage) {}
		void outputCode(const Target& target, const char* filename);
	};
}
