#pragma once

#include "Translator.h"

namespace krafix {
	class AgalTranslator : public Translator {
	public:
		AgalTranslator(std::vector<unsigned>& spirv) : Translator(spirv) {}
		void outputCode(const char* name) {}
	};
}
