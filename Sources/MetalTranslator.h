#pragma once

#include "Translator.h"

namespace krafix {
	class MetalTranslator : public Translator {
	public:
		MetalTranslator(std::vector<unsigned>& spirv, EShLanguage stage) : Translator(spirv, stage) {}
		void outputCode(const char* name);
	};
}
