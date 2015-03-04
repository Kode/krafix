#pragma once

#include "Translator.h"

namespace krafix {
	class GlslTranslator : public Translator {
	public:
		GlslTranslator(std::vector<unsigned>& spirv) : Translator(spirv) {}
		void outputCode(const char* name);
	};
}
