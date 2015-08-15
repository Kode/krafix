#pragma once

#include "Translator.h"
#include <SPIRV/GLSL450Lib.h>
#include <fstream>
#include <sstream>

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
		bool isarray;

		Type() : name("unknown"), length(1), isarray(false) {}
	};

	struct Name {
		const char* name;
	};

	struct Parameter {
		Type type;
		unsigned id;
	};

	struct Function {
		std::string name;
		std::stringstream text;
	};

	struct Merge {
		bool loop;
	};

	class CStyleTranslator : public Translator {
	public:
		CStyleTranslator(std::vector<unsigned>& spirv, EShLanguage stage);
		virtual ~CStyleTranslator() {}
		virtual void outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst);
		virtual void outputLibraryInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst, GLSL_STD_450::Entrypoints entrypoint);
		void startFunction(std::string name);
		void endFunction();
	protected:
		std::ostream* out;
		std::map<unsigned, Name> names;
		std::map<unsigned, Type> types;
		std::map<unsigned, Variable> variables;
		std::map<unsigned, std::string> labelStarts;
		std::map<unsigned, Merge> merges;
		std::map<unsigned, std::string> references;
		std::map<unsigned, std::vector<unsigned>> compositeInserts;
		std::vector<Parameter> parameters;
		std::vector<unsigned> callParameters;
		int indentation = 0;
		bool outputting = false;
		bool firstFunction = true;
		std::string funcName;
		std::string funcType;
		bool firstLabel = true;
		unsigned entryPoint = -1;
		std::vector<Function*> functions;
		std::ostream* tempout = NULL;
		
		virtual std::string indexName(const std::vector<unsigned>& indices);
		void indent(std::ostream* out);
		void output(std::ostream* out);
		std::string getReference(unsigned _id);
	};
}
