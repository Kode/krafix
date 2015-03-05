#include "Translator.h"

using namespace krafix;

Instruction::Instruction(std::vector<unsigned>& spirv, unsigned& index) {
	using namespace spv;

	int wordCount = spirv[index] >> 16;
	opcode = (OpCode)(spirv[index] & 0xffff);

	operands = wordCount > 1 ? &spirv[index + 1] : NULL;

	switch (opcode) {
	case OpString:
		string = (char*)&spirv[index + 2];
		break;
	case OpName:
		string = (char*)&spirv[index + 2];
		break;
	default:
		string = NULL;
		break;
	}

	index += wordCount;
}

Translator::Translator(std::vector<unsigned>& spirv, EShLanguage stage) : stage(stage) {
	unsigned index = 0;
	unsigned magicNumber = spirv[index++];
	unsigned version = spirv[index++];
	unsigned generator = spirv[index++];
	unsigned bound = spirv[index++];
	index++;

	while (index < spirv.size()) {
		instructions.push_back(Instruction(spirv, index));
	}

	printf("Read %i instructions.\n", instructions.size());
}
