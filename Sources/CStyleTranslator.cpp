#include "CStyleTranslator.h"
#include <sstream>

using namespace krafix;

typedef unsigned id;

CStyleTranslator::CStyleTranslator(std::vector<unsigned>& spirv, EShLanguage stage) : Translator(spirv, stage) {

}

void CStyleTranslator::outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst) {
	switch (inst.opcode) {
	default:
		output(out);
		out << "// Unknown operation " << inst.opcode;
		break;
	}
}

const char* CStyleTranslator::indexName(unsigned index) {
	switch (index) {
	case 0:
		return "x";
	case 1:
		return "y";
	case 2:
		return "z";
	case 3:
	default:
		return "w";
	}
}

void CStyleTranslator::indent(std::ofstream& out) {
	for (int i = 0; i < indentation; ++i) {
		out << "\t";
	}
}

void CStyleTranslator::output(std::ofstream& out) {
	outputting = true;
	indent(out);
}

std::string CStyleTranslator::getReference(id _id) {
	if (references.find(_id) == references.end()) {
		std::stringstream str;
		str << "_" << _id;
		return str.str();
	}
	else {
		return references[_id];
	}
}
