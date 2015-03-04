#pragma once

#include "Translator.h"

namespace krafix {
	class HlslTranslator : public Translator {
	public:
		HlslTranslator(std::vector<unsigned>& spirv) : Translator(spirv) {}
		void outputCode(const char* name) {}
	};
}
