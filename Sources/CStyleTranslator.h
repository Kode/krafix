#pragma once

#include "Translator.h"
#include <fstream>

namespace krafix {
	struct Variable {
		unsigned id;
		unsigned type;
		spv::StorageClass storage;
		bool builtin;
		bool declared;

		Variable() : builtin(false) {}
	};

	struct Type {
		const char* name;
		unsigned length;

		Type() : name("unknown"), length(1) {}
	};

	struct Name {
		const char* name;
	};

	struct Parameter {
		Type type;
		unsigned id;
	};

	class CStyleTranslator : public Translator {
	public:
		CStyleTranslator(std::vector<unsigned>& spirv, EShLanguage stage);
		virtual ~CStyleTranslator() {}
		virtual void outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst);
	protected:
		std::ofstream out;
		std::map<unsigned, Name> names;
		std::map<unsigned, Type> types;
		std::map<unsigned, Variable> variables;
		std::map<unsigned, std::string> labelStarts;
		std::map<unsigned, int> merges;
		std::map<unsigned, std::string> references;
		std::map<unsigned, unsigned> compositeInserts;
		std::vector<Parameter> parameters;
		std::vector<unsigned> callParameters;
		int indentation = 0;
		bool outputting = false;
		bool firstFunction = true;
		std::string funcName;
		bool firstLabel = true;
		unsigned entryPoint = -1;
		
		const char* indexName(unsigned index);
		void indent(std::ofstream& out);
		void output(std::ofstream& out);
		std::string getReference(unsigned _id);
	};
}
