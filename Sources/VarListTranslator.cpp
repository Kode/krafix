#include "VarListTranslator.h"
#include <fstream>

using namespace krafix;

namespace {
	typedef unsigned id;

	struct Name {
		const char* name;
	};

	struct Type {
		const char* name;
		unsigned length;
		bool isarray;

		Type() : name("unknown"), length(1), isarray(false) {}
	};

	struct Variable {
		unsigned id;
		unsigned type;
		spv::StorageClass storage;
		bool builtin;

		Variable() : builtin(false) {}
	};
}

void VarListTranslator::outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) {
	using namespace spv;

	std::map<unsigned, Name> names;
	std::map<unsigned, Variable> variables;
	std::map<unsigned, Type> types;

	std::ofstream out;
	out.open(filename, std::ios::binary | std::ios::out);

	for (unsigned i = 0; i < instructions.size(); ++i) {
		Instruction& inst = instructions[i];
		switch (inst.opcode) {
		case OpName: {
			unsigned id = inst.operands[0];
			if (strcmp(inst.string, "") != 0) {
				Name n;
				n.name = inst.string;
				names[id] = n;
			}
			break;
		}
		case OpVariable: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;
			Variable& v = variables[result];
			v.id = result;
			v.type = inst.operands[0];
			v.storage = (StorageClass)inst.operands[2];

			if (names.find(result) != names.end()) {
				out << names[result].name << "\n";
			}

			break;
		}
		}
	}

	out.close();
}
