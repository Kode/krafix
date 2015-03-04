#pragma once

#include "Translator.h"

namespace krafix {
	class MetalTranslator : public Translator {
	public:
		MetalTranslator(std::vector<unsigned>& spirv) : Translator(spirv) {}
		void outputCode(const char* name);
	};
}
